#include "midi/midi_input_collector.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <memory>
#include <vector>

using namespace sessionflow;

namespace
{

constexpr double s_sampleRate = 48000.0;
constexpr std::size_t s_frames = 480;
constexpr std::int64_t s_blockNs = 10'000'000;
constexpr std::int64_t s_blockStartNs = 1'000'000'000;
constexpr std::int64_t s_windowStartNs = s_blockStartNs - s_blockNs;
constexpr std::int64_t s_millisecondNs = 1'000'000;

TimedMidiMessage noteOn(std::int64_t timeNs, std::uint8_t key)
{
    return {.timeNs = timeNs, .data = {0x90, key, 100}};
}

} // namespace

TEST_CASE("messages land at their sample position in the next block")
{
    auto collector = std::make_unique<MidiInputCollector>();
    collector->push(0, noteOn(s_windowStartNs + 5 * s_millisecondNs, 60));
    collector->push(0, noteOn(s_windowStartNs + 6 * s_millisecondNs, 62));

    std::array<MidiEvent, 8> events{};
    REQUIRE(collector->collect(s_blockStartNs, s_frames, s_sampleRate, events) == 2);

    CHECK(events[0] == MidiEvent{.sampleOffset = 240, .data = {0x90, 60, 100}});
    CHECK(events[1] == MidiEvent{.sampleOffset = 288, .data = {0x90, 62, 100}});
}

TEST_CASE("messages stamped after the block start wait for the next block")
{
    auto collector = std::make_unique<MidiInputCollector>();
    collector->push(0, noteOn(s_blockStartNs + 2 * s_millisecondNs, 60));

    std::array<MidiEvent, 8> events{};
    CHECK(collector->collect(s_blockStartNs, s_frames, s_sampleRate, events) == 0);

    REQUIRE(collector->collect(s_blockStartNs + s_blockNs, s_frames, s_sampleRate, events) == 1);
    CHECK(events[0].sampleOffset == 96);
}

TEST_CASE("late messages land at the start of the block")
{
    auto collector = std::make_unique<MidiInputCollector>();
    collector->push(0, noteOn(s_windowStartNs - s_millisecondNs, 60));

    std::array<MidiEvent, 8> events{};
    REQUIRE(collector->collect(s_blockStartNs, s_frames, s_sampleRate, events) == 1);
    CHECK(events[0].sampleOffset == 0);
}

TEST_CASE("messages from several inputs are merged in time order")
{
    auto collector = std::make_unique<MidiInputCollector>();
    collector->push(0, noteOn(s_windowStartNs + 7 * s_millisecondNs, 60));
    collector->push(1, noteOn(s_windowStartNs + 3 * s_millisecondNs, 62));
    collector->push(1, noteOn(s_windowStartNs + 7 * s_millisecondNs, 64));

    std::array<MidiEvent, 8> events{};
    REQUIRE(collector->collect(s_blockStartNs, s_frames, s_sampleRate, events) == 3);

    CHECK(events[0].data[1] == 62);
    CHECK(events[1].data[1] == 60);
    CHECK(events[2].data[1] == 64);
}

TEST_CASE("messages that don't fit are kept for the next block")
{
    auto collector = std::make_unique<MidiInputCollector>();
    collector->push(0, noteOn(s_windowStartNs + 1 * s_millisecondNs, 60));
    collector->push(0, noteOn(s_windowStartNs + 2 * s_millisecondNs, 62));
    collector->push(0, noteOn(s_windowStartNs + 3 * s_millisecondNs, 64));

    std::array<MidiEvent, 2> events{};
    REQUIRE(collector->collect(s_blockStartNs, s_frames, s_sampleRate, events) == 2);
    CHECK(events[1].data[1] == 62);

    REQUIRE(collector->collect(s_blockStartNs + s_blockNs, s_frames, s_sampleRate, events) == 1);
    CHECK(events[0] == MidiEvent{.sampleOffset = 0, .data = {0x90, 64, 100}});
}

TEST_CASE("a full queue drops messages and counts them")
{
    auto collector = std::make_unique<MidiInputCollector>();
    for (std::size_t i = 0; i < MidiInputCollector::queueCapacity; ++i)
    {
        REQUIRE(collector->push(0, noteOn(s_windowStartNs, 60)));
    }

    CHECK_FALSE(collector->push(0, noteOn(s_windowStartNs, 60)));
    CHECK(collector->droppedCount() == 1);
    CHECK(collector->push(1, noteOn(s_windowStartNs, 60)));
}

TEST_CASE("the window follows the real spacing between callbacks")
{
    auto collector = std::make_unique<MidiInputCollector>();
    std::array<MidiEvent, 8> events{};
    REQUIRE(collector->collect(s_blockStartNs, s_frames, s_sampleRate, events) == 0);

    SECTION("a smaller block")
    {
        collector->push(0, noteOn(s_blockStartNs + 5 * s_millisecondNs, 60));
        REQUIRE(collector->collect(s_blockStartNs + s_blockNs, s_frames / 2, s_sampleRate, events) == 1);
        CHECK(events[0].sampleOffset == s_frames / 4);
    }

    SECTION("two callbacks in a burst")
    {
        collector->push(0, noteOn(s_blockStartNs + 10 * s_millisecondNs, 60));
        collector->push(0, noteOn(s_blockStartNs + 30 * s_millisecondNs, 62));
        REQUIRE(collector->collect(s_blockStartNs + 2 * s_blockNs, s_frames, s_sampleRate, events) == 1);
        CHECK(events[0].sampleOffset == s_frames / 2);

        REQUIRE(collector->collect(s_blockStartNs + 2 * s_blockNs, s_frames, s_sampleRate, events) == 0);
        REQUIRE(collector->collect(s_blockStartNs + 4 * s_blockNs, s_frames, s_sampleRate, events) == 1);
        CHECK(events[0].sampleOffset == s_frames / 2);
    }
}

TEST_CASE("reset drops what was queued while the stream was stopped")
{
    auto collector = std::make_unique<MidiInputCollector>();
    collector->push(0, noteOn(s_windowStartNs, 60));
    collector->push(3, noteOn(s_windowStartNs, 62));

    collector->reset();

    std::array<MidiEvent, 8> events{};
    CHECK(collector->collect(s_blockStartNs, s_frames, s_sampleRate, events) == 0);
}

TEST_CASE("an invalid sample rate collects nothing")
{
    auto collector = std::make_unique<MidiInputCollector>();
    collector->push(0, noteOn(s_windowStartNs, 60));

    std::array<MidiEvent, 8> events{};
    CHECK(collector->collect(s_blockStartNs, s_frames, 0.0, events) == 0);
    CHECK(collector->collect(s_blockStartNs, s_frames, std::nan(""), events) == 0);
    CHECK(collector->collect(s_blockStartNs, s_frames, s_sampleRate, events) == 1);
}

TEST_CASE("late messages from several inputs keep their time order")
{
    auto collector = std::make_unique<MidiInputCollector>();
    collector->push(0, noteOn(s_windowStartNs - 2 * s_millisecondNs, 60));
    collector->push(1, noteOn(s_windowStartNs - 3 * s_millisecondNs, 62));

    std::array<MidiEvent, 8> events{};
    REQUIRE(collector->collect(s_blockStartNs, s_frames, s_sampleRate, events) == 2);
    CHECK(events[0].data[1] == 62);
    CHECK(events[1].data[1] == 60);
}

TEST_CASE("a busy input doesn't hold back earlier messages from another")
{
    auto collector = std::make_unique<MidiInputCollector>();
    collector->push(0, noteOn(s_windowStartNs + 2 * s_millisecondNs, 60));
    collector->push(0, noteOn(s_windowStartNs + 3 * s_millisecondNs, 62));
    collector->push(1, noteOn(s_windowStartNs + 1 * s_millisecondNs, 64));

    std::array<MidiEvent, 2> events{};
    REQUIRE(collector->collect(s_blockStartNs, s_frames, s_sampleRate, events) == 2);
    CHECK(events[0].data[1] == 64);
    CHECK(events[1].data[1] == 60);
}

TEST_CASE("a note-off a full queue dropped arrives once the queue has drained")
{
    auto collector = std::make_unique<MidiInputCollector>();
    for (std::size_t i = 0; i < MidiInputCollector::queueCapacity; ++i)
    {
        REQUIRE(collector->push(0, noteOn(s_windowStartNs, 60)));
    }
    CHECK_FALSE(collector->push(0, {.timeNs = s_windowStartNs, .data = {0x81, 61, 64}}));

    SECTION("by the audio thread")
    {
        std::vector<MidiEvent> events(MidiInputCollector::queueCapacity + 1);
        REQUIRE(collector->collect(s_blockStartNs, s_frames, s_sampleRate, events) == events.size());
        CHECK(events.back() == MidiEvent{.sampleOffset = s_frames - 1, .data = {0x81, 61, 0}});
    }

    SECTION("ahead of the input's next message")
    {
        std::vector<MidiEvent> events(MidiInputCollector::queueCapacity);
        REQUIRE(collector->collect(s_blockStartNs, s_frames, s_sampleRate, events) == events.size());

        REQUIRE(collector->push(0, {.timeNs = s_blockStartNs + s_millisecondNs, .data = {0x91, 61, 100}}));
        REQUIRE(collector->collect(s_blockStartNs + s_blockNs, s_frames, s_sampleRate, events) == 2);
        CHECK(events[0].data == std::array<std::uint8_t, 3>{0x81, 61, 0});
        CHECK(events[1].data == std::array<std::uint8_t, 3>{0x91, 61, 100});
    }
}

TEST_CASE("reset keeps the releases it drops")
{
    auto collector = std::make_unique<MidiInputCollector>();
    collector->push(2, noteOn(s_windowStartNs, 60));
    collector->push(2, {.timeNs = s_windowStartNs, .data = {0x90, 62, 0}});
    collector->push(2, {.timeNs = s_windowStartNs, .data = {0xB3, 64, 0}});

    collector->reset();

    std::array<MidiEvent, 8> events{};
    REQUIRE(collector->collect(s_blockStartNs, s_frames, s_sampleRate, events) == 2);
    CHECK(events[0].data == std::array<std::uint8_t, 3>{0x80, 62, 0});
    CHECK(events[1].data == std::array<std::uint8_t, 3>{0xB3, 64, 0});
}
