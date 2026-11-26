#include "engine/metronome.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sessionflow
{

namespace
{

constexpr double s_clickSeconds = 0.03;
constexpr double s_decaySeconds = 0.006;
constexpr double s_downbeatFrequency = 1760.0;
constexpr double s_beatFrequency = 880.0;
constexpr float s_downbeatLevel = 0.5f;
constexpr float s_beatLevel = 0.35f;

// A decaying cosine burst. Starting at full level is what makes it a click, and its first
// sample is exactly the click level.
std::vector<float> renderClick(double sampleRate, double frequency, float level)
{
    std::vector<float> click(static_cast<std::size_t>(std::ceil(s_clickSeconds * sampleRate)));
    for (std::size_t index = 0; index < click.size(); ++index)
    {
        const double seconds = static_cast<double>(index) / sampleRate;
        const double envelope = std::exp(-seconds / s_decaySeconds);
        click[index] = level * static_cast<float>(envelope * std::cos(2.0 * std::numbers::pi * frequency * seconds));
    }
    return click;
}

} // namespace

void Metronome::prepare(double sampleRate)
{
    m_downbeatClick = renderClick(sampleRate, s_downbeatFrequency, s_downbeatLevel);
    m_beatClick = renderClick(sampleRate, s_beatFrequency, s_beatLevel);
    m_click = nullptr;
    m_clickPosition = 0;
}

void Metronome::setEnabled(bool isEnabled) noexcept
{
    m_isEnabled.store(isEnabled, std::memory_order_relaxed);
}

void Metronome::mixClick(AudioBufferView output, std::size_t from, std::size_t to) noexcept SESSIONFLOW_NONBLOCKING
{
    if (m_click == nullptr)
    {
        return;
    }
    const std::size_t count = std::min(to - from, m_click->size() - m_clickPosition);
    for (std::size_t channel = 0; channel < output.channelCount(); ++channel)
    {
        const std::span<float> samples = output.channel(channel);
        for (std::size_t index = 0; index < count; ++index)
        {
            samples[from + index] += (*m_click)[m_clickPosition + index];
        }
    }
    m_clickPosition += count;
    if (m_clickPosition == m_click->size())
    {
        m_click = nullptr;
    }
}

void Metronome::render(AudioBufferView output, std::span<const TransportSegment> segments,
                       const TimeMap& timeMap) noexcept SESSIONFLOW_NONBLOCKING
{
    if (!m_isEnabled.load(std::memory_order_relaxed))
    {
        mixClick(output, 0, output.frameCount());
        return;
    }

    std::size_t cursor = 0;
    for (const TransportSegment& segment : segments)
    {
        const SampleCount segmentEnd = segment.position + static_cast<SampleCount>(segment.frameCount);
        const double startTicks = timeMap.ticksAt(static_cast<double>(segment.position));
        Beat beat = timeMap.beatAtOrAfter(static_cast<TickCount>(std::floor(startTicks)));
        SampleCount beatSample = timeMap.sampleAt(beat.tick);
        while (beatSample < segment.position)
        {
            beat = timeMap.beatAtOrAfter(beat.tick + 1);
            beatSample = timeMap.sampleAt(beat.tick);
        }

        while (beatSample < segmentEnd)
        {
            const std::size_t offset = segment.offset + static_cast<std::size_t>(beatSample - segment.position);
            mixClick(output, cursor, offset);
            cursor = offset;
            m_click = beat.isDownbeat ? &m_downbeatClick : &m_beatClick;
            m_clickPosition = 0;

            beat = timeMap.beatAtOrAfter(beat.tick + 1);
            beatSample = timeMap.sampleAt(beat.tick);
        }
    }
    mixClick(output, cursor, output.frameCount());
}

} // namespace sessionflow
