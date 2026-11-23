#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <span>
#include <vector>

namespace sessionflow
{

// Non-owning view of planar float audio. Cheap to copy and safe to use on the audio thread.
class AudioBufferView
{
  public:
    AudioBufferView() = default;

    AudioBufferView(float* const* channels, std::size_t channelCount, std::size_t frameCount) noexcept
        : m_channels(channels), m_channelCount(channelCount), m_frameCount(frameCount)
    {
    }

    std::size_t channelCount() const noexcept
    {
        return m_channelCount;
    }

    std::size_t frameCount() const noexcept
    {
        return m_frameCount;
    }

    std::span<float> channel(std::size_t index) const noexcept
    {
        assert(index < m_channelCount);
        return {m_channels[index] + m_offset, m_frameCount};
    }

    // Lets a processor split its block at event positions without copying.
    AudioBufferView slice(std::size_t start, std::size_t count) const noexcept
    {
        assert(start + count <= m_frameCount);
        AudioBufferView result = *this;
        result.m_offset += start;
        result.m_frameCount = count;
        return result;
    }

    void clear() const noexcept
    {
        for (std::size_t index = 0; index < m_channelCount; ++index)
        {
            std::ranges::fill(channel(index), 0.0f);
        }
    }

  private:
    float* const* m_channels = nullptr;
    std::size_t m_channelCount = 0;
    std::size_t m_frameCount = 0;
    std::size_t m_offset = 0;
};

// Owning planar buffer. Allocates in the constructor only, so create it off the audio thread.
class AudioBuffer
{
  public:
    AudioBuffer(std::size_t channelCount, std::size_t frameCount)
        : m_samples(channelCount * frameCount), m_channels(channelCount), m_frameCount(frameCount)
    {
        for (std::size_t index = 0; index < channelCount; ++index)
        {
            m_channels[index] = m_samples.data() + index * frameCount;
        }
    }

    // Copying would leave the channel pointers aimed at the source's samples.
    AudioBuffer(const AudioBuffer&) = delete;
    AudioBuffer& operator=(const AudioBuffer&) = delete;
    AudioBuffer(AudioBuffer&&) noexcept = default;
    AudioBuffer& operator=(AudioBuffer&&) noexcept = default;

    AudioBufferView view() noexcept
    {
        return {m_channels.data(), m_channels.size(), m_frameCount};
    }

    std::size_t channelCount() const noexcept
    {
        return m_channels.size();
    }

    std::size_t frameCount() const noexcept
    {
        return m_frameCount;
    }

  private:
    std::vector<float> m_samples;
    std::vector<float*> m_channels;
    std::size_t m_frameCount = 0;
};

} // namespace sessionflow
