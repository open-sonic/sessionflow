#pragma once

#include "core/audio_buffer.hpp"
#include "core/time.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>

namespace sessionflow
{

namespace detail
{
struct AudioDecoder;
}

// Streams a WAV, FLAC or MP3 file from disk. Every call blocks on file I/O, so use it on a
// worker thread and hand the samples to the audio thread through a queue.
class AudioFileReader
{
  public:
    // Returns nothing for unknown extensions, unreadable files and streams the decoders reject.
    static std::optional<AudioFileReader> open(const std::filesystem::path& path);

    AudioFileReader(AudioFileReader&&) noexcept;
    AudioFileReader& operator=(AudioFileReader&&) noexcept;
    ~AudioFileReader();

    std::size_t channelCount() const noexcept;
    double sampleRate() const noexcept;
    SampleCount frameCount() const noexcept;

    bool seek(SampleCount frame);

    // Fills the destination from the current position and returns the frames read. Fewer than
    // requested means the file ended. The destination must have channelCount() channels.
    std::size_t read(AudioBufferView destination);

  private:
    explicit AudioFileReader(std::unique_ptr<detail::AudioDecoder> decoder) noexcept;

    std::unique_ptr<detail::AudioDecoder> m_decoder;
};

struct DecodedAudio
{
    AudioBuffer samples;
    double sampleRate = 0.0;
};

// Reads a whole WAV, FLAC or MP3 file into memory. Blocking, never call it on the audio thread.
// Returns nothing for unknown extensions and unreadable files.
std::optional<DecodedAudio> decodeAudioFile(const std::filesystem::path& path);

} // namespace sessionflow
