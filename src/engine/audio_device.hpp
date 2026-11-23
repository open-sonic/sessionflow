#pragma once

#include <string>
#include <vector>

namespace sessionflow
{

struct AudioDeviceInfo
{
    unsigned int id = 0;
    std::string name;
    unsigned int inputChannels = 0;
    unsigned int outputChannels = 0;
    bool isDefaultOutput = false;
};

// Driver APIs compiled into this build, such as ASIO, WASAPI, CoreAudio or ALSA.
std::vector<std::string> compiledAudioApis();

// Devices of the default API. Empty on machines without audio hardware.
std::vector<AudioDeviceInfo> listAudioDevices();

} // namespace sessionflow
