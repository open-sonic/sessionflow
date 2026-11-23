#include "engine/audio_device.hpp"

#include <RtAudio.h>
#include <spdlog/spdlog.h>

namespace sessionflow
{

std::vector<std::string> compiledAudioApis()
{
    std::vector<RtAudio::Api> apis;
    RtAudio::getCompiledApi(apis);

    std::vector<std::string> names;
    for (const RtAudio::Api api : apis)
    {
        names.push_back(RtAudio::getApiDisplayName(api));
    }
    return names;
}

std::vector<AudioDeviceInfo> listAudioDevices()
{
    RtAudio audio(RtAudio::UNSPECIFIED,
                  [](RtAudioErrorType, const std::string& message) { spdlog::warn("audio device: {}", message); });

    std::vector<AudioDeviceInfo> devices;
    for (const unsigned int id : audio.getDeviceIds())
    {
        const RtAudio::DeviceInfo info = audio.getDeviceInfo(id);
        devices.push_back({
            .id = info.ID,
            .name = info.name,
            .inputChannels = info.inputChannels,
            .outputChannels = info.outputChannels,
            .isDefaultOutput = info.isDefaultOutput,
        });
    }
    return devices;
}

} // namespace sessionflow
