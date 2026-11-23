#pragma once

#include "core/audio_buffer.hpp"
#include "core/realtime.hpp"

#include <cstddef>
#include <vector>

namespace sessionflow
{

// Converts planar audio between two sample rates with r8brain. Construct it off the audio
// thread. inputBlock(), write(), read() and reset() are real-time safe.
//
// Pull model for playback: while availableFrames() is below what the block needs, fill
// inputBlock() from the source and write() it, then read() the block.
class Resampler
{
  public:
    // maxReadFrames is the largest block read() will be asked for.
    Resampler(std::size_t channelCount, double sourceRate, double targetRate, std::size_t maxReadFrames);
    ~Resampler();

    Resampler(Resampler&&) noexcept;
    Resampler& operator=(Resampler&&) noexcept;

    // Scratch buffer for the next source frames, always inputBlockFrames long.
    AudioBufferView inputBlock() noexcept;

    // Converts the first frameCount frames of inputBlock(). Call it only while
    // availableFrames() is below maxReadFrames, which bounds the internal buffer.
    void write(std::size_t frameCount) noexcept SESSIONFLOW_NONBLOCKING;

    std::size_t availableFrames() const noexcept;

    // Moves up to output.frameCount() converted frames into output and returns how many.
    std::size_t read(AudioBufferView output) noexcept SESSIONFLOW_NONBLOCKING;

    // Drops buffered audio and filter state, for example after a seek.
    void reset() noexcept SESSIONFLOW_NONBLOCKING;

    static constexpr std::size_t inputBlockFrames = 256;

  private:
    struct Channel;

    std::vector<Channel> m_channels;
    AudioBuffer m_input;
    std::size_t m_available = 0;
};

} // namespace sessionflow
