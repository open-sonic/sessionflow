#include "midi/midi_input_collector.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <limits>

namespace sessionflow
{

namespace
{

constexpr double s_nanosecondsPerSecond = 1e9;
constexpr std::uint8_t s_noteOff = 0x80;
constexpr std::uint8_t s_noteOn = 0x90;
constexpr std::uint8_t s_controlChange = 0xB0;
constexpr std::uint8_t s_sustainPedal = 64;

} // namespace

std::int64_t midiInputClockNs() noexcept SESSIONFLOW_NONBLOCKING
{
    // libremidi's SystemMonotonic timestamps use the same clock.
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

void MidiInputCollector::remember(LostReleases& lost,
                                  const std::array<std::uint8_t, 3>& data) noexcept SESSIONFLOW_NONBLOCKING
{
    const std::uint8_t type = data[0] & 0xF0;
    const std::size_t channel = data[0] & 0x0F;
    if (type == s_noteOff || (type == s_noteOn && data[2] == 0))
    {
        const std::size_t bit = channel * s_keyCount + (data[1] & 0x7F);
        lost.noteOffs[bit / 64].fetch_or(std::uint64_t{1} << (bit % 64), std::memory_order_relaxed);
    }
    else if (type == s_controlChange && data[1] == s_sustainPedal && data[2] < 64)
    {
        lost.sustainOffs.fetch_or(static_cast<std::uint16_t>(1u << channel), std::memory_order_relaxed);
    }
    else
    {
        return;
    }
    lost.any.store(true, std::memory_order_release);
}

template <typename Emit>
bool MidiInputCollector::flush(LostReleases& lost, Emit&& emit) noexcept SESSIONFLOW_NONBLOCKING
{
    if (!lost.any.exchange(false, std::memory_order_acq_rel))
    {
        return true;
    }

    for (std::size_t word = 0; word < s_lostNoteOffWords; ++word)
    {
        std::uint64_t bits = lost.noteOffs[word].exchange(0, std::memory_order_relaxed);
        while (bits != 0)
        {
            const std::size_t bit = word * 64 + static_cast<std::size_t>(std::countr_zero(bits));
            const auto status = static_cast<std::uint8_t>(s_noteOff | (bit / s_keyCount));
            if (!emit(std::array<std::uint8_t, 3>{status, static_cast<std::uint8_t>(bit % s_keyCount), 0}))
            {
                lost.noteOffs[word].fetch_or(bits, std::memory_order_relaxed);
                lost.any.store(true, std::memory_order_release);
                return false;
            }
            bits &= bits - 1;
        }
    }

    std::uint16_t channels = lost.sustainOffs.exchange(0, std::memory_order_relaxed);
    while (channels != 0)
    {
        const auto status = static_cast<std::uint8_t>(s_controlChange | std::countr_zero(channels));
        if (!emit(std::array<std::uint8_t, 3>{status, s_sustainPedal, 0}))
        {
            lost.sustainOffs.fetch_or(channels, std::memory_order_relaxed);
            lost.any.store(true, std::memory_order_release);
            return false;
        }
        channels &= static_cast<std::uint16_t>(channels - 1);
    }
    return true;
}

bool MidiInputCollector::push(std::size_t source, const TimedMidiMessage& message) noexcept
{
    if (source >= maxSources)
    {
        m_droppedCount.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    Source& target = m_sources[source];
    const auto pushLost = [&](const std::array<std::uint8_t, 3>& data)
    { return target.queue.tryPush({.timeNs = message.timeNs, .data = data}); };

    // Lost releases go first, so they stay ahead of everything played after them.
    if (flush(target.lost, pushLost) && target.queue.tryPush(message))
    {
        return true;
    }
    remember(target.lost, message.data);
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

    // Releases a full queue lost are newer than anything that was queued, so they wait until
    // their source has run empty and then go at the end of the block.
    const auto emit = [&](const std::array<std::uint8_t, 3>& data)
    {
        if (count == out.size())
        {
            return false;
        }
        out[count++] = MidiEvent{.sampleOffset = lastFrame, .data = data};
        return true;
    };
    for (Source& source : m_sources)
    {
        if (!source.pending)
        {
            source.pending = source.queue.tryPop();
        }
        if (!source.pending)
        {
            flush(source.lost, emit);
        }
    }
    return count;
}

void MidiInputCollector::reset() noexcept SESSIONFLOW_NONBLOCKING
{
    for (Source& source : m_sources)
    {
        if (source.pending)
        {
            remember(source.lost, source.pending->data);
            source.pending.reset();
        }
        while (const std::optional<TimedMidiMessage> message = source.queue.tryPop())
        {
            remember(source.lost, message->data);
        }
    }
    m_previousBlockStartNs.reset();
}

std::uint64_t MidiInputCollector::droppedCount() const noexcept
{
    return m_droppedCount.load(std::memory_order_relaxed);
}

} // namespace sessionflow
