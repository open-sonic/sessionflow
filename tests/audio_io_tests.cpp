#include "engine/audio_device.hpp"
#include "engine/audio_io.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

using namespace sessionflow;

namespace
{

class CountingCallback final : public AudioCallback
{
  public:
    void prepare(const ProcessSetup& setup) override
    {
        maxBlockSize = setup.maxBlockSize;
        prepareCount.fetch_add(1);
    }

    void process(AudioBufferView, AudioBufferView output) noexcept override
    {
        if (output.frameCount() > maxBlockSize)
        {
            oversizedBlocks.fetch_add(1, std::memory_order_relaxed);
        }
        blocks.fetch_add(1, std::memory_order_relaxed);
    }

    // Waits for count more blocks, so a test knows the stream is really running.
    bool waitForBlocks(int count) const
    {
        const int target = blocks.load() + count;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (blocks.load() < target)
        {
            if (std::chrono::steady_clock::now() > deadline)
            {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return true;
    }

    std::size_t maxBlockSize = 0;
    std::atomic<int> prepareCount{0};
    std::atomic<int> blocks{0};
    std::atomic<int> oversizedBlocks{0};
};

bool hasDefaultOutput()
{
    const std::vector<AudioDeviceInfo> devices = listAudioDevices();
    return std::ranges::any_of(devices, [](const AudioDeviceInfo& device) { return device.isDefaultOutput; });
}

} // namespace

TEST_CASE("starting an unknown audio API fails with its name in the error")
{
    AudioIo audio;
    CountingCallback callback;

    CHECK_FALSE(audio.start({.api = "No Such API"}, callback).has_value());

    CHECK(audio.lastError().find("No Such API") != std::string::npos);
    CHECK_FALSE(audio.isRunning());
    CHECK(callback.prepareCount == 0);
}

TEST_CASE("a stream without input and output channels is rejected")
{
    AudioIo audio;
    CountingCallback callback;

    CHECK_FALSE(audio.start({.outputChannels = 0, .inputChannels = 0}, callback).has_value());
    CHECK_FALSE(audio.lastError().empty());
}

TEST_CASE("a device ID that does not exist fails without starting")
{
    AudioIo audio;
    CountingCallback callback;

    CHECK_FALSE(audio.start({.outputDevice = 999999}, callback).has_value());
    CHECK_FALSE(audio.lastError().empty());
    CHECK_FALSE(audio.isRunning());
}

TEST_CASE("stopping and polling a closed stream do nothing")
{
    AudioIo audio;

    audio.stop();

    CHECK_FALSE(audio.poll());
    CHECK_FALSE(audio.isRunning());
    CHECK(audio.xrunCount() == 0);
}

TEST_CASE("listing devices of an unknown API returns nothing")
{
    CHECK(listAudioDevices("No Such API").empty());
}

// Hidden from ctest because CI runners have no audio hardware. Run them on a machine with
// speakers or headphones: sessionflow_tests "[hardware]". They play silence.

TEST_CASE("a running stream calls back with blocks no larger than prepared", "[.hardware]")
{
    REQUIRE(hasDefaultOutput());
    AudioIo audio;
    CountingCallback callback;

    const std::optional<AudioStreamInfo> info = audio.start({.bufferFrames = 64}, callback);

    REQUIRE(info.has_value());
    CHECK(audio.isRunning());
    CHECK(callback.waitForBlocks(50));
    CHECK(callback.oversizedBlocks == 0);
    audio.stop();
    CHECK_FALSE(audio.isRunning());
}

TEST_CASE("restarting with other buffer sizes while running keeps the stream alive", "[.hardware]")
{
    REQUIRE(hasDefaultOutput());
    AudioIo audio;
    CountingCallback callback;

    for (const unsigned int bufferFrames : {64u, 512u, 128u, 1024u, 64u, 256u, 64u})
    {
        INFO("buffer size " << bufferFrames);
        REQUIRE(audio.start({.bufferFrames = bufferFrames}, callback).has_value());
        CHECK(callback.waitForBlocks(10));
    }

    CHECK(callback.oversizedBlocks == 0);
    CHECK_FALSE(audio.poll());
}

TEST_CASE("switching through every output device of every API never crashes", "[.hardware]")
{
    AudioIo audio;
    CountingCallback callback;
    int started = 0;

    for (const std::string& api : compiledAudioApis())
    {
        for (const AudioDeviceInfo& device : listAudioDevices(api))
        {
            if (device.outputChannels < 2)
            {
                continue;
            }
            INFO(api << " device " << device.id << " " << device.name);
            // Devices may be busy or refuse the format. That has to fail cleanly, not crash.
            if (audio.start({.api = api, .outputDevice = device.id, .bufferFrames = 128}, callback))
            {
                CHECK(callback.waitForBlocks(5));
                ++started;
            }
            else
            {
                CHECK_FALSE(audio.lastError().empty());
            }
        }
    }

    CHECK(started > 0);
}

TEST_CASE("no callback runs after stop returns", "[.hardware]")
{
    REQUIRE(hasDefaultOutput());
    AudioIo audio;
    CountingCallback callback;
    REQUIRE(audio.start({}, callback).has_value());
    REQUIRE(callback.waitForBlocks(5));

    audio.stop();
    const int blocksAtStop = callback.blocks.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    CHECK(callback.blocks.load() == blocksAtStop);
}
