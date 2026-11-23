#include "midi/midi_inputs.hpp"

#include <libremidi/libremidi.hpp>

namespace sessionflow
{

std::vector<std::string> listMidiInputs()
{
    const libremidi::observer observer;

    std::vector<std::string> names;
    for (const libremidi::input_port& port : observer.get_input_ports())
    {
        names.push_back(port.display_name.empty() ? port.port_name : port.display_name);
    }
    return names;
}

} // namespace sessionflow
