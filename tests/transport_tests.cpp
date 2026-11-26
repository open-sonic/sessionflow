#include "engine/transport.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <random>
#include <vector>

using namespace sessionflow;

namespace
{

constexpr TickCount s_bar = 4 * ticksPerBeat;
constexpr std::size_t s_maxBlock = 512;

std::unique_ptr<const TimeMap> timeMapAt(double bpm, double sampleRate)
{
    return std::make_unique<const TimeMap>(TempoMap{.tempos = {TempoEvent{.position = 0, .bpm = bpm}}}, sampleRate);
}

std::unique_ptr<Transport> makeTransport(double bpm = 120.0, double sampleRate = 48000.0)
{
    auto transport = std::make_unique<Transport>(timeMapAt(bpm, sampleRate));
    transport->prepare(s_maxBlock);
    return transport;
}

// The timeline sample each frame of the block plays.
void appendFramePositions(std::span<const TransportSegment> segments, std::vector<SampleCount>& positions)
{
    for (const TransportSegment& segment : segments)
    {
        for (std::size_t frame = 0; frame < segment.frameCount; ++frame)
        {
            positions.push_back(segment.position + static_cast<SampleCount>(frame));
        }
    }
}

} // namespace

TEST_CASE("a stopped transport plays no segments and keeps its position")
{
    const auto transport = makeTransport();

    CHECK(transport->advance(256).empty());
    CHECK(transport->position() == 0);
    CHECK_FALSE(transport->isPlaying());
}

TEST_CASE("playing advances the playhead by the block size")
{
    const auto transport = makeTransport();
    REQUIRE(transport->play());

    const std::span<const TransportSegment> first = transport->advance(64);
    const std::span<const TransportSegment> second = transport->advance(64);

    REQUIRE(second.size() == 1);
    CHECK(second[0] == TransportSegment{.offset = 0, .frameCount = 64, .position = 64});
    CHECK(transport->position() == 128);
    CHECK(transport->displayPosition() == 128);
    CHECK(transport->isPlayingForDisplay());
    CHECK(first.size() == 1);
}

TEST_CASE("stop holds the playhead where it is")
{
    const auto transport = makeTransport();
    REQUIRE(transport->play());
    transport->advance(100);

    REQUIRE(transport->stop());

    CHECK(transport->advance(100).empty());
    CHECK(transport->position() == 100);
}

TEST_CASE("locate moves the playhead to the sample of the tick")
{
    const auto transport = makeTransport();
    REQUIRE(transport->play());
    REQUIRE(transport->locate(ticksPerBeat));

    const std::span<const TransportSegment> segments = transport->advance(32);

    REQUIRE(segments.size() == 1);
    CHECK(segments[0].position == 24000); // one beat at 120 bpm and 48 kHz
}

TEST_CASE("a loop wraps on its exact end sample for any block sizes")
{
    // At 133 bpm and 44.1 kHz a bar ends between samples, so rounding mistakes show up.
    const auto transport = makeTransport(133.0, 44100.0);
    const auto loopEnd = static_cast<SampleCount>(std::ceil(4.0 * 60.0 / 133.0 * 44100.0));
    REQUIRE(transport->setLoop({.start = 0, .end = s_bar}));
    REQUIRE(transport->setLoopEnabled(true));
    REQUIRE(transport->play());

    std::mt19937 random(7);
    std::uniform_int_distribution<std::size_t> blockSize(1, s_maxBlock);
    std::vector<SampleCount> positions;
    while (positions.size() < static_cast<std::size_t>(5 * loopEnd))
    {
        appendFramePositions(transport->advance(blockSize(random)), positions);
    }

    for (std::size_t frame = 0; frame < positions.size(); ++frame)
    {
        REQUIRE(positions[frame] == static_cast<SampleCount>(frame) % loopEnd);
    }
}

TEST_CASE("a loop shorter than the block wraps several times in one block")
{
    // A 32-tick loop at 120 bpm and 48 kHz is 800 samples.
    const auto transport = makeTransport();
    REQUIRE(transport->setLoop({.start = ticksPerBeat, .end = ticksPerBeat + 32}));
    REQUIRE(transport->setLoopEnabled(true));
    REQUIRE(transport->locate(ticksPerBeat));
    REQUIRE(transport->play());
    transport->prepare(2000);

    const std::span<const TransportSegment> segments = transport->advance(2000);

    REQUIRE(segments.size() == 3);
    CHECK(segments[0] == TransportSegment{.offset = 0, .frameCount = 800, .position = 24000});
    CHECK(segments[1] == TransportSegment{.offset = 800, .frameCount = 800, .position = 24000});
    CHECK(segments[2] == TransportSegment{.offset = 1600, .frameCount = 400, .position = 24000});
}

TEST_CASE("a playhead before the loop plays into it and wraps")
{
    const auto transport = makeTransport();
    REQUIRE(transport->setLoop({.start = ticksPerBeat, .end = 2 * ticksPerBeat}));
    REQUIRE(transport->setLoopEnabled(true));
    REQUIRE(transport->locate(ticksPerBeat - 10)); // 250 samples before the loop
    REQUIRE(transport->play());

    std::vector<SampleCount> positions;
    for (int block = 0; block < 400; ++block)
    {
        appendFramePositions(transport->advance(100), positions);
    }

    CHECK(positions.front() == 23750);
    CHECK(positions[250 + 24000 - 1] == 47999);
    CHECK(positions[250 + 24000] == 24000);
}

TEST_CASE("a playhead after the loop end plays on without wrapping")
{
    const auto transport = makeTransport();
    REQUIRE(transport->setLoop({.start = 0, .end = ticksPerBeat}));
    REQUIRE(transport->setLoopEnabled(true));
    REQUIRE(transport->locate(2 * ticksPerBeat));
    REQUIRE(transport->play());

    for (int block = 0; block < 10; ++block)
    {
        transport->advance(500);
    }

    CHECK(transport->position() == 48000 + 5000);
}

TEST_CASE("a disabled or empty loop does not wrap")
{
    const auto transport = makeTransport();
    REQUIRE(transport->setLoop({.start = 0, .end = 32}));
    REQUIRE(transport->play());
    transport->advance(512);
    transport->advance(512);
    CHECK(transport->position() == 1024);

    REQUIRE(transport->setLoop({.start = 64, .end = 64}));
    REQUIRE(transport->setLoopEnabled(true));
    transport->advance(512);
    CHECK(transport->position() == 1536);
}

TEST_CASE("a tempo change keeps the playhead on its musical position")
{
    const auto transport = makeTransport(120.0);
    REQUIRE(transport->play());
    for (int block = 0; block < 72; ++block)
    {
        transport->advance(500);
    }
    REQUIRE(transport->position() == 36000); // a beat and a half at 120 bpm

    REQUIRE(transport->setTimeMap(timeMapAt(60.0, 48000.0)));
    transport->advance(0);

    CHECK(transport->position() == 72000); // a beat and a half at 60 bpm
    CHECK(transport->timeMap().tempoAt(0.0) == 60.0);
}

TEST_CASE("a tempo change moves the loop end with its bar")
{
    const auto transport = makeTransport(120.0);
    REQUIRE(transport->setLoop({.start = 0, .end = s_bar}));
    REQUIRE(transport->setLoopEnabled(true));
    REQUIRE(transport->play());
    REQUIRE(transport->setTimeMap(timeMapAt(60.0, 48000.0)));

    std::vector<SampleCount> positions;
    while (positions.size() < 200000)
    {
        appendFramePositions(transport->advance(512), positions);
    }

    // A bar at 60 bpm is 192000 samples instead of 96000.
    CHECK(positions[191999] == 191999);
    CHECK(positions[192000] == 0);
}

TEST_CASE("time maps wait for the audio thread to hand back the ones they replace")
{
    const auto transport = makeTransport();
    int accepted = 0;
    while (accepted < 100 && transport->setTimeMap(timeMapAt(100.0 + accepted, 48000.0)))
    {
        ++accepted;
    }
    CHECK(accepted == 32);

    transport->advance(64);
    transport->collect();

    CHECK(transport->setTimeMap(timeMapAt(90.0, 48000.0)));
    transport->advance(64);
    CHECK(transport->timeMap().tempoAt(0.0) == 90.0);
}

TEST_CASE("processors see the tempo and position of their segment")
{
    const auto transport = makeTransport(140.0);
    REQUIRE(transport->play());

    const std::span<const TransportSegment> segments = transport->advance(64);

    REQUIRE(segments.size() == 1);
    const TransportState state = transport->stateAt(segments[0]);
    CHECK(state.position == 0);
    CHECK(state.tempo == 140.0);
    CHECK(state.playing);
}
