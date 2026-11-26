#include "midi/midi_file.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <vector>

using namespace sessionflow;

namespace
{

std::vector<std::uint8_t> bytes(std::initializer_list<int> values)
{
    std::vector<std::uint8_t> result;
    for (const int value : values)
    {
        result.push_back(static_cast<std::uint8_t>(value));
    }
    return result;
}

// A file with one track around body, at the given ticks per quarter note.
std::vector<std::uint8_t> smf(std::uint16_t format, std::uint16_t division, const std::vector<std::uint8_t>& body)
{
    std::vector<std::uint8_t> file = bytes({'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, format, 0, 1});
    file.push_back(static_cast<std::uint8_t>(division >> 8));
    file.push_back(static_cast<std::uint8_t>(division));
    const std::vector<std::uint8_t> header = bytes({'M', 'T', 'r', 'k'});
    file.insert(file.end(), header.begin(), header.end());
    const auto size = static_cast<std::uint32_t>(body.size());
    for (const int shift : {24, 16, 8, 0})
    {
        file.push_back(static_cast<std::uint8_t>(size >> shift));
    }
    file.insert(file.end(), body.begin(), body.end());
    return file;
}

MidiClip phrase()
{
    return {
        .position = 0,
        .length = 8 * ticksPerBeat,
        .notes = {Note{.pitch = 60, .velocity = 1, .start = 0, .length = ticksPerBeat},
                  Note{.pitch = 64, .velocity = 127, .start = 0, .length = 3 * ticksPerBeat},
                  Note{.pitch = 60, .velocity = 90, .start = ticksPerBeat, .length = ticksPerBeat / 3},
                  Note{.pitch = 67, .velocity = 45, .start = 5 * ticksPerBeat + 7, .length = 0},
                  Note{.pitch = 72, .velocity = 100, .start = 6 * ticksPerBeat, .length = 2 * ticksPerBeat}},
        .controlChanges = {ControlChange{.position = 0, .controller = 64, .value = 127},
                           ControlChange{.position = ticksPerBeat, .controller = 1, .value = 0},
                           ControlChange{.position = 4 * ticksPerBeat + 1, .controller = 1, .value = 99},
                           ControlChange{.position = 6 * ticksPerBeat, .controller = 64, .value = 0}},
    };
}

} // namespace

TEST_CASE("export and import round-trip notes, velocities and controllers")
{
    const MidiFile original{
        .tracks = {MidiFileTrack{.name = "Keys", .clip = phrase()},
                   MidiFileTrack{.name = "Bass",
                                 .clip = {.length = 2 * ticksPerBeat,
                                          .notes = {Note{.pitch = 36, .start = 0, .length = 2 * ticksPerBeat}}}}},
        .tempoMap = {.tempos = {TempoEvent{.position = 0, .bpm = 120.0},
                                TempoEvent{.position = 4 * ticksPerBeat, .bpm = 100.0}},
                     .timeSignatures = {TimeSignatureEvent{.position = 0, .numerator = 6, .denominator = 8}}},
    };

    const std::optional<MidiFile> imported = parseMidiFile(serializeMidiFile(original));

    REQUIRE(imported);
    CHECK(*imported == original);
}

TEST_CASE("a clip's position moves its events in the file")
{
    MidiClip clip{.position = 2 * ticksPerBeat,
                  .length = ticksPerBeat,
                  .notes = {Note{.pitch = 60, .start = 0, .length = ticksPerBeat}}};

    const std::optional<MidiFile> imported = parseMidiFile(serializeMidiFile({.tracks = {{.clip = clip}}}));

    REQUIRE(imported);
    REQUIRE(imported->tracks.size() == 1);
    CHECK(imported->tracks[0].clip.position == 0);
    CHECK(imported->tracks[0].clip.length == 3 * ticksPerBeat);
    CHECK(imported->tracks[0].clip.notes[0].start == 2 * ticksPerBeat);
}

TEST_CASE("files from other programs import with their resolution and running status")
{
    // 480 ticks per quarter. Note-on and controller messages share their status bytes, and a
    // note-on with velocity 0 ends a note.
    const std::vector<std::uint8_t> body = bytes({
        0x00, 0xFF, 0x03, 0x04, 'L',  'e',  'a',  'd', // track name
        0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,      // 120 BPM
        0x00, 0x93, 60,   100,                         // note-on on channel 4
        0x00, 64,   80,                                // running status
        0x83, 0x60, 60,   0,                           // 480 ticks later, velocity 0 ends 60
        0x00, 0xB3, 7,    90,                          // volume
        0x00, 10,   30,                                // running status: pan
        0x83, 0x60, 0x83, 64,   0,                     // note-off for 64
        0x00, 0xFF, 0x2F, 0x00,
    });

    const std::optional<MidiFile> file = parseMidiFile(smf(0, 480, body));

    REQUIRE(file);
    REQUIRE(file->tracks.size() == 1);
    const MidiFileTrack& track = file->tracks[0];
    CHECK(track.name == "Lead");
    CHECK(track.clip.length == 2 * ticksPerBeat);
    REQUIRE(track.clip.notes.size() == 2);
    CHECK(track.clip.notes[0] == Note{.pitch = 60, .velocity = 100, .start = 0, .length = ticksPerBeat});
    CHECK(track.clip.notes[1] == Note{.pitch = 64, .velocity = 80, .start = 0, .length = 2 * ticksPerBeat});
    REQUIRE(track.clip.controlChanges.size() == 2);
    CHECK(track.clip.controlChanges[0] == ControlChange{.position = ticksPerBeat, .controller = 7, .value = 90});
    CHECK(track.clip.controlChanges[1] == ControlChange{.position = ticksPerBeat, .controller = 10, .value = 30});
    CHECK(file->tempoMap == TempoMap{});
}

TEST_CASE("overlapping notes of one pitch end in the order they started")
{
    const std::vector<std::uint8_t> body = bytes({
        0x00, 0x90, 60,   100, // first starts
        0x60, 0x90, 60,   50,  // second starts 96 ticks later
        0x60, 0x80, 60,   0,   // first ends
        0x60, 0x80, 60,   0,   // second ends
        0x00, 0xFF, 0x2F, 0x00,
    });

    const std::optional<MidiFile> file = parseMidiFile(smf(0, ticksPerBeat, body));

    REQUIRE(file);
    const MidiClip& clip = file->tracks[0].clip;
    REQUIRE(clip.notes.size() == 2);
    CHECK(clip.notes[0] == Note{.pitch = 60, .velocity = 100, .start = 0, .length = 192});
    CHECK(clip.notes[1] == Note{.pitch = 60, .velocity = 50, .start = 96, .length = 192});
}

TEST_CASE("a note still held at the end of the track lasts until the end")
{
    const std::vector<std::uint8_t> body = bytes({0x00, 0x90, 60, 100, 0x87, 0x40, 0xFF, 0x2F, 0x00});

    const std::optional<MidiFile> file = parseMidiFile(smf(0, ticksPerBeat, body));

    REQUIRE(file);
    CHECK(file->tracks[0].clip.notes[0].length == ticksPerBeat);
}

TEST_CASE("a file without tempo events plays at 120 BPM in 4/4")
{
    const std::vector<std::uint8_t> body = bytes({0x00, 0x90, 60, 100, 0x10, 0x80, 60, 0, 0x00, 0xFF, 0x2F, 0x00});

    const std::optional<MidiFile> file = parseMidiFile(smf(1, ticksPerBeat, body));

    REQUIRE(file);
    CHECK(file->tempoMap == TempoMap{});
}

TEST_CASE("files the model can't hold are rejected")
{
    const std::vector<std::uint8_t> body = bytes({0x00, 0xFF, 0x2F, 0x00});

    CHECK_FALSE(parseMidiFile(smf(2, ticksPerBeat, body)));
    CHECK_FALSE(parseMidiFile(smf(1, 0xE728, body))); // SMPTE timing
    CHECK_FALSE(parseMidiFile(bytes({'R', 'I', 'F', 'F'})));
}

TEST_CASE("truncated files are rejected")
{
    const std::vector<std::uint8_t> file = serializeMidiFile({.tracks = {{.clip = phrase()}}});

    for (const std::size_t size : {std::size_t{0}, std::size_t{13}, std::size_t{30}, file.size() - 1})
    {
        CHECK_FALSE(parseMidiFile(std::span(file).first(size)));
    }
    CHECK_FALSE(parseMidiFile(smf(0, ticksPerBeat, bytes({0x00, 60, 100}))));    // data without a status
    CHECK_FALSE(parseMidiFile(smf(0, ticksPerBeat, bytes({0xFF, 0xFF, 0xFF})))); // endless delta time
}

TEST_CASE("midi files are written to and read from disk")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "sessionflow_midi_file_test.mid";
    const MidiFile original{.tracks = {{.name = "Keys", .clip = phrase()}}};

    REQUIRE(writeMidiFile(path, original));
    const std::optional<MidiFile> imported = readMidiFile(path);
    std::filesystem::remove(path);

    REQUIRE(imported);
    CHECK(*imported == original);
    CHECK_FALSE(readMidiFile(path));
}
