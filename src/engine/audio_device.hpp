#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace sessionflow
{

struct AudioDeviceInfo
{
    std::string name{};
    unsigned int inputChannels = 0;
    unsigned int outputChannels = 0;
    bool isDefaultOutput = false;
    bool isDefaultInput = false;
    std::vector<unsigned int> sampleRates{};
    // What the system runs the device at, for example the WASAPI mix format. 0 if unknown.
    unsigned int preferredSampleRate = 0;
};

// Driver APIs compiled into this build, such as ASIO, WASAPI, CoreAudio or ALSA, by display name.
std::vector<std::string> compiledAudioApis();

// Devices of the named API, or of the default API when api is empty. Empty on machines without
// audio hardware and for unknown API names. While an ASIO stream is open in this process,
// RtAudio cannot probe ASIO and the ASIO list is empty: list devices before starting and keep
// the result.
std::vector<AudioDeviceInfo> listAudioDevices(std::string_view api = {});

} // namespace sessionflow
