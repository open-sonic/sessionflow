#include "devices/gain.hpp"
#include "support/signals.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace sessionflow;

namespace
{

void processBlock(Processor& processor, AudioBufferView audio)
{
    ProcessBlock block;
    block.audio = audio;
    processor.process(block);
}

} // namespace

TEST_CASE("gain device scales audio after prepare")
{
    GainDevice gain;
    gain.setGainDecibels(-6.0f);
    gain.prepare({.sampleRate = 48000.0, .maxBlockSize = 64});

    AudioBuffer buffer(2, 64);
    test::fillConstant(buffer.view(), 1.0f);
    processBlock(gain, buffer.view());

    CHECK(test::peak(buffer.view()) == Catch::Approx(0.501f).margin(0.001));
}

TEST_CASE("gain changes ramp instead of jumping")
{
    GainDevice gain;
    gain.prepare({.sampleRate = 1000.0, .maxBlockSize = 64});
    gain.setGainDecibels(-120.0f);

    AudioBuffer buffer(1, 64);
    test::fillConstant(buffer.view(), 1.0f);
    processBlock(gain, buffer.view());

    const std::span<float> samples = buffer.view().channel(0);
    CHECK(samples[0] < 1.0f);
    CHECK(samples[0] > 0.9f);
    CHECK(samples[19] < 0.001f);
}

TEST_CASE("gain device reports no latency")
{
    CHECK(GainDevice().latency() == 0);
}
