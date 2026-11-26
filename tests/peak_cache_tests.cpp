#include "media/peak_cache.hpp"

#include "media/audio_file.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>

using namespace sessionflow;

namespace
{

const std::filesystem::path testData = SESSIONFLOW_TEST_DATA_DIR;

// One per test, so ctest can run them in parallel.
struct TemporaryDirectory
{
    std::filesystem::path path;

    explicit TemporaryDirectory(const std::string& name)
        : path(std::filesystem::temp_directory_path() / ("sessionflow_peak_tests_" + name))
    {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }

    ~TemporaryDirectory()
    {
        std::filesystem::remove_all(path);
    }
};

std::int16_t expectedPeakValue(float sample)
{
    return static_cast<std::int16_t>(std::lround(sample * 32767.0f));
}

void requireSamePeaks(const WaveformPeaks& actual, const WaveformPeaks& expected)
{
    REQUIRE(actual.channelCount == expected.channelCount);
    REQUIRE(actual.sampleRate == expected.sampleRate);
    REQUIRE(actual.frameCount == expected.frameCount);
    REQUIRE(actual.levels.size() == expected.levels.size());
    for (std::size_t index = 0; index < actual.levels.size(); ++index)
    {
        REQUIRE(actual.levels[index].samplesPerPeak == expected.levels[index].samplesPerPeak);
        REQUIRE(actual.levels[index].peakCount == expected.levels[index].peakCount);
        REQUIRE(actual.levels[index].peaks == expected.levels[index].peaks);
    }
}

} // namespace

TEST_CASE("finest level holds the minimum and maximum of every 256 frames")
{
    std::optional<DecodedAudio> decoded = decodeAudioFile(testData / "sine_24bit.flac");
    std::optional<WaveformPeaks> peaks = buildWaveformPeaks(testData / "sine_24bit.flac");
    REQUIRE(decoded.has_value());
    REQUIRE(peaks.has_value());

    CHECK(peaks->channelCount == 2);
    CHECK(peaks->sampleRate == 44100.0);
    CHECK(peaks->frameCount == 4410);

    const PeakLevel& finest = peaks->levels.front();
    REQUIRE(finest.samplesPerPeak == 256);
    // 4410 frames end in a partial peak.
    REQUIRE(finest.peakCount == 18);
    for (std::size_t index = 0; index < finest.peakCount; ++index)
    {
        for (std::size_t channel = 0; channel < 2; ++channel)
        {
            const std::size_t start = index * 256;
            const std::size_t count = std::min<std::size_t>(256, 4410 - start);
            const auto [lowest, highest] =
                std::ranges::minmax(decoded->samples.view().channel(channel).subspan(start, count));
            const Peak peak = peaks->peak(finest, index, channel);
            REQUIRE(peak.minimum == expectedPeakValue(lowest));
            REQUIRE(peak.maximum == expectedPeakValue(highest));
        }
    }
}

TEST_CASE("each coarser level merges four peaks of the level before")
{
    std::optional<WaveformPeaks> peaks = buildWaveformPeaks(testData / "sine.mp3");
    REQUIRE(peaks.has_value());
    REQUIRE(peaks->levels.size() == 5);

    for (std::size_t index = 1; index < peaks->levels.size(); ++index)
    {
        const PeakLevel& finer = peaks->levels[index - 1];
        const PeakLevel& level = peaks->levels[index];
        REQUIRE(level.samplesPerPeak == finer.samplesPerPeak * 4);
        REQUIRE(level.peakCount == (finer.peakCount + 3) / 4);
        for (std::size_t peak = 0; peak < level.peakCount; ++peak)
        {
            for (std::size_t channel = 0; channel < 2; ++channel)
            {
                Peak expected = peaks->peak(finer, peak * 4, channel);
                for (std::size_t source = peak * 4 + 1; source < std::min(peak * 4 + 4, finer.peakCount); ++source)
                {
                    expected.minimum = std::min(expected.minimum, peaks->peak(finer, source, channel).minimum);
                    expected.maximum = std::max(expected.maximum, peaks->peak(finer, source, channel).maximum);
                }
                REQUIRE(peaks->peak(level, peak, channel) == expected);
            }
        }
    }
}

TEST_CASE("level for a zoom is the coarsest with at least one peak per pixel")
{
    std::optional<WaveformPeaks> peaks = buildWaveformPeaks(testData / "sine.mp3");
    REQUIRE(peaks.has_value());

    CHECK(peaks->levelFor(10.0).samplesPerPeak == 256);
    CHECK(peaks->levelFor(256.0).samplesPerPeak == 256);
    CHECK(peaks->levelFor(1500.0).samplesPerPeak == 1024);
    CHECK(peaks->levelFor(1e9).samplesPerPeak == 65536);
}

TEST_CASE("peak file loads back until the source changes")
{
    TemporaryDirectory directory("reload");
    const std::filesystem::path source = directory.path / "clip.flac";
    std::filesystem::copy_file(testData / "sine_24bit.flac", source);
    const std::filesystem::path peakFile = peakFilePath(directory.path / "peaks", source);

    const std::optional<SourceStamp> stamp = stampSource(source);
    REQUIRE(stamp.has_value());
    std::optional<WaveformPeaks> built = buildWaveformPeaks(source);
    REQUIRE(built.has_value());
    REQUIRE(savePeakFile(peakFile, *stamp, *built));

    std::optional<WaveformPeaks> loaded = loadPeakFile(peakFile, *stampSource(source));
    REQUIRE(loaded.has_value());
    requireSamePeaks(*loaded, *built);

    std::filesystem::copy_file(testData / "sine.mp3", source, std::filesystem::copy_options::overwrite_existing);
    const std::optional<SourceStamp> changed = stampSource(source);
    REQUIRE(changed.has_value());
    CHECK_FALSE(loadPeakFile(peakFile, *changed).has_value());
}

TEST_CASE("truncated peak files are rejected")
{
    TemporaryDirectory directory("truncated");
    const std::filesystem::path source = testData / "sine_24bit.flac";
    const std::filesystem::path peakFile = peakFilePath(directory.path, source);
    const std::optional<SourceStamp> stamp = stampSource(source);
    std::optional<WaveformPeaks> built = buildWaveformPeaks(source);
    REQUIRE(stamp.has_value());
    REQUIRE(built.has_value());
    REQUIRE(savePeakFile(peakFile, *stamp, *built));

    std::filesystem::resize_file(peakFile, std::filesystem::file_size(peakFile) - 1);
    CHECK_FALSE(loadPeakFile(peakFile, *stamp).has_value());
}

TEST_CASE("sources with the same name get different peak files")
{
    const std::filesystem::path cache = "cache";
    const std::filesystem::path first = peakFilePath(cache, "/a/kick.wav");
    const std::filesystem::path second = peakFilePath(cache, "/b/kick.wav");

    CHECK(first != second);
    CHECK(first == peakFilePath(cache, "/a/kick.wav"));
    CHECK(first.parent_path() == cache);
    CHECK(first.filename().string().starts_with("kick-"));
}

TEST_CASE("peak cache builds in the background and reuses the file next time")
{
    TemporaryDirectory directory("background");
    const std::filesystem::path source = testData / "sine.mp3";
    const auto waitForPeaks = [&](PeakCache& cache)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        std::shared_ptr<const WaveformPeaks> peaks = cache.find(source);
        while (!peaks && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            peaks = cache.find(source);
        }
        return peaks;
    };

    std::shared_ptr<const WaveformPeaks> built;
    {
        PeakCache cache(directory.path);
        built = waitForPeaks(cache);
        REQUIRE(built);
        CHECK(cache.find(source) == built);
    }

    const std::filesystem::path peakFile = peakFilePath(directory.path, source);
    REQUIRE(std::filesystem::exists(peakFile));
    const std::filesystem::file_time_type written = std::filesystem::last_write_time(peakFile);

    PeakCache cache(directory.path);
    std::shared_ptr<const WaveformPeaks> loaded = waitForPeaks(cache);
    REQUIRE(loaded);
    requireSamePeaks(*loaded, *built);
    // libc++ keeps file times in __int128, which Catch can't print, so compare outside the macro.
    const bool reused = std::filesystem::last_write_time(peakFile) == written;
    CHECK(reused);
}

TEST_CASE("peak cache stops while a source is still queued")
{
    TemporaryDirectory directory("stop");
    PeakCache cache(directory.path);
    CHECK_FALSE(cache.find(testData / "missing.wav"));
    CHECK_FALSE(cache.find(testData / "sine.mp3"));
}
