#include "media/resampler.hpp"
#include "support/signals.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>

using namespace sessionflow;

namespace
{

using Signal = std::function<double(double seconds)>;

// Pulls the resampler the way a clip player does: refill only when a block can't be served.
AudioBuffer resample(const Signal& signal, double sourceRate, double targetRate, double seconds, std::size_t readFrames)
{
    const auto sourceFrames = static_cast<std::size_t>(seconds * sourceRate);
    const auto targetFrames = static_cast<std::size_t>(seconds * targetRate);
    Resampler resampler(1, sourceRate, targetRate, readFrames);
    AudioBuffer output(1, targetFrames);

    std::size_t sourcePosition = 0;
    std::size_t written = 0;
    while (written < targetFrames)
    {
        const std::size_t want = std::min(readFrames, targetFrames - written);
        while (resampler.availableFrames() < want)
        {
            const std::span<float> block = resampler.inputBlock().channel(0);
            for (float& sample : block)
            {
                const bool inside = sourcePosition < sourceFrames;
                sample = inside ? static_cast<float>(signal(static_cast<double>(sourcePosition) / sourceRate)) : 0.0f;
                ++sourcePosition;
            }
            resampler.write(block.size());
        }
        written += resampler.read(output.view().slice(written, want));
    }
    return output;
}

Signal sine(double frequency, double amplitude)
{
    return [=](double t) { return amplitude * std::sin(2.0 * std::numbers::pi * frequency * t); };
}

// Linear sweep, so the instantaneous frequency maps straight to a time window.
constexpr double sweepStart = 20.0;
constexpr double sweepEnd = 24000.0;
constexpr double sweepSeconds = 2.0;

double sweepTimeAt(double frequency)
{
    return (frequency - sweepStart) / (sweepEnd - sweepStart) * sweepSeconds;
}

double sweep(double t)
{
    const double rate = (sweepEnd - sweepStart) / sweepSeconds;
    return 0.5 * std::sin(2.0 * std::numbers::pi * (sweepStart * t + 0.5 * rate * t * t));
}

float peakBetween(AudioBufferView audio, double sampleRate, double from, double to)
{
    const auto start = static_cast<std::size_t>(from * sampleRate);
    const auto end = static_cast<std::size_t>(to * sampleRate);
    return test::peak(audio.slice(start, end - start));
}

} // namespace

TEST_CASE("resampled sine keeps frequency, phase and level")
{
    const double sourceRate = GENERATE(44100.0, 48000.0);
    const double targetRate = sourceRate == 44100.0 ? 48000.0 : 44100.0;
    AudioBuffer output = resample(sine(1000.0, 0.5), sourceRate, targetRate, 0.2, 512);

    // Skip the filter's run-in at the start, where the source began from silence.
    const AudioBufferView samples = output.view();
    for (std::size_t frame = 2000; frame < samples.frameCount() - 2000; ++frame)
    {
        const double expected = sine(1000.0, 0.5)(static_cast<double>(frame) / targetRate);
        REQUIRE(samples.channel(0)[frame] == Catch::Approx(expected).margin(1e-4));
    }
}

TEST_CASE("sweep down to 44.1 kHz keeps the passband and doesn't alias")
{
    AudioBuffer output = resample(sweep, 48000.0, 44100.0, sweepSeconds, 512);
    const AudioBufferView samples = output.view();

    CHECK(peakBetween(samples, 44100.0, sweepTimeAt(100.0), sweepTimeAt(20000.0)) == Catch::Approx(0.5f).margin(0.005));

    // Above the new Nyquist of 22.05 kHz plus the transition band, anything left is aliasing.
    // The sweep's abrupt end smears across the filter length, so stop short of it.
    const float aliasing = peakBetween(samples, 44100.0, sweepTimeAt(22600.0), sweepSeconds - 0.02);
    CHECK(aliasing < 1e-5f);
}

TEST_CASE("read size doesn't change the result")
{
    AudioBuffer small = resample(sweep, 48000.0, 44100.0, 0.5, 37);
    AudioBuffer large = resample(sweep, 48000.0, 44100.0, 0.5, 1024);
    REQUIRE(small.frameCount() == large.frameCount());
    CHECK(std::ranges::equal(small.view().channel(0), large.view().channel(0)));
}

TEST_CASE("equal rates pass audio through")
{
    AudioBuffer output = resample(sine(440.0, 0.5), 48000.0, 48000.0, 0.05, 128);
    const AudioBufferView samples = output.view();
    for (std::size_t frame = 0; frame < samples.frameCount(); ++frame)
    {
        REQUIRE(samples.channel(0)[frame] ==
                Catch::Approx(sine(440.0, 0.5)(static_cast<double>(frame) / 48000.0)).margin(1e-6));
    }
}

TEST_CASE("reset drops buffered audio and filter state")
{
    Resampler resampler(2, 44100.0, 48000.0, 512);
    test::fillConstant(resampler.inputBlock(), 1.0f);
    const auto blocksUntilOutput = [&resampler]
    {
        int blocks = 0;
        while (resampler.availableFrames() == 0 && blocks < 1000)
        {
            resampler.write(Resampler::inputBlockFrames);
            ++blocks;
        }
        return blocks;
    };

    const int firstRun = blocksUntilOutput();
    REQUIRE(resampler.availableFrames() > 0);

    resampler.reset();
    CHECK(resampler.availableFrames() == 0);
    // Same filter run-in again, so no old state survived.
    CHECK(blocksUntilOutput() == firstRun);
}
