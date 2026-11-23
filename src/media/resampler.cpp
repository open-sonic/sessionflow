#include "media/resampler.hpp"

#include <CDSPResampler.h>

#include <algorithm>
#include <cassert>
#include <memory>

// r8brain's processing path neither allocates nor locks, but it runs through virtual calls
// that Clang's static nonblocking check can't follow. RealtimeSanitizer still checks it at
// run time.
#if defined(__clang__)
#if __has_warning("-Wfunction-effects")
#pragma clang diagnostic ignored "-Wfunction-effects"
#endif
#endif

namespace sessionflow
{

struct Resampler::Channel
{
    std::unique_ptr<r8b::CDSPResampler24> resampler;
    std::vector<double> input;
    std::vector<float> output;
};

Resampler::Resampler(std::size_t channelCount, double sourceRate, double targetRate, std::size_t maxReadFrames)
    : m_input(channelCount, inputBlockFrames)
{
    m_channels.resize(channelCount);
    for (Channel& channel : m_channels)
    {
        channel.resampler =
            std::make_unique<r8b::CDSPResampler24>(sourceRate, targetRate, static_cast<int>(inputBlockFrames));
        channel.input.resize(inputBlockFrames);
        // read() only drains, so the most it ever holds is a read's worth short of the
        // threshold plus one full write.
        const auto maxWriteFrames = static_cast<std::size_t>(channel.resampler->getMaxOutLen(0));
        channel.output.resize(maxReadFrames + maxWriteFrames);
    }
}

Resampler::~Resampler() = default;
Resampler::Resampler(Resampler&&) noexcept = default;
Resampler& Resampler::operator=(Resampler&&) noexcept = default;

AudioBufferView Resampler::inputBlock() noexcept
{
    return m_input.view();
}

void Resampler::write(std::size_t frameCount) noexcept SESSIONFLOW_NONBLOCKING
{
    assert(frameCount <= inputBlockFrames);
    const AudioBufferView input = m_input.view();

    std::size_t produced = 0;
    for (std::size_t index = 0; index < m_channels.size(); ++index)
    {
        Channel& channel = m_channels[index];
        std::ranges::copy(input.channel(index).first(frameCount), channel.input.begin());

        double* converted = nullptr;
        produced = static_cast<std::size_t>(
            channel.resampler->process(channel.input.data(), static_cast<int>(frameCount), converted));
        assert(m_available + produced <= channel.output.size());
        std::transform(converted, converted + produced,
                       channel.output.begin() + static_cast<std::ptrdiff_t>(m_available),
                       [](double sample) { return static_cast<float>(sample); });
    }
    m_available += produced;
}

std::size_t Resampler::availableFrames() const noexcept
{
    return m_available;
}

std::size_t Resampler::read(AudioBufferView output) noexcept SESSIONFLOW_NONBLOCKING
{
    assert(output.channelCount() == m_channels.size());
    const std::size_t count = std::min(m_available, output.frameCount());
    for (std::size_t index = 0; index < m_channels.size(); ++index)
    {
        std::vector<float>& buffered = m_channels[index].output;
        const auto begin = buffered.begin();
        std::copy(begin, begin + static_cast<std::ptrdiff_t>(count), output.channel(index).begin());
        std::copy(begin + static_cast<std::ptrdiff_t>(count), begin + static_cast<std::ptrdiff_t>(m_available), begin);
    }
    m_available -= count;
    return count;
}

void Resampler::reset() noexcept SESSIONFLOW_NONBLOCKING
{
    for (Channel& channel : m_channels)
    {
        channel.resampler->clear();
    }
    m_available = 0;
}

} // namespace sessionflow
