#include "devices/gain.hpp"

#include <cmath>

namespace sessionflow
{

namespace
{

constexpr double s_rampSeconds = 0.02;

} // namespace

void GainDevice::setGainDecibels(float decibels) noexcept
{
    m_targetGain.store(std::pow(10.0f, decibels / 20.0f), std::memory_order_relaxed);
}

void GainDevice::prepare(const ProcessSetup& setup)
{
    m_gain.prepare(setup.sampleRate, s_rampSeconds);
    m_gain.snapTo(m_targetGain.load(std::memory_order_relaxed));
}

void GainDevice::process(const ProcessBlock& block) noexcept SESSIONFLOW_NONBLOCKING
{
    m_gain.setTarget(m_targetGain.load(std::memory_order_relaxed));

    const AudioBufferView audio = block.audio;
    for (std::size_t frame = 0; frame < audio.frameCount(); ++frame)
    {
        const float gain = m_gain.next();
        for (std::size_t channel = 0; channel < audio.channelCount(); ++channel)
        {
            audio.channel(channel)[frame] *= gain;
        }
    }
}

} // namespace sessionflow
