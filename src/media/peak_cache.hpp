#pragma once

#include "core/time.hpp"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

namespace sessionflow
{

// Minimum and maximum of one channel over samplesPerPeak frames, scaled so 32767 is full scale.
// Samples beyond full scale are clamped.
struct Peak
{
    std::int16_t minimum = 0;
    std::int16_t maximum = 0;

    bool operator==(const Peak&) const = default;
};

struct PeakLevel
{
    std::size_t samplesPerPeak = 0;
    std::size_t peakCount = 0;
    // Peak-major, channel-minor.
    std::vector<Peak> peaks;
};

struct WaveformPeaks
{
    std::size_t channelCount = 0;
    double sampleRate = 0.0;
    SampleCount frameCount = 0;
    // Finest first, each level four times coarser than the one before.
    std::vector<PeakLevel> levels;

    Peak peak(const PeakLevel& level, std::size_t index, std::size_t channel) const noexcept
    {
        return level.peaks[index * channelCount + channel];
    }

    // The coarsest level that still has at least one peak per pixel. Zoomed in further than
    // the finest level, draw from the samples instead.
    const PeakLevel& levelFor(double samplesPerPixel) const noexcept;
};

inline constexpr std::size_t finestSamplesPerPeak = 256;

// Decodes the whole file. Blocking, run it on a worker. Returns nothing for unreadable files
// and when stopped.
std::optional<WaveformPeaks> buildWaveformPeaks(const std::filesystem::path& source, std::stop_token stop = {});

// The cache file records the source's size and modification time and is only read back while
// both still match.
bool savePeakFile(const std::filesystem::path& peakFile, const std::filesystem::path& source,
                  const WaveformPeaks& peaks);
std::optional<WaveformPeaks> loadPeakFile(const std::filesystem::path& peakFile, const std::filesystem::path& source);

// Unique per source path, so two sources with the same name don't share a cache file.
std::filesystem::path peakFilePath(const std::filesystem::path& cacheDirectory, const std::filesystem::path& source);

// Loads peaks from cacheDirectory, or builds and saves them, on one worker thread. For the UI
// thread: find() never waits for the worker.
class PeakCache
{
  public:
    explicit PeakCache(std::filesystem::path cacheDirectory);
    ~PeakCache();

    PeakCache(const PeakCache&) = delete;
    PeakCache& operator=(const PeakCache&) = delete;

    // Returns null until the peaks are ready. The first call for a source queues it. Sources
    // that fail to decode stay null.
    std::shared_ptr<const WaveformPeaks> find(const std::filesystem::path& source);

  private:
    void run(std::stop_token stop);

    std::filesystem::path m_cacheDirectory;
    std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::map<std::filesystem::path, std::shared_ptr<const WaveformPeaks>> m_entries;
    std::deque<std::filesystem::path> m_queue;
    // Last member, so the worker stops before the state it uses is destroyed.
    std::jthread m_worker;
};

} // namespace sessionflow
