#include "midi/midi_file.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <deque>
#include <fstream>
#include <iterator>
#include <map>
#include <utility>

namespace sessionflow
{

namespace
{

constexpr std::uint8_t s_noteOff = 0x80;
constexpr std::uint8_t s_noteOn = 0x90;
constexpr std::uint8_t s_controlChange = 0xB0;
constexpr std::uint8_t s_meta = 0xFF;
constexpr std::uint8_t s_metaTrackName = 0x03;
constexpr std::uint8_t s_metaEndOfTrack = 0x2F;
constexpr std::uint8_t s_metaTempo = 0x51;
constexpr std::uint8_t s_metaTimeSignature = 0x58;
constexpr std::uint8_t s_noteOffVelocity = 64;
constexpr double s_microsecondsPerMinute = 60'000'000.0;
// The largest power of two TimeMap accepts as a time signature denominator, as an exponent.
constexpr std::uint8_t s_maxDenominatorPower = 6;

class ByteReader
{
  public:
    explicit ByteReader(std::span<const std::uint8_t> bytes) noexcept : m_bytes(bytes)
    {
    }

    bool atEnd() const noexcept
    {
        return m_position == m_bytes.size();
    }

    std::optional<std::uint8_t> peek() const noexcept
    {
        if (atEnd())
        {
            return std::nullopt;
        }
        return m_bytes[m_position];
    }

    std::optional<std::uint8_t> byte() noexcept
    {
        const std::optional<std::uint8_t> value = peek();
        if (value)
        {
            ++m_position;
        }
        return value;
    }

    std::optional<std::uint32_t> bigEndian(std::size_t size) noexcept
    {
        const std::optional<std::span<const std::uint8_t>> data = take(size);
        if (!data)
        {
            return std::nullopt;
        }
        std::uint32_t value = 0;
        for (const std::uint8_t part : *data)
        {
            value = (value << 8) | part;
        }
        return value;
    }

    // At most four bytes, as the format allows.
    std::optional<std::uint32_t> variableLength() noexcept
    {
        std::uint32_t value = 0;
        for (int index = 0; index < 4; ++index)
        {
            const std::optional<std::uint8_t> part = byte();
            if (!part)
            {
                return std::nullopt;
            }
            value = (value << 7) | (*part & 0x7Fu);
            if ((*part & 0x80) == 0)
            {
                return value;
            }
        }
        return std::nullopt;
    }

    std::optional<std::span<const std::uint8_t>> take(std::size_t size) noexcept
    {
        if (m_bytes.size() - m_position < size)
        {
            return std::nullopt;
        }
        const std::span<const std::uint8_t> data = m_bytes.subspan(m_position, size);
        m_position += size;
        return data;
    }

  private:
    std::span<const std::uint8_t> m_bytes;
    std::size_t m_position = 0;
};

struct OpenNote
{
    TickCount start = 0;
    std::uint8_t velocity = 0;
};

struct ParsedTrack
{
    MidiFileTrack track;
    bool hasEvents = false;
};

// Tempo and meter events from every track, keyed by position so a later one at the same tick
// replaces an earlier one.
struct ParsedTempoMap
{
    std::map<TickCount, double> tempos;
    std::map<TickCount, TimeSignatureEvent> timeSignatures;
};

std::optional<ParsedTrack> parseTrack(std::span<const std::uint8_t> bytes, std::uint32_t division,
                                      ParsedTempoMap& tempoMap)
{
    ByteReader reader(bytes);
    ParsedTrack parsed;
    std::vector<Note> notes;
    std::vector<ControlChange> controlChanges;
    // Overlapping notes of the same pitch and channel end in the order they started.
    std::map<std::pair<std::uint8_t, std::uint8_t>, std::deque<OpenNote>> openNotes;

    std::uint64_t fileTick = 0;
    std::uint8_t runningStatus = 0;
    TickCount endTick = 0;

    while (!reader.atEnd())
    {
        const std::optional<std::uint32_t> delta = reader.variableLength();
        if (!delta)
        {
            return std::nullopt;
        }
        fileTick += *delta;
        const auto tick =
            static_cast<TickCount>((fileTick * static_cast<std::uint64_t>(ticksPerBeat) + division / 2) / division);
        endTick = std::max(endTick, tick);

        const std::optional<std::uint8_t> next = reader.peek();
        if (!next)
        {
            return std::nullopt;
        }
        std::uint8_t status = runningStatus;
        if (*next & 0x80)
        {
            status = *reader.byte();
        }
        if (status == 0)
        {
            return std::nullopt;
        }

        if (status == s_meta)
        {
            runningStatus = 0;
            const std::optional<std::uint8_t> type = reader.byte();
            const std::optional<std::uint32_t> length = type ? reader.variableLength() : std::nullopt;
            const std::optional<std::span<const std::uint8_t>> data = length ? reader.take(*length) : std::nullopt;
            if (!data)
            {
                return std::nullopt;
            }
            if (*type == s_metaEndOfTrack)
            {
                break;
            }
            if (*type == s_metaTrackName)
            {
                parsed.track.name.assign(data->begin(), data->end());
            }
            else if (*type == s_metaTempo && data->size() == 3)
            {
                const std::uint32_t microsecondsPerQuarter =
                    (std::uint32_t{(*data)[0]} << 16) | (std::uint32_t{(*data)[1]} << 8) | (*data)[2];
                if (microsecondsPerQuarter > 0)
                {
                    tempoMap.tempos[tick] = s_microsecondsPerMinute / microsecondsPerQuarter;
                }
            }
            else if (*type == s_metaTimeSignature && data->size() >= 2 && (*data)[0] > 0 &&
                     (*data)[1] <= s_maxDenominatorPower)
            {
                tempoMap.timeSignatures[tick] = {
                    .position = tick, .numerator = (*data)[0], .denominator = 1 << (*data)[1]};
            }
            continue;
        }

        if (status == 0xF0 || status == 0xF7)
        {
            runningStatus = 0;
            const std::optional<std::uint32_t> length = reader.variableLength();
            if (!length || !reader.take(*length))
            {
                return std::nullopt;
            }
            continue;
        }
        if (status >= 0xF0)
        {
            // System common and real-time messages have no place in a file.
            return std::nullopt;
        }

        runningStatus = status;
        const std::uint8_t type = status & 0xF0;
        const std::uint8_t channel = status & 0x0F;
        const std::size_t dataSize = type == 0xC0 || type == 0xD0 ? 1 : 2;
        const std::optional<std::span<const std::uint8_t>> data = reader.take(dataSize);
        if (!data)
        {
            return std::nullopt;
        }

        if (type == s_noteOn && (*data)[1] > 0)
        {
            openNotes[{channel, (*data)[0]}].push_back({.start = tick, .velocity = (*data)[1]});
            parsed.hasEvents = true;
        }
        else if (type == s_noteOff || type == s_noteOn)
        {
            const auto open = openNotes.find({channel, (*data)[0]});
            if (open != openNotes.end() && !open->second.empty())
            {
                const OpenNote note = open->second.front();
                open->second.pop_front();
                notes.push_back(
                    {.pitch = (*data)[0], .velocity = note.velocity, .start = note.start, .length = tick - note.start});
            }
        }
        else if (type == s_controlChange)
        {
            controlChanges.push_back({.position = tick, .controller = (*data)[0], .value = (*data)[1]});
            parsed.hasEvents = true;
        }
    }

    // Notes still held when the track ends last until its end.
    for (const auto& [key, open] : openNotes)
    {
        for (const OpenNote& note : open)
        {
            notes.push_back(
                {.pitch = key.second, .velocity = note.velocity, .start = note.start, .length = endTick - note.start});
        }
    }
    std::stable_sort(notes.begin(), notes.end(), [](const Note& a, const Note& b)
                     { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });

    parsed.track.clip.length = endTick;
    parsed.track.clip.notes = immer::flex_vector<Note>(notes.begin(), notes.end());
    parsed.track.clip.controlChanges = immer::flex_vector<ControlChange>(controlChanges.begin(), controlChanges.end());
    return parsed;
}

// A file without an event at tick 0 starts at the format's default of 120 BPM in 4/4.
TempoMap toTempoMap(const ParsedTempoMap& parsed)
{
    std::vector<TempoEvent> tempos;
    if (parsed.tempos.empty() || parsed.tempos.begin()->first != 0)
    {
        tempos.push_back({});
    }
    for (const auto& [position, bpm] : parsed.tempos)
    {
        tempos.push_back({.position = position, .bpm = bpm});
    }

    std::vector<TimeSignatureEvent> timeSignatures;
    if (parsed.timeSignatures.empty() || parsed.timeSignatures.begin()->first != 0)
    {
        timeSignatures.push_back({});
    }
    for (const auto& [position, timeSignature] : parsed.timeSignatures)
    {
        timeSignatures.push_back(timeSignature);
    }

    return {.tempos = immer::flex_vector<TempoEvent>(tempos.begin(), tempos.end()),
            .timeSignatures = immer::flex_vector<TimeSignatureEvent>(timeSignatures.begin(), timeSignatures.end())};
}

class ByteWriter
{
  public:
    void byte(std::uint8_t value)
    {
        m_bytes.push_back(value);
    }

    void bigEndian(std::uint32_t value, std::size_t size)
    {
        for (std::size_t index = size; index > 0; --index)
        {
            byte(static_cast<std::uint8_t>(value >> (8 * (index - 1))));
        }
    }

    void variableLength(std::uint32_t value)
    {
        std::array<std::uint8_t, 5> groups{};
        std::size_t count = 0;
        do
        {
            groups[count++] = static_cast<std::uint8_t>(value & 0x7F);
            value >>= 7;
        } while (value != 0);
        while (count > 1)
        {
            byte(static_cast<std::uint8_t>(groups[--count] | 0x80));
        }
        byte(groups[0]);
    }

    void append(std::span<const std::uint8_t> bytes)
    {
        m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end());
    }

    std::vector<std::uint8_t>& bytes() noexcept
    {
        return m_bytes;
    }

  private:
    std::vector<std::uint8_t> m_bytes;
};

struct TrackEvent
{
    TickCount tick = 0;
    // At the same tick: note-offs, then controllers, then note-ons, so a note that ends where
    // the next one of the same pitch starts stays two notes. A note of length 0 ends after it
    // starts.
    int order = 0;
    std::vector<std::uint8_t> data;
};

std::vector<std::uint8_t> trackChunk(std::vector<TrackEvent> events, TickCount endTick)
{
    std::stable_sort(events.begin(), events.end(), [](const TrackEvent& a, const TrackEvent& b)
                     { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });

    ByteWriter body;
    TickCount previous = 0;
    for (const TrackEvent& event : events)
    {
        const TickCount tick = std::max(event.tick, previous);
        body.variableLength(static_cast<std::uint32_t>(tick - previous));
        body.append(event.data);
        previous = tick;
    }
    body.variableLength(static_cast<std::uint32_t>(std::max(endTick, previous) - previous));
    body.append(std::array<std::uint8_t, 3>{s_meta, s_metaEndOfTrack, 0});

    ByteWriter chunk;
    chunk.append(std::array<std::uint8_t, 4>{'M', 'T', 'r', 'k'});
    chunk.bigEndian(static_cast<std::uint32_t>(body.bytes().size()), 4);
    chunk.append(body.bytes());
    return std::move(chunk.bytes());
}

std::vector<std::uint8_t> metaEvent(std::uint8_t type, std::span<const std::uint8_t> data)
{
    ByteWriter writer;
    writer.byte(s_meta);
    writer.byte(type);
    writer.variableLength(static_cast<std::uint32_t>(data.size()));
    writer.append(data);
    return std::move(writer.bytes());
}

std::vector<std::uint8_t> tempoTrack(const TempoMap& tempoMap)
{
    std::vector<TrackEvent> events;
    TickCount endTick = 0;
    for (const TimeSignatureEvent& timeSignature : tempoMap.timeSignatures)
    {
        const auto power =
            static_cast<std::uint8_t>(std::countr_zero(static_cast<unsigned>(timeSignature.denominator)));
        const std::array<std::uint8_t, 4> data{static_cast<std::uint8_t>(timeSignature.numerator), power, 24, 8};
        events.push_back({.tick = timeSignature.position, .data = metaEvent(s_metaTimeSignature, data)});
        endTick = std::max(endTick, timeSignature.position);
    }
    for (const TempoEvent& tempo : tempoMap.tempos)
    {
        // Three bytes in the file.
        const auto microsecondsPerQuarter = static_cast<std::uint32_t>(
            std::lround(std::clamp(s_microsecondsPerMinute / tempo.bpm, 1.0, double{0xFFFFFF})));
        const std::array<std::uint8_t, 3> data{static_cast<std::uint8_t>(microsecondsPerQuarter >> 16),
                                               static_cast<std::uint8_t>(microsecondsPerQuarter >> 8),
                                               static_cast<std::uint8_t>(microsecondsPerQuarter)};
        events.push_back({.tick = tempo.position, .order = 1, .data = metaEvent(s_metaTempo, data)});
        endTick = std::max(endTick, tempo.position);
    }
    return trackChunk(std::move(events), endTick);
}

std::vector<std::uint8_t> clipTrack(const MidiFileTrack& track)
{
    const MidiClip& clip = track.clip;
    std::vector<TrackEvent> events;
    if (!track.name.empty())
    {
        const std::span<const std::uint8_t> name(reinterpret_cast<const std::uint8_t*>(track.name.data()),
                                                 track.name.size());
        events.push_back({.tick = 0, .order = -1, .data = metaEvent(s_metaTrackName, name)});
    }

    TickCount endTick = clip.position + clip.length;
    for (const Note& note : clip.notes)
    {
        const std::uint8_t pitch = note.pitch & 0x7F;
        const auto velocity = static_cast<std::uint8_t>(std::clamp<int>(note.velocity, 1, 127));
        const TickCount start = clip.position + note.start;
        const TickCount end = start + std::max<TickCount>(note.length, 0);
        events.push_back({.tick = start, .order = 2, .data = {s_noteOn, pitch, velocity}});
        events.push_back(
            {.tick = end, .order = note.length > 0 ? 0 : 3, .data = {s_noteOff, pitch, s_noteOffVelocity}});
        endTick = std::max(endTick, end);
    }
    for (const ControlChange& change : clip.controlChanges)
    {
        const TickCount tick = clip.position + change.position;
        events.push_back({.tick = tick,
                          .order = 1,
                          .data = {s_controlChange, static_cast<std::uint8_t>(change.controller & 0x7F),
                                   static_cast<std::uint8_t>(change.value & 0x7F)}});
        endTick = std::max(endTick, tick);
    }
    return trackChunk(std::move(events), endTick);
}

} // namespace

std::optional<MidiFile> parseMidiFile(std::span<const std::uint8_t> bytes)
{
    ByteReader reader(bytes);
    const std::optional<std::span<const std::uint8_t>> magic = reader.take(4);
    const std::optional<std::uint32_t> headerSize = reader.bigEndian(4);
    if (!magic || !std::ranges::equal(*magic, std::array<std::uint8_t, 4>{'M', 'T', 'h', 'd'}) || !headerSize ||
        *headerSize < 6)
    {
        return std::nullopt;
    }
    const std::optional<std::uint32_t> format = reader.bigEndian(2);
    const std::optional<std::uint32_t> trackCount = reader.bigEndian(2);
    const std::optional<std::uint32_t> division = reader.bigEndian(2);
    if (!division || !reader.take(*headerSize - 6) || *format > 1 || (*division & 0x8000) != 0 || *division == 0)
    {
        return std::nullopt;
    }

    MidiFile file;
    ParsedTempoMap tempoMap;
    std::uint32_t tracksRead = 0;
    while (tracksRead < *trackCount)
    {
        const std::optional<std::span<const std::uint8_t>> type = reader.take(4);
        const std::optional<std::uint32_t> size = type ? reader.bigEndian(4) : std::nullopt;
        const std::optional<std::span<const std::uint8_t>> body = size ? reader.take(*size) : std::nullopt;
        if (!body)
        {
            return std::nullopt;
        }
        if (!std::ranges::equal(*type, std::array<std::uint8_t, 4>{'M', 'T', 'r', 'k'}))
        {
            continue;
        }
        ++tracksRead;

        std::optional<ParsedTrack> track = parseTrack(*body, *division, tempoMap);
        if (!track)
        {
            return std::nullopt;
        }
        if (track->hasEvents)
        {
            file.tracks.push_back(std::move(track->track));
        }
    }
    file.tempoMap = toTempoMap(tempoMap);
    return file;
}

std::vector<std::uint8_t> serializeMidiFile(const MidiFile& file)
{
    ByteWriter writer;
    writer.append(std::array<std::uint8_t, 4>{'M', 'T', 'h', 'd'});
    writer.bigEndian(6, 4);
    writer.bigEndian(1, 2);
    writer.bigEndian(static_cast<std::uint32_t>(file.tracks.size() + 1), 2);
    writer.bigEndian(static_cast<std::uint32_t>(ticksPerBeat), 2);

    writer.append(tempoTrack(file.tempoMap));
    for (const MidiFileTrack& track : file.tracks)
    {
        writer.append(clipTrack(track));
    }
    return std::move(writer.bytes());
}

std::optional<MidiFile> readMidiFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return std::nullopt;
    }
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    return parseMidiFile(bytes);
}

bool writeMidiFile(const std::filesystem::path& path, const MidiFile& file)
{
    const std::vector<std::uint8_t> bytes = serializeMidiFile(file);
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(stream);
}

} // namespace sessionflow
