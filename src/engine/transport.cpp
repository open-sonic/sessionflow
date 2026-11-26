#include "engine/transport.hpp"

#include <cassert>
#include <cmath>
#include <optional>

namespace sessionflow
{

Transport::Transport(std::unique_ptr<const TimeMap> timeMap) : m_timeMap(timeMap.release())
{
    assert(m_timeMap != nullptr);
}

Transport::~Transport()
{
    // Maps still queued never reached the audio thread, so they are ours to free.
    while (const std::optional<Command> command = m_commands.tryPop())
    {
        delete command->timeMap;
    }
    collect();
    delete m_timeMap;
}

void Transport::prepare(std::size_t maxBlockFrames)
{
    // A segment holds at least one frame, so a block never has more segments than frames.
    m_segments.assign(maxBlockFrames, {});
}

bool Transport::play()
{
    return m_commands.tryPush({.type = Command::Type::Play});
}

bool Transport::stop()
{
    return m_commands.tryPush({.type = Command::Type::Stop});
}

bool Transport::locate(TickCount position)
{
    return m_commands.tryPush({.type = Command::Type::Locate, .first = position});
}

bool Transport::setLoop(LoopRange loop)
{
    return m_commands.tryPush({.type = Command::Type::SetLoop, .first = loop.start, .second = loop.end});
}

bool Transport::setLoopEnabled(bool isEnabled)
{
    return m_commands.tryPush({.type = Command::Type::SetLoopEnabled, .flag = isEnabled});
}

bool Transport::setTimeMap(std::unique_ptr<const TimeMap> timeMap)
{
    assert(timeMap != nullptr);
    if (m_timeMapsInFlight == s_retiredCapacity)
    {
        collect();
        if (m_timeMapsInFlight == s_retiredCapacity)
        {
            return false;
        }
    }
    if (!m_commands.tryPush({.type = Command::Type::SetTimeMap, .timeMap = timeMap.get()}))
    {
        return false;
    }
    timeMap.release();
    ++m_timeMapsInFlight;
    return true;
}

void Transport::collect()
{
    while (const std::optional<const TimeMap*> timeMap = m_retiredTimeMaps.tryPop())
    {
        delete *timeMap;
        --m_timeMapsInFlight;
    }
}

SampleCount Transport::displayPosition() const noexcept
{
    return m_displayPosition.load(std::memory_order_relaxed);
}

bool Transport::isPlayingForDisplay() const noexcept
{
    return m_isPlayingForDisplay.load(std::memory_order_relaxed);
}

void Transport::apply(const Command& command) noexcept SESSIONFLOW_NONBLOCKING
{
    switch (command.type)
    {
    case Command::Type::Play:
        m_isPlaying = true;
        break;
    case Command::Type::Stop:
        m_isPlaying = false;
        break;
    case Command::Type::Locate:
        m_position = m_timeMap->sampleAt(command.first);
        break;
    case Command::Type::SetLoop:
        m_loop = {.start = command.first, .end = command.second};
        break;
    case Command::Type::SetLoopEnabled:
        m_isLoopEnabled = command.flag;
        break;
    case Command::Type::SetTimeMap:
    {
        // Keep the playhead on the same musical position under the new tempo.
        const double ticks = m_timeMap->ticksAt(static_cast<double>(m_position));
        m_position = static_cast<SampleCount>(std::llround(command.timeMap->samplesAt(ticks)));
        [[maybe_unused]] const bool isRetired = m_retiredTimeMaps.tryPush(m_timeMap);
        assert(isRetired);
        m_timeMap = command.timeMap;
        break;
    }
    }
}

std::span<const TransportSegment> Transport::advance(std::size_t frameCount) noexcept SESSIONFLOW_NONBLOCKING
{
    assert(frameCount <= m_segments.size());
    while (const std::optional<Command> command = m_commands.tryPop())
    {
        apply(*command);
    }

    std::size_t segmentCount = 0;
    if (m_isPlaying)
    {
        const SampleCount loopStart = m_timeMap->sampleAt(m_loop.start);
        const SampleCount loopEnd = m_timeMap->sampleAt(m_loop.end);
        const bool isLooping = m_isLoopEnabled && loopEnd > loopStart;

        std::size_t offset = 0;
        while (offset < frameCount)
        {
            std::size_t count = frameCount - offset;
            // Only a playhead before the loop end wraps. One placed after it plays on.
            const bool wraps =
                isLooping && m_position < loopEnd && loopEnd - m_position <= static_cast<SampleCount>(count);
            if (wraps)
            {
                count = static_cast<std::size_t>(loopEnd - m_position);
            }
            m_segments[segmentCount++] = {.offset = offset, .frameCount = count, .position = m_position};
            m_position = wraps ? loopStart : m_position + static_cast<SampleCount>(count);
            offset += count;
        }
    }

    m_displayPosition.store(m_position, std::memory_order_relaxed);
    m_isPlayingForDisplay.store(m_isPlaying, std::memory_order_relaxed);
    return {m_segments.data(), segmentCount};
}

TransportState Transport::stateAt(const TransportSegment& segment) const noexcept SESSIONFLOW_NONBLOCKING
{
    return {
        .position = segment.position,
        .tempo = m_timeMap->tempoAt(m_timeMap->ticksAt(static_cast<double>(segment.position))),
        .playing = m_isPlaying,
    };
}

} // namespace sessionflow
