#include "core/audio_buffer.hpp"
#include "support/signals.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace sessionflow;

TEST_CASE("audio buffer starts silent with the requested shape")
{
    AudioBuffer buffer(2, 64);
    const AudioBufferView view = buffer.view();

    CHECK(view.channelCount() == 2);
    CHECK(view.frameCount() == 64);
    CHECK(test::peak(view) == 0.0f);
}

TEST_CASE("audio buffer channels don't overlap")
{
    AudioBuffer buffer(2, 8);
    const AudioBufferView view = buffer.view();

    std::ranges::fill(view.channel(0), 1.0f);

    CHECK(view.channel(0)[7] == 1.0f);
    CHECK(view.channel(1)[0] == 0.0f);
}

TEST_CASE("slice addresses a frame range of every channel")
{
    AudioBuffer buffer(2, 16);
    const AudioBufferView slice = buffer.view().slice(4, 8);

    test::fillConstant(slice, 0.5f);

    const AudioBufferView view = buffer.view();
    CHECK(slice.frameCount() == 8);
    CHECK(view.channel(1)[3] == 0.0f);
    CHECK(view.channel(1)[4] == 0.5f);
    CHECK(view.channel(1)[11] == 0.5f);
    CHECK(view.channel(1)[12] == 0.0f);
}

TEST_CASE("clear silences a view")
{
    AudioBuffer buffer(1, 32);
    test::fillSine(buffer.view(), 440.0, 48000.0);

    buffer.view().clear();

    CHECK(test::peak(buffer.view()) == 0.0f);
}
