#pragma once

#include "core/audio_buffer.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sessionflow::test
{

inline void fillSine(AudioBufferView buffer, double frequency, double sampleRate, float amplitude = 1.0f)
{
    for (std::size_t channel = 0; channel < buffer.channelCount(); ++channel)
    {
        const std::span<float> samples = buffer.channel(channel);
        for (std::size_t frame = 0; frame < samples.size(); ++frame)
        {
            const double phase = 2.0 * std::numbers::pi * frequency * static_cast<double>(frame) / sampleRate;
            samples[frame] = amplitude * static_cast<float>(std::sin(phase));
        }
    }
}

inline void fillConstant(AudioBufferView buffer, float value)
{
    for (std::size_t channel = 0; channel < buffer.channelCount(); ++channel)
    {
        std::ranges::fill(buffer.channel(channel), value);
    }
}

inline float peak(AudioBufferView buffer)
{
    float result = 0.0f;
    for (std::size_t channel = 0; channel < buffer.channelCount(); ++channel)
    {
        for (const float sample : buffer.channel(channel))
        {
            result = std::max(result, std::abs(sample));
        }
    }
    return result;
}

} // namespace sessionflow::test
