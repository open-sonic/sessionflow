#pragma once

#include "core/audio_buffer.hpp"
#include "core/realtime.hpp"
#include "engine/processor.hpp"

namespace sessionflow
{

// Receives the device audio. The engine implements this once and runs its graph inside.
class AudioCallback
{
  public:
    virtual ~AudioCallback() = default;

    // Called on the control thread before the stream starts and again before every restart.
    // setup.maxBlockSize bounds the frame count of every process() call. May allocate.
    virtual void prepare(const ProcessSetup& setup) = 0;

    // Called on the audio thread. output arrives silent. input has no channels when the
    // stream was opened without input.
    virtual void process(AudioBufferView input, AudioBufferView output) noexcept SESSIONFLOW_NONBLOCKING = 0;
};

} // namespace sessionflow
