#include "engine/device_block.hpp"

#include <algorithm>
#include <cassert>

namespace sessionflow
{

namespace
{

void pointChannels(std::vector<float*>& channels, float* buffer, std::size_t frameCount) noexcept
{
    for (std::size_t index = 0; index < channels.size(); ++index)
    {
        channels[index] = buffer == nullptr ? nullptr : buffer + index * frameCount;
    }
}

} // namespace

DeviceBlockRenderer::DeviceBlockRenderer(AudioCallback& callback, std::size_t inputChannels, std::size_t outputChannels,
                                         std::size_t maxBlockFrames)
    : m_callback(&callback), m_inputChannels(inputChannels), m_outputChannels(outputChannels),
      m_maxBlockFrames(maxBlockFrames)
{
    assert(maxBlockFrames > 0);
}

void DeviceBlockRenderer::render(float* output, float* input, std::size_t frameCount) noexcept SESSIONFLOW_NONBLOCKING
{
    pointChannels(m_outputChannels, output, frameCount);
    pointChannels(m_inputChannels, input, frameCount);

    const std::size_t outputChannels = output == nullptr ? 0 : m_outputChannels.size();
    const std::size_t inputChannels = input == nullptr ? 0 : m_inputChannels.size();
    const AudioBufferView outputView(m_outputChannels.data(), outputChannels, frameCount);
    const AudioBufferView inputView(m_inputChannels.data(), inputChannels, frameCount);
    outputView.clear();

    for (std::size_t start = 0; start < frameCount; start += m_maxBlockFrames)
    {
        const std::size_t count = std::min(m_maxBlockFrames, frameCount - start);
        m_callback->process(inputView.slice(start, count), outputView.slice(start, count));
    }
}

} // namespace sessionflow
