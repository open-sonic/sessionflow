#include "midi/midi_input_collector.hpp"

#include <chrono>

namespace sessionflow
{

namespace
{

constexpr double s_nanosecondsPerSecond = 1e9;

// Insertion sort: stable, so events at the same offset keep their arrival order, and it
// needs no allocation.
void sortBySampleOffset(std::span<MidiEvent> events) noexcept SESSIONFLOW_NONBLOCKING
{
    for (std::size_t i = 1; i < events.size(); ++i)
    {
        const MidiEvent event = events[i];
        std::size_t j = i;
        while (j > 0 && events[j - 1].sampleOffset > event.sampleOffset)
        {
            events[j] = events[j - 1];
            --j;
        }
        events[j] = event;
    }
}

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

std::size_t MidiInputCollector::collect(std::int64_t blockStartNs, std::uint32_t frameCount, double sampleRate,
                                        std::span<MidiEvent> out) noexcept SESSIONFLOW_NONBLOCKING
{
    if (frameCount == 0)
    {
        return 0;
    }

    const double samplesPerNanosecond = sampleRate / s_nanosecondsPerSecond;
    const auto blockLengthNs = static_cast<std::int64_t>(frameCount / samplesPerNanosecond);
    const std::int64_t windowStartNs = blockStartNs - blockLengthNs;
    const std::uint32_t lastFrame = frameCount - 1;

    std::size_t count = 0;
    for (Source& source : m_sources)
    {
        while (count < out.size())
        {
            if (!source.pending)
            {
                source.pending = source.queue.tryPop();
            }
            if (!source.pending || source.pending->timeNs >= blockStartNs)
            {
                break;
            }

            std::uint32_t offset = 0;
            if (source.pending->timeNs > windowStartNs)
            {
                const auto frame = static_cast<std::uint32_t>(
                    static_cast<double>(source.pending->timeNs - windowStartNs) * samplesPerNanosecond);
                offset = frame < lastFrame ? frame : lastFrame;
            }
            out[count++] = MidiEvent{.sampleOffset = offset, .data = source.pending->data};
            source.pending.reset();
        }
    }

    sortBySampleOffset(out.first(count));
    return count;
}

std::uint64_t MidiInputCollector::droppedCount() const noexcept
{
    return m_droppedCount.load(std::memory_order_relaxed);
}

} // namespace sessionflow
