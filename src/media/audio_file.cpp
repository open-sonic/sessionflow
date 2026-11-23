#include "media/audio_file.hpp"

#define DR_WAV_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION
#define DR_MP3_IMPLEMENTATION
#include <dr_flac.h>
#include <dr_mp3.h>
#include <dr_wav.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace sessionflow
{

namespace
{

std::string lowercaseExtension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

// Reading through std::ifstream keeps non-ASCII paths working on Windows.
std::optional<std::vector<char>> readFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return std::nullopt;
    }
    return std::vector<char>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::optional<DecodedAudio> deinterleave(const float* samples, unsigned int channels, unsigned int sampleRate,
                                         std::uint64_t frameCount)
{
    if (samples == nullptr || channels == 0)
    {
        return std::nullopt;
    }

    const auto frames = static_cast<std::size_t>(frameCount);
    DecodedAudio decoded{AudioBuffer(channels, frames), static_cast<double>(sampleRate)};
    const AudioBufferView view = decoded.samples.view();
    for (std::size_t channel = 0; channel < channels; ++channel)
    {
        const std::span<float> destination = view.channel(channel);
        for (std::size_t frame = 0; frame < frames; ++frame)
        {
            destination[frame] = samples[frame * channels + channel];
        }
    }
    return decoded;
}

std::optional<DecodedAudio> decodeWav(const std::vector<char>& bytes)
{
    unsigned int channels = 0;
    unsigned int sampleRate = 0;
    drwav_uint64 frames = 0;
    float* samples =
        drwav_open_memory_and_read_pcm_frames_f32(bytes.data(), bytes.size(), &channels, &sampleRate, &frames, nullptr);
    std::optional<DecodedAudio> decoded = deinterleave(samples, channels, sampleRate, frames);
    drwav_free(samples, nullptr);
    return decoded;
}

std::optional<DecodedAudio> decodeFlac(const std::vector<char>& bytes)
{
    unsigned int channels = 0;
    unsigned int sampleRate = 0;
    drflac_uint64 frames = 0;
    float* samples = drflac_open_memory_and_read_pcm_frames_f32(bytes.data(), bytes.size(), &channels, &sampleRate,
                                                                &frames, nullptr);
    std::optional<DecodedAudio> decoded = deinterleave(samples, channels, sampleRate, frames);
    drflac_free(samples, nullptr);
    return decoded;
}

std::optional<DecodedAudio> decodeMp3(const std::vector<char>& bytes)
{
    drmp3_config config{};
    drmp3_uint64 frames = 0;
    float* samples = drmp3_open_memory_and_read_pcm_frames_f32(bytes.data(), bytes.size(), &config, &frames, nullptr);
    std::optional<DecodedAudio> decoded = deinterleave(samples, config.channels, config.sampleRate, frames);
    drmp3_free(samples, nullptr);
    return decoded;
}

} // namespace

std::optional<DecodedAudio> decodeAudioFile(const std::filesystem::path& path)
{
    const std::string extension = lowercaseExtension(path);
    if (extension != ".wav" && extension != ".flac" && extension != ".mp3")
    {
        return std::nullopt;
    }

    const std::optional<std::vector<char>> bytes = readFile(path);
    if (!bytes)
    {
        return std::nullopt;
    }

    if (extension == ".wav")
    {
        return decodeWav(*bytes);
    }
    if (extension == ".flac")
    {
        return decodeFlac(*bytes);
    }
    return decodeMp3(*bytes);
}

} // namespace sessionflow
