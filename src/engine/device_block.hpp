#pragma once

#include "core/realtime.hpp"
#include "engine/audio_callback.hpp"

#include <cstddef>
#include <vector>

namespace sessionflow
{

// Turns one driver callback into AudioCallback::process() calls. The driver buffers are
// non-interleaved: channel c starts at c * frameCount. Construct it off the audio thread.
class DeviceBlockRenderer
{
  public:
    DeviceBlockRenderer(AudioCallback& callback, std::size_t inputChannels, std::size_t outputChannels,
                        std::size_t maxBlockFrames);

    // Silences output, then hands the block to the callback in pieces of at most
    // maxBlockFrames. Some drivers deliver more frames than they promised at open time.
    void render(float* output, float* input, std::size_t frameCount) noexcept SESSIONFLOW_NONBLOCKING;

  private:
    AudioCallback* m_callback;
    std::vector<float*> m_inputChannels;
    std::vector<float*> m_outputChannels;
    std::size_t m_maxBlockFrames;
};

} // namespace sessionflow
