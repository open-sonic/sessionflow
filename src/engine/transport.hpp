#pragma once

#include "core/realtime.hpp"
#include "core/spsc_queue.hpp"
#include "core/time.hpp"
#include "engine/processor.hpp"
#include "engine/time_map.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace sessionflow
{

// A loop in ticks, so it stays on its bars when the tempo changes. Active only when end > start.
struct LoopRange
{
    TickCount start = 0;
    TickCount end = 0;

    bool operator==(const LoopRange&) const = default;
};

// A stretch of the current block that plays consecutive timeline samples. A loop wrap starts
// a new segment, so processors render both sides of the wrap sample-accurately.
struct TransportSegment
{
    std::size_t offset = 0;
    std::size_t frameCount = 0;
    SampleCount position = 0; // timeline sample of the segment's first frame

    bool operator==(const TransportSegment&) const = default;
};

// A control-thread request the audio thread applies at the start of its next block. Outside
// Transport because GCC rejects nested types with member initializers in SpscQueue's
// static_assert while the enclosing class is incomplete.
struct TransportCommand
{
    enum class Type : std::uint8_t
    {
        Play,
        Stop,
        Locate,
        SetLoop,
        SetLoopEnabled,
        SetTimeMap,
    };

    Type type = Type::Play;
    TickCount first = 0;
    TickCount second = 0;
    bool flag = false;
    const TimeMap* timeMap = nullptr;
};

// Playhead, play state, loop and tempo map. The control thread sends commands, the audio
// thread applies them at the start of its next block and moves the playhead.
//
// The playhead is an integer sample position. Musical positions are derived from it through
// the time map, so they cannot drift, and a tempo change keeps the playhead on its beat.
class Transport
{
  public:
    explicit Transport(std::unique_ptr<const TimeMap> timeMap);
    ~Transport();

    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;

    // Control thread, before the audio thread runs. advance() takes at most maxBlockFrames.
    void prepare(std::size_t maxBlockFrames);

    // Control thread. Each returns false when the command queue is full, which only happens
    // if the audio thread has stopped calling advance().
    bool play();
    bool stop();
    bool locate(TickCount position);
    bool setLoop(LoopRange loop);
    bool setLoopEnabled(bool isEnabled);

    // Control thread. Also false while the audio thread still holds many replaced maps:
    // call collect() and try again.
    bool setTimeMap(std::unique_ptr<const TimeMap> timeMap);

    // Control thread, regularly. Frees time maps the audio thread no longer uses.
    void collect();

    // Control thread. Lags the audio thread by up to one block.
    SampleCount displayPosition() const noexcept;
    bool isPlayingForDisplay() const noexcept;

    // Audio thread. Applies pending commands, then returns the segments of this block. Empty
    // while stopped.
    std::span<const TransportSegment> advance(std::size_t frameCount) noexcept SESSIONFLOW_NONBLOCKING;

    // Audio thread, after advance().
    const TimeMap& timeMap() const noexcept
    {
        return *m_timeMap;
    }

    bool isPlaying() const noexcept
    {
        return m_isPlaying;
    }

    SampleCount position() const noexcept
    {
        return m_position;
    }

    // What a processor needs to know about one segment.
    TransportState stateAt(const TransportSegment& segment) const noexcept SESSIONFLOW_NONBLOCKING;

  private:
    using Command = TransportCommand;

    static constexpr std::size_t s_commandCapacity = 256;
    // Every map sent replaces one, so keeping no more maps in flight than this guarantees
    // the audio thread can always hand the replaced one back without blocking or freeing.
    static constexpr std::size_t s_retiredCapacity = 32;

    void apply(const Command& command) noexcept SESSIONFLOW_NONBLOCKING;

    // Shared between the threads.
    SpscQueue<Command, s_commandCapacity> m_commands;
    SpscQueue<const TimeMap*, s_retiredCapacity> m_retiredTimeMaps;
    std::atomic<SampleCount> m_displayPosition{0};
    std::atomic<bool> m_isPlayingForDisplay{false};

    // Control thread.
    std::size_t m_timeMapsInFlight = 0;

    // Audio thread, after prepare().
    const TimeMap* m_timeMap;
    std::vector<TransportSegment> m_segments;
    SampleCount m_position = 0;
    LoopRange m_loop;
    bool m_isLoopEnabled = false;
    bool m_isPlaying = false;
};

} // namespace sessionflow
