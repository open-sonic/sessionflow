#include "engine/device_block.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

using namespace sessionflow;

namespace
{

// Copies input channel c to output channel c and adds a running frame number, so a test can
// see which driver frame each output sample came from.
class PassThrough final : public AudioCallback
{
  public:
    void prepare(const ProcessSetup&) override
    {
    }

    void process(AudioBufferView input, AudioBufferView output) noexcept override
    {
        blockSizes.push_back(output.frameCount());
        inputChannelCounts.push_back(input.channelCount());

        for (std::size_t channel = 0; channel < output.channelCount(); ++channel)
        {
            const std::span<float> out = output.channel(channel);
            wasSilent = wasSilent && std::ranges::all_of(out, [](float sample) { return sample == 0.0f; });
            for (std::size_t frame = 0; frame < out.size(); ++frame)
            {
                const float in = channel < input.channelCount() ? input.channel(channel)[frame] : 0.0f;
                out[frame] = in + static_cast<float>(m_framesSeen + frame);
            }
        }
        m_framesSeen += output.frameCount();
    }

    std::vector<std::size_t> blockSizes;
    std::vector<std::size_t> inputChannelCounts;
    bool wasSilent = true;

  private:
    std::size_t m_framesSeen = 0;
};

} // namespace

TEST_CASE("output reaches the callback silent")
{
    PassThrough callback;
    DeviceBlockRenderer renderer(callback, 0, 2, 64);
    std::vector<float> output(2 * 64, 0.5f);

    renderer.render(output.data(), nullptr, 64);

    CHECK(callback.wasSilent);
}

TEST_CASE("non-interleaved driver channels arrive as separate channels")
{
    constexpr std::size_t frames = 8;
    PassThrough callback;
    DeviceBlockRenderer renderer(callback, 2, 2, frames);
    std::vector<float> input(2 * frames);
    std::fill_n(input.begin(), frames, 100.0f);
    std::fill_n(input.begin() + frames, frames, 200.0f);
    std::vector<float> output(2 * frames);

    renderer.render(output.data(), input.data(), frames);

    // Channel offset from the input plus the frame number from PassThrough.
    CHECK(output[0] == 100.0f);
    CHECK(output[frames - 1] == 107.0f);
    CHECK(output[frames] == 200.0f);
    CHECK(output[2 * frames - 1] == 207.0f);
}

TEST_CASE("a driver block larger than promised arrives in pieces of the prepared size")
{
    PassThrough callback;
    DeviceBlockRenderer renderer(callback, 0, 1, 64);
    std::vector<float> output(150);

    renderer.render(output.data(), nullptr, 150);

    CHECK(callback.blockSizes == std::vector<std::size_t>{64, 64, 22});
    for (std::size_t frame = 0; frame < output.size(); ++frame)
    {
        REQUIRE(output[frame] == static_cast<float>(frame));
    }
}

TEST_CASE("an output-only stream gives the callback no input channels")
{
    PassThrough callback;
    DeviceBlockRenderer renderer(callback, 2, 2, 32);
    std::vector<float> output(2 * 32);

    renderer.render(output.data(), nullptr, 32);

    CHECK(callback.inputChannelCounts == std::vector<std::size_t>{0});
}
