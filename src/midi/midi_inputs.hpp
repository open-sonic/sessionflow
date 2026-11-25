#pragma once

#include "midi/midi_input_collector.hpp"

#include <memory>
#include <string>
#include <vector>

namespace sessionflow
{

// Display names of the MIDI inputs currently connected. Empty when none are available.
std::vector<std::string> listMidiInputs();

// Opens every connected MIDI input, follows devices as they are plugged in and out, and feeds
// their messages into collector(). Construct and destroy it off the audio thread. Only
// collector().collect() may run on the audio thread.
class MidiInputs
{
  public:
    MidiInputs();
    ~MidiInputs();

    MidiInputs(const MidiInputs&) = delete;
    MidiInputs& operator=(const MidiInputs&) = delete;

    MidiInputCollector& collector() noexcept;

    // Display names of the inputs currently open.
    std::vector<std::string> openInputNames() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace sessionflow
