#include "media/audio_file.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <numbers>
#include <string>
#include <vector>

using namespace sessionflow;

namespace
{

const std::filesystem::path testData = SESSIONFLOW_TEST_DATA_DIR;

void appendLittleEndian(std::vector<char>& bytes, std::uint32_t value, int size)
{
    for (int index = 0; index < size; ++index)
    {
        bytes.push_back(static_cast<char>((value >> (8 * index)) & 0xff));
    }
}

enum class WavFormat
{
    pcm16,
    pcm24,
    float32,
};

// Minimal stereo WAV, written by hand so the test doesn't depend on the decoder.
std::filesystem::path writeStereoWav(WavFormat format, const std::vector<float>& interleaved,
                                     std::uint32_t sampleRate = 44100)
{
    const std::uint32_t bytesPerSample = format == WavFormat::pcm16 ? 2 : format == WavFormat::pcm24 ? 3 : 4;
    const std::uint32_t formatTag = format == WavFormat::float32 ? 3 : 1;
    const auto dataSize = static_cast<std::uint32_t>(interleaved.size()) * bytesPerSample;

    std::vector<char> bytes{'R', 'I', 'F', 'F'};
    appendLittleEndian(bytes, 36 + dataSize, 4);
    bytes.insert(bytes.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    appendLittleEndian(bytes, 16, 4);
    appendLittleEndian(bytes, formatTag, 2);
    appendLittleEndian(bytes, 2, 2);
    appendLittleEndian(bytes, sampleRate, 4);
    appendLittleEndian(bytes, sampleRate * 2 * bytesPerSample, 4);
    appendLittleEndian(bytes, 2 * bytesPerSample, 2);
    appendLittleEndian(bytes, 8 * bytesPerSample, 2);
    bytes.insert(bytes.end(), {'d', 'a', 't', 'a'});
    appendLittleEndian(bytes, dataSize, 4);
    for (const float sample : interleaved)
    {
        if (format == WavFormat::float32)
        {
            appendLittleEndian(bytes, std::bit_cast<std::uint32_t>(sample), 4);
            continue;
        }
        const double fullScale = format == WavFormat::pcm16 ? 32768.0 : 8388608.0;
        const auto integer = static_cast<std::int32_t>(std::lround(sample * fullScale));
        appendLittleEndian(bytes, static_cast<std::uint32_t>(integer), static_cast<int>(bytesPerSample));
    }

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "sessionflow_test_stereo.wav";
    std::ofstream(path, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return path;
}

// Matches the signal in tests/data: 441 Hz, left at 0.5, right at 0.25.
float fixtureSine(std::size_t frame, float amplitude)
{
    return amplitude *
           static_cast<float>(std::sin(2.0 * std::numbers::pi * 441.0 * static_cast<double>(frame) / 44100.0));
}

} // namespace

TEST_CASE("decodes 16, 24 and 32-bit float WAV into planar channels")
{
    const WavFormat format = GENERATE(WavFormat::pcm16, WavFormat::pcm24, WavFormat::float32);
    const std::filesystem::path path = writeStereoWav(format, {0.5f, -0.5f, -0.25f, 0.125f});

    std::optional<DecodedAudio> decoded = decodeAudioFile(path);
    std::filesystem::remove(path);

    REQUIRE(decoded.has_value());
    CHECK(decoded->sampleRate == 44100.0);
    REQUIRE(decoded->samples.channelCount() == 2);
    REQUIRE(decoded->samples.frameCount() == 2);

    const AudioBufferView samples = decoded->samples.view();
    CHECK(samples.channel(0)[0] == 0.5f);
    CHECK(samples.channel(1)[0] == -0.5f);
    CHECK(samples.channel(0)[1] == -0.25f);
    CHECK(samples.channel(1)[1] == 0.125f);
}

TEST_CASE("decodes 24-bit FLAC")
{
    std::optional<DecodedAudio> decoded = decodeAudioFile(testData / "sine_24bit.flac");

    REQUIRE(decoded.has_value());
    CHECK(decoded->sampleRate == 44100.0);
    REQUIRE(decoded->samples.channelCount() == 2);
    REQUIRE(decoded->samples.frameCount() == 4410);

    const AudioBufferView samples = decoded->samples.view();
    for (std::size_t frame = 0; frame < samples.frameCount(); ++frame)
    {
        REQUIRE(samples.channel(0)[frame] == Catch::Approx(fixtureSine(frame, 0.5f)).margin(1e-5));
        REQUIRE(samples.channel(1)[frame] == Catch::Approx(fixtureSine(frame, 0.25f)).margin(1e-5));
    }
}

TEST_CASE("decodes MP3 without encoder delay")
{
    std::optional<DecodedAudio> decoded = decodeAudioFile(testData / "sine.mp3");

    REQUIRE(decoded.has_value());
    CHECK(decoded->sampleRate == 44100.0);
    REQUIRE(decoded->samples.channelCount() == 2);
    // The encoder pads both ends. Only the gapless header lets the decoder trim back to 4410.
    CHECK(decoded->samples.frameCount() == 4410);

    // Lossy, so compare a steady stretch in the middle with a loose margin.
    const AudioBufferView samples = decoded->samples.view();
    for (std::size_t frame = 1000; frame < 3000; ++frame)
    {
        REQUIRE(samples.channel(0)[frame] == Catch::Approx(fixtureSine(frame, 0.5f)).margin(0.05));
        REQUIRE(samples.channel(1)[frame] == Catch::Approx(fixtureSine(frame, 0.25f)).margin(0.05));
    }
}

TEST_CASE("reader streams blocks and seeks")
{
    const std::string file = GENERATE("sine_24bit.flac", "sine.mp3");
    std::optional<DecodedAudio> whole = decodeAudioFile(testData / file);
    std::optional<AudioFileReader> reader = AudioFileReader::open(testData / file);
    REQUIRE(whole.has_value());
    REQUIRE(reader.has_value());
    REQUIRE(reader->frameCount() == static_cast<SampleCount>(whole->samples.frameCount()));

    AudioBuffer block(2, 1000);
    REQUIRE(reader->seek(2000));
    REQUIRE(reader->read(block.view()) == 1000);
    for (std::size_t frame = 0; frame < 1000; ++frame)
    {
        REQUIRE(block.view().channel(0)[frame] == whole->samples.view().channel(0)[2000 + frame]);
        REQUIRE(block.view().channel(1)[frame] == whole->samples.view().channel(1)[2000 + frame]);
    }

    REQUIRE(reader->seek(reader->frameCount() - 10));
    CHECK(reader->read(block.view()) == 10);
    CHECK_FALSE(reader->seek(reader->frameCount() + 1));
}

TEST_CASE("unknown extensions and missing files decode to nothing")
{
    CHECK_FALSE(decodeAudioFile("song.ogg").has_value());
    CHECK_FALSE(decodeAudioFile(std::filesystem::temp_directory_path() / "sessionflow_missing.wav").has_value());
    CHECK_FALSE(AudioFileReader::open(testData / "missing.flac").has_value());
}
