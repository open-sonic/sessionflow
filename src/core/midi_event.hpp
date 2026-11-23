#pragma once

#include <array>
#include <cstdint>

namespace sessionflow
{

// A short MIDI 1.0 message at a sample position inside the current block. Fixed size so
// events can live in preallocated arrays and lock-free queues on the audio thread.
struct MidiEvent
{
    std::uint32_t sampleOffset = 0;
    std::array<std::uint8_t, 3> data{};

    bool operator==(const MidiEvent&) const = default;
};

} // namespace sessionflow
