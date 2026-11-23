#pragma once

#include <string>
#include <vector>

namespace sessionflow
{

// Display names of the MIDI inputs currently connected. Empty when none are available.
std::vector<std::string> listMidiInputs();

} // namespace sessionflow
