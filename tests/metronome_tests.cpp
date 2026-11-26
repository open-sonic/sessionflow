#include "engine/metronome.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <vector>

using namespace sessionflow;

namespace
{

// The first sample of a click is exactly its level, every later sample is quieter.
constexpr float s_downbeatLevel = 0.5f;
constexpr float s_beatLevel = 0.35f;

struct Click
{
    SampleCount position = 0;
    bool isDownbeat = false;

    bool operator==(const Click&) const = default;
};

// Runs transport and metronome block by block and records each click with the timeline
// sample it started on.
class ClickRecorder
{
  public:
    ClickRecorder(double bpm, double sampleRate, std::size_t blockSize)
        : transport(
              std::make_unique<const TimeMap>(TempoMap{.tempos = {TempoEvent{.position = 0, .bpm = bpm}}}, sampleRate)),
          m_blockSize(blockSize), m_buffer(1, blockSize)
    {
        transport.prepare(blockSize);
        metronome.prepare(sampleRate);
    }

    void run(std::size_t blocks)
    {
        for (std::size_t block = 0; block < blocks; ++block)
        {
            const AudioBufferView output = m_buffer.view();
            output.clear();
            const std::span<const TransportSegment> segments = transport.advance(m_blockSize);
            metronome.render(output, segments, transport.timeMap());
            record(output.channel(0), segments);
        }
    }

    Transport transport;
    Metronome metronome;
    std::vector<Click> clicks;
    bool heardAnything = false;

  private:
    void record(std::span<const float> samples, std::span<const TransportSegment> segments)
    {
        for (std::size_t frame = 0; frame < samples.size(); ++frame)
        {
            heardAnything = heardAnything || samples[frame] != 0.0f;
            if (samples[frame] != s_downbeatLevel && samples[frame] != s_beatLevel)
            {
                continue;
            }
            for (const TransportSegment& segment : segments)
            {
                if (frame >= segment.offset && frame < segment.offset + segment.frameCount)
                {
                    clicks.push_back({.position = segment.position + static_cast<SampleCount>(frame - segment.offset),
                                      .isDownbeat = samples[frame] == s_downbeatLevel});
                }
            }
        }
    }

    std::size_t m_blockSize;
    AudioBuffer m_buffer;
};

} // namespace

TEST_CASE("clicks land on the exact beat samples for ten minutes at a fractional tempo")
{
    constexpr double bpm = 137.3;
    constexpr double sampleRate = 44100.0;
    ClickRecorder recorder(bpm, sampleRate, 64);
    REQUIRE(recorder.transport.play());

    constexpr std::size_t blocks = 413437; // ten minutes of 64-frame blocks
    recorder.run(blocks);

    // Beat k starts k * 60 / bpm seconds in, on the first sample at or after that time.
    std::vector<Click> expected;
    for (std::size_t beat = 0;; ++beat)
    {
        const double exactSample = static_cast<double>(beat) * 60.0 / bpm * sampleRate;
        const auto position = static_cast<SampleCount>(std::ceil(exactSample - 1e-4));
        if (position >= static_cast<SampleCount>(blocks * 64))
        {
            break;
        }
        expected.push_back({.position = position, .isDownbeat = beat % 4 == 0});
    }
    REQUIRE(expected.size() > 1300);
    REQUIRE(recorder.clicks.size() == expected.size());
    for (std::size_t beat = 0; beat < expected.size(); ++beat)
    {
        INFO("beat " << beat);
        REQUIRE(recorder.clicks[beat] == expected[beat]);
    }
}

TEST_CASE("a loop wrap clicks the downbeat on the loop start sample")
{
    // Loop bar 2 at 120 bpm and 48 kHz: 96000 to 192000, beats every 24000 samples. The block
    // size doesn't divide the loop, so wraps happen mid-block.
    ClickRecorder recorder(120.0, 48000.0, 1000);
    REQUIRE(recorder.transport.setLoop({.start = 4 * ticksPerBeat, .end = 8 * ticksPerBeat}));
    REQUIRE(recorder.transport.setLoopEnabled(true));
    REQUIRE(recorder.transport.locate(4 * ticksPerBeat));
    REQUIRE(recorder.transport.play());

    recorder.run(300); // a little over three passes

    const std::vector<Click> onePass = {{96000, true}, {120000, false}, {144000, false}, {168000, false}};
    std::vector<Click> expected;
    for (int pass = 0; pass < 4; ++pass)
    {
        expected.insert(expected.end(), onePass.begin(), onePass.end());
    }
    expected.resize(recorder.clicks.size());
    CHECK(recorder.clicks.size() == 13);
    CHECK(recorder.clicks == expected);
}

TEST_CASE("a tempo change during playback keeps clicks on the new beat grid")
{
    ClickRecorder recorder(120.0, 48000.0, 500);
    REQUIRE(recorder.transport.play());
    recorder.run(72); // a beat and a half at 120 bpm: clicks at 0 and 24000

    REQUIRE(recorder.transport.setTimeMap(
        std::make_unique<const TimeMap>(TempoMap{.tempos = {TempoEvent{.position = 0, .bpm = 60.0}}}, 48000.0)));
    recorder.run(200);

    // The playhead continues at a beat and a half, now sample 72000 at 60 bpm. Beat 2 falls on
    // 96000 and beat 3 on 144000.
    const std::vector<Click> expected = {{0, true}, {24000, false}, {96000, false}, {144000, false}};
    CHECK(recorder.clicks == expected);
}

TEST_CASE("a stopped or disabled metronome stays silent")
{
    ClickRecorder stopped(120.0, 48000.0, 512);
    stopped.run(100);
    CHECK_FALSE(stopped.heardAnything);

    ClickRecorder disabled(120.0, 48000.0, 512);
    disabled.metronome.setEnabled(false);
    REQUIRE(disabled.transport.play());
    disabled.run(100);
    CHECK_FALSE(disabled.heardAnything);
}

TEST_CASE("a click that started before disabling rings out without new clicks")
{
    ClickRecorder recorder(120.0, 48000.0, 64);
    REQUIRE(recorder.transport.play());
    recorder.run(1);
    REQUIRE(recorder.clicks.size() == 1);
    recorder.metronome.setEnabled(false);
    recorder.heardAnything = false;

    recorder.run(1);
    CHECK(recorder.heardAnything);

    // Past the next beat at 24000 samples: the tail is over and no new click started.
    recorder.run(400);
    CHECK(recorder.clicks.size() == 1);
}

TEST_CASE("a click that started before stop rings out")
{
    ClickRecorder recorder(120.0, 48000.0, 64);
    REQUIRE(recorder.transport.play());
    recorder.run(1);
    REQUIRE(recorder.clicks.size() == 1);
    REQUIRE(recorder.transport.stop());
    recorder.heardAnything = false;

    recorder.run(1);

    CHECK(recorder.heardAnything);
}
