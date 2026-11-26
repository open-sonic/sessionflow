#pragma once

#include "engine/audio_callback.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace sessionflow
{

struct AudioStreamConfig
{
    std::string api{};             // display name from compiledAudioApis(), empty for the default API
    unsigned int outputDevice = 0; // 0 picks the default output
    unsigned int inputDevice = 0;  // 0 picks the default input
    unsigned int outputChannels = 2;
    unsigned int inputChannels = 0; // 0 opens the stream without input
    unsigned int sampleRate = 48000;
    unsigned int bufferFrames = 256;
};

// What the driver granted. It may round the buffer size, and some APIs resample internally.
struct AudioStreamInfo
{
    std::string api{};
    std::string outputDeviceName{};
    std::string inputDeviceName{};
    unsigned int outputChannels = 0;
    unsigned int inputChannels = 0;
    unsigned int sampleRate = 0;
    unsigned int bufferFrames = 0;
    long latencyFrames = 0; // as reported by the driver, 0 when it doesn't say
};

// Runs one RtAudio stream: ASIO or WASAPI on Windows, CoreAudio on macOS, JACK, ALSA or
// PulseAudio on Linux. All member functions belong to one control thread, usually the UI.
class AudioIo
{
  public:
    AudioIo();
    ~AudioIo();

    // The driver keeps a pointer to the stream state, so it must not move.
    AudioIo(const AudioIo&) = delete;
    AudioIo& operator=(const AudioIo&) = delete;

    // Stops the running stream, then opens and starts the configured devices. Changing the
    // device, sample rate or buffer size is another start(). callback must outlive the stream.
    // On failure nothing runs and lastError() says why.
    std::optional<AudioStreamInfo> start(const AudioStreamConfig& config, AudioCallback& callback);

    // Returns once the callback has finished for the last time. Safe to call when stopped.
    void stop();

    // Call regularly, for example once per UI frame. Notices a device that was unplugged or a
    // driver that stopped on its own, closes the stream and returns true once. Call start()
    // again to reopen, with the same or a fallback device.
    bool poll();

    bool isRunning() const noexcept;

    // Underruns and overflows the driver reported since start(). Not every API reports them.
    std::uint64_t xrunCount() const noexcept;

    const std::string& lastError() const noexcept
    {
        return m_lastError;
    }

  private:
    struct Stream;

    std::unique_ptr<Stream> m_stream;
    std::string m_lastError;
};

} // namespace sessionflow
