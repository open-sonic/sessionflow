#include "engine/time_map.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace sessionflow;

namespace
{

constexpr TickCount s_bar = 4 * ticksPerBeat;

TempoMap tempoMap(std::initializer_list<TempoEvent> tempos,
                  std::initializer_list<TimeSignatureEvent> timeSignatures = {TimeSignatureEvent{}})
{
    return {.tempos = tempos, .timeSignatures = timeSignatures};
}

} // namespace

TEST_CASE("at 120 bpm and 48 kHz a beat lasts 24000 samples")
{
    const TimeMap map(TempoMap{}, 48000.0);

    CHECK(map.samplesAt(ticksPerBeat) == 24000.0);
    CHECK(map.sampleAt(ticksPerBeat) == 24000);
    CHECK(map.ticksAt(24000.0) == Catch::Approx(ticksPerBeat));
}

TEST_CASE("positions after tempo changes add up each segment")
{
    // Two bars at 120 bpm are 4 s, two bars at 90 bpm are 16/3 s.
    const TimeMap map(tempoMap({{.position = 0, .bpm = 120.0},
                                {.position = 2 * s_bar, .bpm = 90.0},
                                {.position = 4 * s_bar, .bpm = 150.0}}),
                      48000.0);

    CHECK(map.samplesAt(2.0 * s_bar) == Catch::Approx(4.0 * 48000.0));
    CHECK(map.samplesAt(4.0 * s_bar) == Catch::Approx((4.0 + 16.0 / 3.0) * 48000.0));
    // One more bar at 150 bpm is 1.6 s.
    CHECK(map.samplesAt(5.0 * s_bar) == Catch::Approx((4.0 + 16.0 / 3.0 + 1.6) * 48000.0));
    CHECK(map.tempoAt(2.0 * s_bar - 1.0) == 120.0);
    CHECK(map.tempoAt(2.0 * s_bar) == 90.0);
}

TEST_CASE("samples and ticks convert back and forth across tempo changes")
{
    const TimeMap map(tempoMap({{.position = 0, .bpm = 120.0}, {.position = 3000, .bpm = 77.7}}), 44100.0);

    for (const double ticks : {0.0, 1.0, 2999.5, 3000.0, 3001.0, 123456.789})
    {
        CHECK(map.ticksAt(map.samplesAt(ticks)) == Catch::Approx(ticks).margin(1e-6));
    }
}

TEST_CASE("sampleAt gives the first sample at or after the tick")
{
    // At 120 bpm and 44.1 kHz a tick lasts 22.96875 samples.
    const TimeMap map(TempoMap{}, 44100.0);

    CHECK(map.sampleAt(1) == 23);
    // 32 ticks are exactly 735 samples, which must not round up to 736.
    CHECK(map.sampleAt(32) == 735);
}

TEST_CASE("seconds convert at the sample rate")
{
    const TimeMap map(TempoMap{}, 48000.0);

    CHECK(map.secondsAt(96000) == 2.0);
    CHECK(map.sampleAtSeconds(1.5) == 72000);
}

TEST_CASE("bars and beats follow time signature changes")
{
    // 4/4 for two bars, 3/4 for two bars, then 6/8, which counts eighths.
    constexpr TickCount threeFour = 2 * s_bar;
    constexpr TickCount sixEight = threeFour + 2 * 3 * ticksPerBeat;
    const TimeMap map(tempoMap({TempoEvent{}}, {{.position = 0, .numerator = 4, .denominator = 4},
                                                {.position = threeFour, .numerator = 3, .denominator = 4},
                                                {.position = sixEight, .numerator = 6, .denominator = 8}}),
                      48000.0);

    const std::pair<TickCount, MusicalPosition> expected[] = {
        {0, {.bar = 1, .beat = 1, .tick = 0}},
        {threeFour - 1, {.bar = 2, .beat = 4, .tick = ticksPerBeat - 1}},
        {threeFour, {.bar = 3, .beat = 1, .tick = 0}},
        {threeFour + 3 * ticksPerBeat, {.bar = 4, .beat = 1, .tick = 0}},
        {sixEight, {.bar = 5, .beat = 1, .tick = 0}},
        {sixEight + ticksPerBeat / 2, {.bar = 5, .beat = 2, .tick = 0}},
        {sixEight + 3 * ticksPerBeat, {.bar = 6, .beat = 1, .tick = 0}},
    };
    for (const auto& [tick, position] : expected)
    {
        INFO("tick " << tick);
        CHECK(map.musicalPositionAt(tick) == position);
        CHECK(map.tickAt(position) == tick);
    }
    CHECK(map.timeSignatureAt(sixEight + 1) ==
          TimeSignatureEvent{.position = sixEight, .numerator = 6, .denominator = 8});
}

TEST_CASE("a time signature change inside a bar starts a new bar")
{
    // 3/4 starts on beat 3 of bar 2. The cut-off bar 2 still counts.
    const TimeMap map(tempoMap({TempoEvent{}}, {{.position = 0, .numerator = 4, .denominator = 4},
                                                {.position = 6 * ticksPerBeat, .numerator = 3, .denominator = 4}}),
                      48000.0);

    CHECK(map.musicalPositionAt(6 * ticksPerBeat) == MusicalPosition{.bar = 3, .beat = 1, .tick = 0});
}

TEST_CASE("beats fall on denominator notes with downbeats on bar starts")
{
    const TimeMap fourFour(TempoMap{}, 48000.0);

    CHECK(fourFour.beatAtOrAfter(0) == Beat{.tick = 0, .isDownbeat = true});
    CHECK(fourFour.beatAtOrAfter(1) == Beat{.tick = ticksPerBeat, .isDownbeat = false});
    CHECK(fourFour.beatAtOrAfter(3 * ticksPerBeat + 1) == Beat{.tick = s_bar, .isDownbeat = true});

    const TimeMap sixEight(tempoMap({TempoEvent{}}, {{.position = 0, .numerator = 6, .denominator = 8}}), 48000.0);

    CHECK(sixEight.beatAtOrAfter(1) == Beat{.tick = ticksPerBeat / 2, .isDownbeat = false});
    CHECK(sixEight.beatAtOrAfter(5 * ticksPerBeat / 2 + 1) == Beat{.tick = 3 * ticksPerBeat, .isDownbeat = true});
}

TEST_CASE("the next beat after a time signature change is the change itself")
{
    // 3/4 starts half a beat into bar 2, off the 4/4 beat grid.
    constexpr TickCount change = s_bar + ticksPerBeat / 2;
    const TimeMap map(tempoMap({TempoEvent{}}, {{.position = 0, .numerator = 4, .denominator = 4},
                                                {.position = change, .numerator = 3, .denominator = 4}}),
                      48000.0);

    CHECK(map.beatAtOrAfter(s_bar + 1) == Beat{.tick = change, .isDownbeat = true});
}
