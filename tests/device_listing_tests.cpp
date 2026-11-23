#include "engine/audio_device.hpp"
#include "midi/midi_inputs.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace sessionflow;

// CI runners have no audio or MIDI hardware, so these only check that the backends link and run.

TEST_CASE("at least one audio API is compiled in")
{
    CHECK_FALSE(compiledAudioApis().empty());
}

TEST_CASE("listing audio devices works without hardware")
{
    CHECK_NOTHROW(listAudioDevices());
}

TEST_CASE("listing MIDI inputs works without hardware")
{
    CHECK_NOTHROW(listMidiInputs());
}
