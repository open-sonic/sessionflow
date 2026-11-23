#pragma once

#include "core/audio_buffer.hpp"
#include "core/midi_event.hpp"
#include "core/realtime.hpp"
#include "core/time.hpp"

#include <cstddef>
#include <span>

namespace sessionflow
{

struct ProcessSetup
{
    double sampleRate = 48000.0;
    std::size_t maxBlockSize = 512;
};

struct TransportState
{
    SampleCount position = 0;
    double tempo = 120.0;
    bool playing = false;
};

struct ProcessBlock
{
    AudioBufferView audio;
    AudioBufferView sidechain;
    std::span<const MidiEvent> midi;
    TransportState transport;
};

// A node in the audio graph. Hosted plugins, built-in devices, clip players and mixer
// stages all implement this, so the graph treats them the same.
class Processor
{
  public:
    virtual ~Processor() = default;

    // Called off the audio thread before the first process() and after every setup change.
    // May allocate.
    virtual void prepare(const ProcessSetup& setup) = 0;

    // Processes block.audio in place. block.sidechain is empty when nothing is routed to it.
    // block.midi is sorted by sampleOffset.
    virtual void process(const ProcessBlock& block) noexcept SESSIONFLOW_NONBLOCKING = 0;

    // Delay in samples this processor adds, for plugin delay compensation.
    virtual SampleCount latency() const noexcept
    {
        return 0;
    }
};

} // namespace sessionflow
