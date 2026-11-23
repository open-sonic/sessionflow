#include "media/audio_file.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <fstream>
#include <vector>

using namespace sessionflow;

namespace
{

void appendLittleEndian(std::vector<char>& bytes, std::uint32_t value, int size)
{
    for (int index = 0; index < size; ++index)
    {
        bytes.push_back(static_cast<char>((value >> (8 * index)) & 0xff));
    }
}

// Minimal 16-bit PCM WAV, written by hand so the test doesn't depend on the decoder.
std::filesystem::path writeStereoWav(const std::vector<std::int16_t>& interleaved, std::uint32_t sampleRate)
{
    const auto dataSize = static_cast<std::uint32_t>(interleaved.size() * sizeof(std::int16_t));
    std::vector<char> bytes{'R', 'I', 'F', 'F'};
    appendLittleEndian(bytes, 36 + dataSize, 4);
    bytes.insert(bytes.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    appendLittleEndian(bytes, 16, 4);
    appendLittleEndian(bytes, 1, 2);
    appendLittleEndian(bytes, 2, 2);
    appendLittleEndian(bytes, sampleRate, 4);
    appendLittleEndian(bytes, sampleRate * 4, 4);
    appendLittleEndian(bytes, 4, 2);
    appendLittleEndian(bytes, 16, 2);
    bytes.insert(bytes.end(), {'d', 'a', 't', 'a'});
    appendLittleEndian(bytes, dataSize, 4);
    for (const std::int16_t sample : interleaved)
    {
        appendLittleEndian(bytes, static_cast<std::uint16_t>(sample), 2);
    }

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "sessionflow_test_stereo.wav";
    std::ofstream(path, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return path;
}

} // namespace

TEST_CASE("decodes a stereo WAV into planar channels")
{
    const std::filesystem::path path = writeStereoWav({16384, -16384, 0, 32767}, 44100);

    std::optional<DecodedAudio> decoded = decodeAudioFile(path);
    std::filesystem::remove(path);

    REQUIRE(decoded.has_value());
    CHECK(decoded->sampleRate == 44100.0);
    REQUIRE(decoded->samples.channelCount() == 2);
    REQUIRE(decoded->samples.frameCount() == 2);

    const AudioBufferView samples = decoded->samples.view();
    CHECK(samples.channel(0)[0] == Catch::Approx(0.5f));
    CHECK(samples.channel(1)[0] == Catch::Approx(-0.5f));
    CHECK(samples.channel(0)[1] == Catch::Approx(0.0f));
    CHECK(samples.channel(1)[1] == Catch::Approx(1.0f).margin(0.001));
}

TEST_CASE("unknown extensions and missing files decode to nothing")
{
    CHECK_FALSE(decodeAudioFile("song.ogg").has_value());
    CHECK_FALSE(decodeAudioFile(std::filesystem::temp_directory_path() / "sessionflow_missing.wav").has_value());
}
