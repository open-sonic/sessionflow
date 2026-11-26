#include "midi/midi_input_collector.hpp"

#include <algorithm>
#include <chrono>
#include <limits>

namespace sessionflow
{

namespace
{

constexpr double s_nanosecondsPerSecond = 1e9;

} // namespace

std::int64_t midiInputClockNs() noexcept
{
    // libremidi's SystemMonotonic timestamps use the same clock.
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

bool MidiInputCollector::push(std::size_t source, const TimedMidiMessage& message) noexcept
{
    if (m_sources[source].queue.tryPush(message))
    {
        return true;
    }
    m_droppedCount.fetch_add(1, std::memory_order_relaxed);
    return false;
}

std::size_t MidiInputCollector::collect(std::int64_t blockStartNs, std::size_t frameCount, double sampleRate,
                                        std::span<MidiEvent> out) noexcept SESSIONFLOW_NONBLOCKING
{
    if (frameCount == 0 || frameCount > std::numeric_limits<std::uint32_t>::max() || !(sampleRate >= 1.0))
    {
        return 0;
    }

    // The previous callback's start, not blockStart minus this block's length: callbacks vary
    // in size and often arrive in bursts, and only the real spacing keeps every message inside
    // exactly one window.
    std::int64_t windowStartNs = 0;
    if (m_previousBlockStartNs)
    {
        windowStartNs = std::min(*m_previousBlockStartNs, blockStartNs);
    }
    else
    {
        const double blockLengthNs = static_cast<double>(frameCount) * s_nanosecondsPerSecond / sampleRate;
        windowStartNs = blockStartNs - static_cast<std::int64_t>(blockLengthNs);
    }
    m_previousBlockStartNs = blockStartNs;

    const double windowLengthNs = static_cast<double>(blockStartNs - windowStartNs);
    const double framesPerNanosecond = windowLengthNs > 0.0 ? static_cast<double>(frameCount) / windowLengthNs : 0.0;
    const auto lastFrame = static_cast<std::uint32_t>(frameCount - 1);

    // Merges the sources by time, so late input from several devices keeps its order and a busy
    // source can't push the others' earlier messages into the next block.
    std::size_t count = 0;
    std::uint32_t previousOffset = 0;
    while (count < out.size())
    {
        Source* earliest = nullptr;
        for (Source& source : m_sources)
        {
            if (!source.pending)
            {
                source.pending = source.queue.tryPop();
            }
            if (source.pending && source.pending->timeNs < blockStartNs &&
                (earliest == nullptr || source.pending->timeNs < earliest->pending->timeNs))
            {
                earliest = &source;
            }
        }
        if (earliest == nullptr)
        {
            break;
        }

        std::uint32_t offset = 0;
        if (earliest->pending->timeNs > windowStartNs)
        {
            const double frame = static_cast<double>(earliest->pending->timeNs - windowStartNs) * framesPerNanosecond;
            offset = frame < lastFrame ? static_cast<std::uint32_t>(frame) : lastFrame;
        }
        // A device clock that steps backwards must not unsort the block.
        offset = std::max(offset, previousOffset);
        previousOffset = offset;
        out[count++] = MidiEvent{.sampleOffset = offset, .data = earliest->pending->data};
        earliest->pending.reset();
    }
    return count;
}

void MidiInputCollector::reset() noexcept SESSIONFLOW_NONBLOCKING
{
    for (Source& source : m_sources)
    {
        source.pending.reset();
        while (source.queue.tryPop())
        {
        }
    }
    m_previousBlockStartNs.reset();
}

std::uint64_t MidiInputCollector::droppedCount() const noexcept
{
    return m_droppedCount.load(std::memory_order_relaxed);
}

} // namespace sessionflow
