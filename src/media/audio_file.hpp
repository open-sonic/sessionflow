#pragma once

#include "core/audio_buffer.hpp"

#include <filesystem>
#include <optional>

namespace sessionflow
{

struct DecodedAudio
{
    AudioBuffer samples;
    double sampleRate = 0.0;
};

// Reads a whole WAV, FLAC or MP3 file into memory. Blocking, never call it on the audio thread.
// Returns nothing for unknown extensions and unreadable files.
std::optional<DecodedAudio> decodeAudioFile(const std::filesystem::path& path);

} // namespace sessionflow
