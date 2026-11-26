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

std::vector<AudioDeviceInfo> listAudioDevices(std::string_view api)
{
    RtAudio::Api rtApi = RtAudio::UNSPECIFIED;
    if (!api.empty())
    {
        rtApi = RtAudio::getCompiledApiByDisplayName(std::string(api));
        if (rtApi == RtAudio::UNSPECIFIED)
        {
            spdlog::warn("audio device: unknown audio API \"{}\"", api);
            return {};
        }
    }

    RtAudio audio(rtApi,
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
            .isDefaultInput = info.isDefaultInput,
            .sampleRates = info.sampleRates,
            .preferredSampleRate = info.preferredSampleRate,
        });
    }
    return devices;
}

} // namespace sessionflow
