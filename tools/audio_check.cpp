// Plays a quiet sine on an audio device and reports whether every block was ready in time.
// Manual hardware check for the device layer, not part of the test suite.

#include "engine/audio_device.hpp"
#include "engine/audio_io.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{

using namespace sessionflow;
using Clock = std::chrono::steady_clock;

constexpr double s_frequency = 440.0;
constexpr float s_amplitude = 0.1f; // -20 dBFS, audible without being loud
constexpr unsigned int s_switchBufferSizes[] = {64, 256, 128, 512, 1024};
constexpr std::chrono::seconds s_measureAfterSwitching{3};

constexpr const char* s_usage = R"(usage: sessionflow_audio_check [options]

  --list            list audio APIs and devices, then exit
  --api NAME        audio API by display name, default: the platform default
  --device NAME     output device name from --list, default: the default output
  --rate HZ         sample rate, default 48000
  --buffer FRAMES   buffer size, default 64
  --seconds N       how long to play, default 10
  --switch          reopen the stream with other buffer sizes while playing

A block is late when rendering it took longer than playing it. Late blocks and driver
xruns count as glitches. Exit code 0 without glitches, 1 with glitches, 2 on errors.
)";

struct Options
{
    AudioStreamConfig stream{.bufferFrames = 64};
    int seconds = 10;
    bool list = false;
    bool switchDevices = false;
};

std::int64_t nowNanoseconds() noexcept
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

class SineCheck final : public AudioCallback
{
  public:
    void prepare(const ProcessSetup& setup) override
    {
        m_sampleRate = setup.sampleRate;
        m_phaseStep = 2.0 * std::numbers::pi * s_frequency / setup.sampleRate;
    }

    void process(AudioBufferView, AudioBufferView output) noexcept override
    {
        const std::int64_t start = nowNanoseconds();

        for (std::size_t frame = 0; frame < output.frameCount(); ++frame)
        {
            const float sample = s_amplitude * static_cast<float>(std::sin(m_phase));
            m_phase = std::fmod(m_phase + m_phaseStep, 2.0 * std::numbers::pi);
            for (std::size_t channel = 0; channel < output.channelCount(); ++channel)
            {
                output.channel(channel)[frame] = sample;
            }
        }

        const std::int64_t end = nowNanoseconds();
        const double budgetNs = static_cast<double>(output.frameCount()) / m_sampleRate * 1e9;
        const double load = static_cast<double>(end - start) / budgetNs;
        if (load > peakLoad.load(std::memory_order_relaxed))
        {
            peakLoad.store(load, std::memory_order_relaxed);
        }
        if (load > 1.0)
        {
            lateBlocks.fetch_add(1, std::memory_order_relaxed);
        }
        frames.fetch_add(output.frameCount(), std::memory_order_relaxed);
    }

    // Only while no stream runs.
    void resetCounters() noexcept
    {
        frames.store(0);
        lateBlocks.store(0);
        peakLoad.store(0.0);
    }

    std::atomic<std::uint64_t> frames{0};
    std::atomic<std::uint64_t> lateBlocks{0};
    std::atomic<double> peakLoad{0.0};

  private:
    double m_sampleRate = 48000.0;
    double m_phase = 0.0;
    double m_phaseStep = 0.0;
};

std::optional<Options> parseOptions(int argc, char** argv)
{
    Options options;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument = argv[index];
        const bool hasValue = index + 1 < argc;
        if (argument == "--list")
        {
            options.list = true;
        }
        else if (argument == "--switch")
        {
            options.switchDevices = true;
        }
        else if (argument == "--api" && hasValue)
        {
            options.stream.api = argv[++index];
        }
        else if (argument == "--device" && hasValue)
        {
            options.stream.outputDevice = argv[++index];
        }
        else if (argument == "--rate" && hasValue)
        {
            options.stream.sampleRate = static_cast<unsigned int>(std::strtoul(argv[++index], nullptr, 10));
        }
        else if (argument == "--buffer" && hasValue)
        {
            options.stream.bufferFrames = static_cast<unsigned int>(std::strtoul(argv[++index], nullptr, 10));
        }
        else if (argument == "--seconds" && hasValue)
        {
            options.seconds = std::atoi(argv[++index]);
        }
        else
        {
            return std::nullopt;
        }
    }
    return options;
}

void listDevices()
{
    for (const std::string& api : compiledAudioApis())
    {
        std::printf("%s\n", api.c_str());
        for (const AudioDeviceInfo& device : listAudioDevices(api))
        {
            std::string rates;
            for (const unsigned int rate : device.sampleRates)
            {
                rates += (rates.empty() ? "" : ", ") + std::to_string(rate);
            }
            std::printf("  %s  in %u, out %u%s%s  rates: %s\n", device.name.c_str(), device.inputChannels,
                        device.outputChannels, device.isDefaultOutput ? ", default output" : "",
                        device.isDefaultInput ? ", default input" : "", rates.c_str());
        }
    }
}

void printStream(const AudioStreamInfo& info)
{
    std::printf("%s, \"%s\", %u Hz, %u frames (%.2f ms), driver latency %ld frames\n", info.api.c_str(),
                info.outputDeviceName.c_str(), info.sampleRate, info.bufferFrames,
                1000.0 * info.bufferFrames / info.sampleRate, info.latencyFrames);
}

} // namespace

int main(int argc, char** argv)
{
    const std::optional<Options> options = parseOptions(argc, argv);
    if (!options)
    {
        std::fputs(s_usage, stderr);
        return 2;
    }
    if (options->list)
    {
        listDevices();
        return 0;
    }

    AudioIo audio;
    SineCheck sine;
    AudioStreamConfig config = options->stream;
    std::optional<AudioStreamInfo> info = audio.start(config, sine);
    if (!info)
    {
        std::fprintf(stderr, "could not start: %s\n", audio.lastError().c_str());
        return 2;
    }
    printStream(*info);

    const auto end = Clock::now() + std::chrono::seconds(options->seconds);
    // The last stretch runs with the requested settings, so the glitch check measures them.
    const auto stopSwitching = end - s_measureAfterSwitching;
    auto nextSwitch = Clock::now() + std::chrono::milliseconds(500);
    bool isSwitching = options->switchDevices;
    std::size_t switchIndex = 0;
    int restarts = 0;
    while (Clock::now() < end)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (audio.poll())
        {
            std::fprintf(stderr, "stream lost and closed: %s\n", audio.lastError().c_str());
            return 2;
        }
        const auto now = Clock::now();
        if (isSwitching && now >= nextSwitch)
        {
            isSwitching = now < stopSwitching;
            config.bufferFrames = isSwitching ? s_switchBufferSizes[switchIndex++ % std::size(s_switchBufferSizes)]
                                              : options->stream.bufferFrames;
            nextSwitch = now + std::chrono::milliseconds(500);
            audio.stop();
            sine.resetCounters();
            info = audio.start(config, sine);
            if (!info)
            {
                std::fprintf(stderr, "restart %d failed: %s\n", restarts + 1, audio.lastError().c_str());
                return 2;
            }
            ++restarts;
        }
    }

    const std::uint64_t xruns = audio.xrunCount();
    audio.stop();

    if (options->switchDevices)
    {
        std::printf("restarts: %d, final stream: ", restarts);
        printStream(*info);
    }
    const std::uint64_t lateBlocks = sine.lateBlocks.load();
    std::printf("played %.2f s, peak callback load %.1f %%, late blocks %llu, driver xruns %llu\n",
                static_cast<double>(sine.frames.load()) / info->sampleRate, 100.0 * sine.peakLoad.load(),
                static_cast<unsigned long long>(lateBlocks), static_cast<unsigned long long>(xruns));

    if (lateBlocks > 0 || xruns > 0)
    {
        std::printf("glitches detected\n");
        return 1;
    }
    std::printf("no glitches\n");
    return 0;
}
