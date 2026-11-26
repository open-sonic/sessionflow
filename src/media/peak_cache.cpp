#include "media/peak_cache.hpp"

#include "core/audio_buffer.hpp"
#include "media/audio_file.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

namespace sessionflow
{

namespace
{

constexpr std::size_t levelCount = 5;
constexpr std::size_t levelFactor = 4;
// A multiple of finestSamplesPerPeak, so only the last block ends in a partial peak.
constexpr std::size_t readFrames = finestSamplesPerPeak * 256;

constexpr std::array<char, 4> magic{'S', 'F', 'P', 'K'};
constexpr std::uint32_t formatVersion = 1;
constexpr std::uint32_t maxChannels = 64;

static_assert(std::endian::native == std::endian::little, "peak files are written in native byte order");
static_assert(sizeof(Peak) == 4);

std::int16_t toPeakValue(float sample)
{
    return static_cast<std::int16_t>(std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
}

PeakLevel coarsen(const PeakLevel& finer, std::size_t channels)
{
    PeakLevel level;
    level.samplesPerPeak = finer.samplesPerPeak * levelFactor;
    level.peakCount = (finer.peakCount + levelFactor - 1) / levelFactor;
    level.peaks.reserve(level.peakCount * channels);
    for (std::size_t index = 0; index < level.peakCount; ++index)
    {
        const std::size_t first = index * levelFactor;
        const std::size_t last = std::min(first + levelFactor, finer.peakCount);
        for (std::size_t channel = 0; channel < channels; ++channel)
        {
            Peak merged = finer.peaks[first * channels + channel];
            for (std::size_t source = first + 1; source < last; ++source)
            {
                const Peak& peak = finer.peaks[source * channels + channel];
                merged.minimum = std::min(merged.minimum, peak.minimum);
                merged.maximum = std::max(merged.maximum, peak.maximum);
            }
            level.peaks.push_back(merged);
        }
    }
    return level;
}

struct SourceStamp
{
    std::uint64_t size = 0;
    std::int64_t modified = 0;

    bool operator==(const SourceStamp&) const = default;
};

std::optional<SourceStamp> stampOf(const std::filesystem::path& source)
{
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(source, error);
    if (error)
    {
        return std::nullopt;
    }
    const std::filesystem::file_time_type modified = std::filesystem::last_write_time(source, error);
    if (error)
    {
        return std::nullopt;
    }
    return SourceStamp{size, static_cast<std::int64_t>(modified.time_since_epoch().count())};
}

template <typename T> void writeValue(std::ofstream& file, const T& value)
{
    file.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T> bool readValue(std::ifstream& file, T& value)
{
    file.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(file);
}

} // namespace

const PeakLevel& WaveformPeaks::levelFor(double samplesPerPixel) const noexcept
{
    for (auto level = levels.rbegin(); level != levels.rend(); ++level)
    {
        if (static_cast<double>(level->samplesPerPeak) <= samplesPerPixel)
        {
            return *level;
        }
    }
    return levels.front();
}

std::optional<WaveformPeaks> buildWaveformPeaks(const std::filesystem::path& source, std::stop_token stop)
{
    std::optional<AudioFileReader> reader = AudioFileReader::open(source);
    if (!reader)
    {
        return std::nullopt;
    }

    WaveformPeaks result;
    result.channelCount = reader->channelCount();
    result.sampleRate = reader->sampleRate();

    PeakLevel finest;
    finest.samplesPerPeak = finestSamplesPerPeak;
    const auto expectedPeaks =
        (static_cast<std::size_t>(reader->frameCount()) + finestSamplesPerPeak - 1) / finestSamplesPerPeak;
    finest.peaks.reserve(expectedPeaks * result.channelCount);

    AudioBuffer block(result.channelCount, readFrames);
    while (true)
    {
        if (stop.stop_requested())
        {
            return std::nullopt;
        }
        const std::size_t framesRead = reader->read(block.view());
        for (std::size_t start = 0; start < framesRead; start += finestSamplesPerPeak)
        {
            const std::size_t count = std::min(finestSamplesPerPeak, framesRead - start);
            for (std::size_t channel = 0; channel < result.channelCount; ++channel)
            {
                const auto [lowest, highest] = std::ranges::minmax(block.view().channel(channel).subspan(start, count));
                finest.peaks.push_back({toPeakValue(lowest), toPeakValue(highest)});
            }
        }
        // A truncated file ends before its header says, so count what was actually read.
        result.frameCount += static_cast<SampleCount>(framesRead);
        if (framesRead < readFrames)
        {
            break;
        }
    }
    finest.peakCount = finest.peaks.size() / result.channelCount;

    result.levels.push_back(std::move(finest));
    while (result.levels.size() < levelCount)
    {
        result.levels.push_back(coarsen(result.levels.back(), result.channelCount));
    }
    return result;
}

bool savePeakFile(const std::filesystem::path& peakFile, const std::filesystem::path& source,
                  const WaveformPeaks& peaks)
{
    const std::optional<SourceStamp> stamp = stampOf(source);
    if (!stamp)
    {
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(peakFile.parent_path(), error);

    // Written aside and renamed, so a crash never leaves a half-written file under the real name.
    std::filesystem::path temporary = peakFile;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(magic.data(), magic.size());
        writeValue(file, formatVersion);
        writeValue(file, stamp->size);
        writeValue(file, stamp->modified);
        writeValue(file, static_cast<std::uint32_t>(peaks.channelCount));
        writeValue(file, peaks.sampleRate);
        writeValue(file, peaks.frameCount);
        writeValue(file, static_cast<std::uint32_t>(peaks.levels.size()));
        for (const PeakLevel& level : peaks.levels)
        {
            writeValue(file, static_cast<std::uint32_t>(level.samplesPerPeak));
            writeValue(file, static_cast<std::uint64_t>(level.peakCount));
            file.write(reinterpret_cast<const char*>(level.peaks.data()),
                       static_cast<std::streamsize>(level.peaks.size() * sizeof(Peak)));
        }
        if (!file.flush())
        {
            file.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
    }

    std::filesystem::rename(temporary, peakFile, error);
    if (error)
    {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

std::optional<WaveformPeaks> loadPeakFile(const std::filesystem::path& peakFile, const std::filesystem::path& source)
{
    const std::optional<SourceStamp> stamp = stampOf(source);
    if (!stamp)
    {
        return std::nullopt;
    }

    std::ifstream file(peakFile, std::ios::binary | std::ios::ate);
    if (!file)
    {
        return std::nullopt;
    }
    const auto fileSize = static_cast<std::uint64_t>(file.tellg());
    file.seekg(0);

    std::array<char, 4> fileMagic{};
    std::uint32_t version = 0;
    SourceStamp fileStamp;
    std::uint32_t channels = 0;
    std::uint32_t levels = 0;
    WaveformPeaks peaks;
    if (!readValue(file, fileMagic) || fileMagic != magic || !readValue(file, version) || version != formatVersion ||
        !readValue(file, fileStamp.size) || !readValue(file, fileStamp.modified) || fileStamp != *stamp ||
        !readValue(file, channels) || channels == 0 || channels > maxChannels || !readValue(file, peaks.sampleRate) ||
        !readValue(file, peaks.frameCount) || !readValue(file, levels) || levels == 0)
    {
        return std::nullopt;
    }
    peaks.channelCount = channels;

    for (std::uint32_t index = 0; index < levels; ++index)
    {
        std::uint32_t samplesPerPeak = 0;
        std::uint64_t peakCount = 0;
        if (!readValue(file, samplesPerPeak) || samplesPerPeak == 0 || !readValue(file, peakCount))
        {
            return std::nullopt;
        }
        // Checked before allocating, so a damaged count can't ask for gigabytes.
        const auto remaining = fileSize - static_cast<std::uint64_t>(file.tellg());
        if (peakCount > remaining / (channels * sizeof(Peak)))
        {
            return std::nullopt;
        }

        PeakLevel& level = peaks.levels.emplace_back();
        level.samplesPerPeak = samplesPerPeak;
        level.peakCount = static_cast<std::size_t>(peakCount);
        level.peaks.resize(level.peakCount * channels);
        file.read(reinterpret_cast<char*>(level.peaks.data()),
                  static_cast<std::streamsize>(level.peaks.size() * sizeof(Peak)));
        if (!file)
        {
            return std::nullopt;
        }
    }
    return peaks;
}

std::filesystem::path peakFilePath(const std::filesystem::path& cacheDirectory, const std::filesystem::path& source)
{
    std::error_code error;
    std::filesystem::path absolute = std::filesystem::absolute(source, error);
    if (error)
    {
        absolute = source;
    }

    // FNV-1a instead of std::hash, which differs between standard libraries and releases.
    std::uint64_t hash = 0xcbf29ce484222325;
    for (const char8_t byte : absolute.lexically_normal().generic_u8string())
    {
        hash ^= static_cast<std::uint8_t>(byte);
        hash *= 0x100000001b3;
    }

    std::array<char, 17> hex{};
    std::snprintf(hex.data(), hex.size(), "%016llx", static_cast<unsigned long long>(hash));

    std::filesystem::path name = source.stem();
    name += "-";
    name += hex.data();
    name += ".peaks";
    return cacheDirectory / name;
}

PeakCache::PeakCache(std::filesystem::path cacheDirectory)
    : m_cacheDirectory(std::move(cacheDirectory)), m_worker([this](std::stop_token stop) { run(stop); })
{
}

PeakCache::~PeakCache() = default;

std::shared_ptr<const WaveformPeaks> PeakCache::find(const std::filesystem::path& source)
{
    const std::scoped_lock lock(m_mutex);
    const auto [entry, inserted] = m_entries.try_emplace(source);
    if (inserted)
    {
        m_queue.push_back(source);
        m_wake.notify_one();
    }
    return entry->second;
}

void PeakCache::run(std::stop_token stop)
{
    while (true)
    {
        std::filesystem::path source;
        {
            std::unique_lock lock(m_mutex);
            if (!m_wake.wait(lock, stop, [this] { return !m_queue.empty(); }))
            {
                return;
            }
            source = std::move(m_queue.front());
            m_queue.pop_front();
        }

        const std::filesystem::path peakFile = peakFilePath(m_cacheDirectory, source);
        std::optional<WaveformPeaks> peaks = loadPeakFile(peakFile, source);
        if (!peaks)
        {
            peaks = buildWaveformPeaks(source, stop);
            if (peaks)
            {
                savePeakFile(peakFile, source, *peaks);
            }
        }
        if (!peaks)
        {
            continue;
        }

        auto shared = std::make_shared<const WaveformPeaks>(std::move(*peaks));
        const std::scoped_lock lock(m_mutex);
        m_entries[source] = std::move(shared);
    }
}

} // namespace sessionflow
