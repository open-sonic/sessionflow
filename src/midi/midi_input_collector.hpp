#pragma once

#include "core/midi_event.hpp"
#include "core/realtime.hpp"
#include "core/spsc_queue.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace sessionflow
{

// A MIDI input message stamped on arrival, in nanoseconds of midiInputClockNs().
struct TimedMidiMessage
{
    std::int64_t timeNs = 0;
    std::array<std::uint8_t, 3> data{};
};

// The clock MIDI input is stamped with. Read it at the start of each audio callback and pass
// it to MidiInputCollector::collect().
std::int64_t midiInputClockNs() noexcept;

// Hands MIDI input from device callback threads to the audio thread. Every source has its own
// queue, so each device callback thread is the single producer of its source.
// Large: allocate it on the heap.
class MidiInputCollector
{
  public:
    static constexpr std::size_t maxSources = 16;
    static constexpr std::size_t queueCapacity = 1024;

    // Called from the callback thread of source. Drops the message and returns false when that
    // source's queue is full.
    bool push(std::size_t source, const TimedMidiMessage& message) noexcept;

    // Audio thread. Maps the messages stamped between the previous call's blockStartNs and this
    // one onto the frames of this block, writes them into out sorted by sampleOffset and returns
    // how many it wrote. Input runs one callback late so the spacing between events survives
    // irregular callback sizes and timing instead of snapping to block boundaries. Older
    // messages land at offset 0. Messages stamped at or after blockStartNs, or that don't fit
    // into out, wait for the next call. The first call after construction or reset() assumes
    // the previous callback started one block earlier.
    std::size_t collect(std::int64_t blockStartNs, std::size_t frameCount, double sampleRate,
                        std::span<MidiEvent> out) noexcept SESSIONFLOW_NONBLOCKING;

    // Audio thread, when the stream starts. Drops what was queued while it was stopped.
    void reset() noexcept SESSIONFLOW_NONBLOCKING;

    // Messages lost to full queues since construction.
    std::uint64_t droppedCount() const noexcept;

  private:
    struct Source
    {
        SpscQueue<TimedMidiMessage, queueCapacity> queue;
        std::optional<TimedMidiMessage> pending;
    };

    std::array<Source, maxSources> m_sources;
    std::optional<std::int64_t> m_previousBlockStartNs;
    std::atomic<std::uint64_t> m_droppedCount{0};
};

} // namespace sessionflow
