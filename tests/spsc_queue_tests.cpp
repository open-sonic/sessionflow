#include "core/midi_event.hpp"
#include "core/spsc_queue.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <thread>

using namespace sessionflow;

TEST_CASE("queue pops in push order")
{
    SpscQueue<int, 4> queue;

    CHECK(queue.tryPush(1));
    CHECK(queue.tryPush(2));

    CHECK(queue.tryPop() == 1);
    CHECK(queue.tryPop() == 2);
    CHECK_FALSE(queue.tryPop().has_value());
}

TEST_CASE("queue rejects pushes when full")
{
    SpscQueue<int, 2> queue;

    CHECK(queue.tryPush(1));
    CHECK(queue.tryPush(2));
    CHECK_FALSE(queue.tryPush(3));

    CHECK(queue.tryPop() == 1);
    CHECK(queue.tryPush(3));
}

TEST_CASE("queue carries MIDI events")
{
    SpscQueue<MidiEvent, 8> queue;
    const MidiEvent noteOn{.sampleOffset = 12, .data = {0x90, 60, 100}};

    CHECK(queue.tryPush(noteOn));

    CHECK(queue.tryPop() == noteOn);
}

TEST_CASE("queue keeps order across threads")
{
    constexpr int count = 100000;
    auto queue = std::make_unique<SpscQueue<int, 1024>>();

    std::thread producer(
        [&]
        {
            for (int value = 0; value < count; ++value)
            {
                while (!queue->tryPush(value))
                {
                    std::this_thread::yield();
                }
            }
        });

    int expected = 0;
    bool inOrder = true;
    while (expected < count)
    {
        if (const std::optional<int> value = queue->tryPop())
        {
            inOrder = inOrder && *value == expected;
            ++expected;
        }
    }
    producer.join();

    CHECK(inOrder);
}
