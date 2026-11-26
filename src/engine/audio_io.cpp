#include "engine/audio_io.hpp"

#include "engine/device_block.hpp"

#include <RtAudio.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <string_view>
#include <thread>

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
#endif

namespace sessionflow
{

namespace
{

// SCHED_RR priority for the callback thread where RtAudio creates it (ALSA, PulseAudio).
// Without permission for real-time scheduling RtAudio falls back to a normal thread.
constexpr int s_realtimePriority = 70;

// After a driver error, a driver thread may still be changing the stream: CoreAudio, JACK
// and ASIO close a disconnected stream from inside their error report, and WASAPI's stream
// thread sets its state after reporting. RtAudio's stream state isn't atomic and there is
// nothing to wait on, so stop() leaves the stream alone this long first.
constexpr std::chrono::milliseconds s_driverCloseGrace{200};

// Denormal floats make recursive filters and reverb tails many times slower, which is enough
// to miss a 64-frame deadline. Flushes them to zero for one callback and restores the mode
// afterwards, because the driver owns the thread.
class ScopedFlushDenormals
{
  public:
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
    ScopedFlushDenormals() noexcept : m_saved(_mm_getcsr())
    {
        constexpr unsigned int flushToZero = 0x8000;
        constexpr unsigned int denormalsAreZero = 0x0040;
        _mm_setcsr(m_saved | flushToZero | denormalsAreZero);
    }

    ~ScopedFlushDenormals()
    {
        _mm_setcsr(m_saved);
    }

  private:
    unsigned int m_saved;
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
    ScopedFlushDenormals() noexcept
    {
        constexpr std::uint64_t flushToZero = std::uint64_t{1} << 24;
        __asm__ volatile("mrs %0, fpcr" : "=r"(m_saved));
        __asm__ volatile("msr fpcr, %0" : : "r"(m_saved | flushToZero));
    }

    ~ScopedFlushDenormals()
    {
        __asm__ volatile("msr fpcr, %0" : : "r"(m_saved));
    }

  private:
    std::uint64_t m_saved = 0;
#endif

  public:
    ScopedFlushDenormals(const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator=(const ScopedFlushDenormals&) = delete;
};

// Keeps the first error the driver reports. Lock-free because the driver may report from
// its audio thread, for example when WASAPI loses the device.
class DriverError
{
  public:
    void report(std::string_view text) noexcept
    {
        bool expected = false;
        if (!m_claimed.compare_exchange_strong(expected, true, std::memory_order_acquire))
        {
            return;
        }
        m_length = std::min(text.size(), m_text.size());
        std::copy_n(text.data(), m_length, m_text.data());
        m_ready.store(true, std::memory_order_release);
    }

    bool hasError() const noexcept
    {
        return m_ready.load(std::memory_order_acquire);
    }

    std::string text(std::string_view fallback) const
    {
        if (!m_ready.load(std::memory_order_acquire))
        {
            return std::string(fallback);
        }
        return std::string(m_text.data(), m_length);
    }

    // Only while no driver thread runs, that is before the stream starts.
    void reset() noexcept
    {
        m_ready.store(false, std::memory_order_relaxed);
        m_claimed.store(false, std::memory_order_release);
    }

  private:
    std::atomic<bool> m_claimed{false};
    std::atomic<bool> m_ready{false};
    std::array<char, 256> m_text{};
    std::size_t m_length = 0;
};

// 0 when no such device exists or it has no channels in that direction.
unsigned int findDevice(RtAudio& audio, const std::string& name, bool isOutput)
{
    if (name.empty())
    {
        return isOutput ? audio.getDefaultOutputDevice() : audio.getDefaultInputDevice();
    }
    for (const unsigned int id : audio.getDeviceIds())
    {
        const RtAudio::DeviceInfo info = audio.getDeviceInfo(id);
        if (info.name == name && (isOutput ? info.outputChannels : info.inputChannels) > 0)
        {
            return id;
        }
    }
    return 0;
}

std::string deviceNotFound(const std::string& name, const std::string& direction)
{
    if (name.empty())
    {
        return "no default audio " + direction + " device found";
    }
    return "audio " + direction + " device \"" + name + "\" not found";
}

} // namespace

struct AudioIo::Stream
{
    explicit Stream(RtAudio::Api api)
        : rtAudio(api, [this](RtAudioErrorType type, const std::string& text) { onDriverError(type, text); })
    {
    }

    void onDriverError(RtAudioErrorType type, std::string_view text) noexcept
    {
        if (type == RTAUDIO_NO_ERROR || type == RTAUDIO_WARNING)
        {
            return;
        }
        error.report(text);
    }

    static int onAudio(void* output, void* input, unsigned int frameCount, double, RtAudioStreamStatus status,
                       void* userData) noexcept SESSIONFLOW_NONBLOCKING
    {
        Stream& stream = *static_cast<Stream*>(userData);
        if (status != 0)
        {
            stream.xruns.fetch_add(1, std::memory_order_relaxed);
        }
        const ScopedFlushDenormals flushDenormals;
        stream.renderer->render(static_cast<float*>(output), static_cast<float*>(input), frameCount);
        return 0;
    }

    std::atomic<std::uint64_t> xruns{0};
    DriverError error;
    std::optional<DeviceBlockRenderer> renderer;

    // Declared last so it is destroyed first: the driver must be gone before the state its
    // threads point at.
    RtAudio rtAudio;
};

AudioIo::AudioIo() = default;

AudioIo::~AudioIo()
{
    stop();
}

std::optional<AudioStreamInfo> AudioIo::start(const AudioStreamConfig& config, AudioCallback& callback)
{
    stop();

    const auto fail = [this](std::string message)
    {
        m_lastError = std::move(message);
        spdlog::warn("audio: {}", m_lastError);
        return std::nullopt;
    };

    RtAudio::Api api = RtAudio::UNSPECIFIED;
    if (!config.api.empty())
    {
        api = RtAudio::getCompiledApiByDisplayName(config.api);
        if (api == RtAudio::UNSPECIFIED)
        {
            return fail("unknown audio API \"" + config.api + "\"");
        }
    }
    if (config.outputChannels == 0 && config.inputChannels == 0)
    {
        return fail("a stream needs at least one input or output channel");
    }
    if (config.sampleRate == 0 || config.bufferFrames == 0)
    {
        return fail("sample rate and buffer size must be positive, got " + std::to_string(config.sampleRate) +
                    " Hz and " + std::to_string(config.bufferFrames) + " frames");
    }

    auto stream = std::make_unique<Stream>(api);
    RtAudio& audio = stream->rtAudio;

    RtAudio::StreamParameters output;
    output.nChannels = config.outputChannels;
    output.deviceId = config.outputChannels > 0 ? findDevice(audio, config.outputDevice, true) : 0;
    if (config.outputChannels > 0 && output.deviceId == 0)
    {
        return fail(deviceNotFound(config.outputDevice, "output"));
    }

    RtAudio::StreamParameters input;
    input.nChannels = config.inputChannels;
    input.deviceId = config.inputChannels > 0 ? findDevice(audio, config.inputDevice, false) : 0;
    if (config.inputChannels > 0 && input.deviceId == 0)
    {
        return fail(deviceNotFound(config.inputDevice, "input"));
    }

    RtAudio::StreamOptions options;
    options.flags = RTAUDIO_NONINTERLEAVED | RTAUDIO_SCHEDULE_REALTIME;
    options.priority = s_realtimePriority;
    options.streamName = "Sessionflow";

    unsigned int bufferFrames = config.bufferFrames;
    const RtAudioErrorType opened =
        audio.openStream(config.outputChannels > 0 ? &output : nullptr, config.inputChannels > 0 ? &input : nullptr,
                         RTAUDIO_FLOAT32, config.sampleRate, &bufferFrames, &Stream::onAudio, stream.get(), &options);
    if (opened != RTAUDIO_NO_ERROR)
    {
        return fail(audio.getErrorText());
    }

    const unsigned int sampleRate = audio.getStreamSampleRate();
    stream->renderer.emplace(callback, config.inputChannels, config.outputChannels, bufferFrames);
    callback.prepare({.sampleRate = static_cast<double>(sampleRate), .maxBlockSize = bufferFrames});
    stream->error.reset();

    if (audio.startStream() != RTAUDIO_NO_ERROR)
    {
        std::string message = audio.getErrorText();
        audio.closeStream();
        return fail(std::move(message));
    }

    AudioStreamInfo info{
        .api = RtAudio::getApiDisplayName(audio.getCurrentApi()),
        .outputDeviceName = output.deviceId != 0 ? audio.getDeviceInfo(output.deviceId).name : std::string(),
        .inputDeviceName = input.deviceId != 0 ? audio.getDeviceInfo(input.deviceId).name : std::string(),
        .outputChannels = config.outputChannels,
        .inputChannels = config.inputChannels,
        .sampleRate = sampleRate,
        .bufferFrames = bufferFrames,
        .latencyFrames = audio.getStreamLatency(),
    };
    m_stream = std::move(stream);
    m_lastError.clear();
    spdlog::info("audio: {} on \"{}\", {} Hz, {} frames", info.api, info.outputDeviceName, info.sampleRate,
                 info.bufferFrames);
    return info;
}

void AudioIo::stop()
{
    if (!m_stream)
    {
        return;
    }
    if (m_stream->error.hasError())
    {
        std::this_thread::sleep_for(s_driverCloseGrace);
    }

    RtAudio& audio = m_stream->rtAudio;
    if (audio.isStreamRunning())
    {
        audio.stopStream();
    }
    if (audio.isStreamOpen())
    {
        audio.closeStream();
    }
    m_stream.reset();
}

bool AudioIo::poll()
{
    if (!m_stream)
    {
        return false;
    }
    // Every way a running stream dies goes through the driver's error report: a disconnect on
    // CoreAudio, JACK and ASIO, and the end of WASAPI's stream thread. Only our own flag is
    // read here, because a driver thread may still be changing RtAudio's stream state.
    if (!m_stream->error.hasError())
    {
        return false;
    }

    std::string message = m_stream->error.text("the audio driver stopped the stream");
    stop();
    m_lastError = std::move(message);
    spdlog::warn("audio: {}", m_lastError);
    return true;
}

bool AudioIo::isRunning() const noexcept
{
    return m_stream != nullptr && !m_stream->error.hasError();
}

std::uint64_t AudioIo::xrunCount() const noexcept
{
    return m_stream == nullptr ? 0 : m_stream->xruns.load(std::memory_order_relaxed);
}

} // namespace sessionflow
