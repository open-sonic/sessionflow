#pragma once

#include <algorithm>

namespace sessionflow
{

// Ramps a parameter linearly to its target so value jumps don't cause zipper noise.
class LinearSmoother
{
  public:
    // Call off the audio thread before processing starts.
    void prepare(double sampleRate, double rampSeconds) noexcept
    {
        m_rampLength = std::max(1, static_cast<int>(sampleRate * rampSeconds));
    }

    void setTarget(float target) noexcept
    {
        if (target == m_target)
        {
            return;
        }
        m_target = target;
        m_remaining = m_rampLength;
        m_step = (m_target - m_current) / static_cast<float>(m_rampLength);
    }

    void snapTo(float value) noexcept
    {
        m_current = value;
        m_target = value;
        m_step = 0.0f;
        m_remaining = 0;
    }

    float next() noexcept
    {
        if (m_remaining == 0)
        {
            return m_current;
        }
        --m_remaining;
        m_current = m_remaining == 0 ? m_target : m_current + m_step;
        return m_current;
    }

    bool isSmoothing() const noexcept
    {
        return m_remaining > 0;
    }

    float target() const noexcept
    {
        return m_target;
    }

  private:
    float m_current = 0.0f;
    float m_target = 0.0f;
    float m_step = 0.0f;
    int m_remaining = 0;
    int m_rampLength = 1;
};

} // namespace sessionflow
