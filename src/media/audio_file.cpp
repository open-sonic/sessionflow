#include "media/audio_file.hpp"

#define DR_WAV_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION
#define DR_MP3_IMPLEMENTATION
#include <dr_flac.h>
#include <dr_mp3.h>
#include <dr_wav.h>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace sessionflow
{

namespace
{

constexpr std::size_t chunkFrames = 4096;

// Reading through std::ifstream keeps non-ASCII paths working on Windows.
struct FileStream
{
    std::ifstream file;
    std::int64_t size = 0;
};

std::size_t readBytes(void* userData, void* destination, std::size_t byteCount)
{
    FileStream& stream = *static_cast<FileStream*>(userData);
    stream.file.read(static_cast<char*>(destination), static_cast<std::streamsize>(byteCount));
    const auto bytesRead = static_cast<std::size_t>(stream.file.gcount());
    // A short read at the end sets failbit, which would make every later seek fail.
    stream.file.clear();
    return bytesRead;
}

static_assert(DRWAV_SEEK_CUR == 1 && DRFLAC_SEEK_CUR == 1 && DRMP3_SEEK_CUR == 1);
static_assert(DRWAV_SEEK_END == 2 && DRFLAC_SEEK_END == 2 && DRMP3_SEEK_END == 2);

// Each dr_lib has its own origin enum, all in SET, CUR, END order.
template <typename Origin> unsigned int seekBytes(void* userData, int offset, Origin origin)
{
    FileStream& stream = *static_cast<FileStream*>(userData);
    std::int64_t base = 0;
    if (static_cast<int>(origin) == 1)
    {
        base = static_cast<std::int64_t>(stream.file.tellg());
    }
    else if (static_cast<int>(origin) == 2)
    {
        base = stream.size;
    }

    const std::int64_t target = base + offset;
    // dr_flac seeks past the end while searching for frames and expects that to fail.
    if (target < 0 || target > stream.size)
    {
        return 0;
    }
    stream.file.seekg(target);
    return stream.file ? 1 : 0;
}

template <typename Int64> unsigned int tellBytes(void* userData, Int64* cursor)
{
    FileStream& stream = *static_cast<FileStream*>(userData);
    *cursor = static_cast<Int64>(stream.file.tellg());
    return 1;
}

std::string lowercaseExtension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

} // namespace

namespace detail
{

struct AudioDecoder
{
    virtual ~AudioDecoder() = default;

    // Sets channels, sampleRate and frameCount on success.
    virtual bool init() = 0;
    virtual std::uint64_t readInterleaved(float* samples, std::uint64_t frameCount) = 0;
    virtual bool seekToFrame(std::uint64_t frame) = 0;

    FileStream stream;
    std::size_t channels = 0;
    double sampleRate = 0.0;
    SampleCount frameCount = 0;
    std::vector<float> interleaved;
};

} // namespace detail

namespace
{

class WavDecoder final : public detail::AudioDecoder
{
  public:
    ~WavDecoder() override
    {
        if (m_initialized)
        {
            drwav_uninit(&m_wav);
        }
    }

    bool init() override
    {
        m_initialized = drwav_init(&m_wav, readBytes, seekBytes<drwav_seek_origin>, tellBytes<drwav_int64>, &stream,
                                   nullptr) == DRWAV_TRUE;
        if (!m_initialized)
        {
            return false;
        }
        channels = m_wav.channels;
        sampleRate = m_wav.sampleRate;
        frameCount = static_cast<SampleCount>(m_wav.totalPCMFrameCount);
        return true;
    }

    std::uint64_t readInterleaved(float* samples, std::uint64_t count) override
    {
        return drwav_read_pcm_frames_f32(&m_wav, count, samples);
    }

    bool seekToFrame(std::uint64_t frame) override
    {
        return drwav_seek_to_pcm_frame(&m_wav, frame) == DRWAV_TRUE;
    }

  private:
    drwav m_wav{};
    bool m_initialized = false;
};

class FlacDecoder final : public detail::AudioDecoder
{
  public:
    ~FlacDecoder() override
    {
        drflac_close(m_flac);
    }

    bool init() override
    {
        m_flac = drflac_open(readBytes, seekBytes<drflac_seek_origin>, tellBytes<drflac_int64>, &stream, nullptr);
        if (m_flac == nullptr)
        {
            return false;
        }
        channels = m_flac->channels;
        sampleRate = m_flac->sampleRate;
        frameCount = static_cast<SampleCount>(m_flac->totalPCMFrameCount);
        return true;
    }

    std::uint64_t readInterleaved(float* samples, std::uint64_t count) override
    {
        return drflac_read_pcm_frames_f32(m_flac, count, samples);
    }

    bool seekToFrame(std::uint64_t frame) override
    {
        return drflac_seek_to_pcm_frame(m_flac, frame) == DRFLAC_TRUE;
    }

  private:
    drflac* m_flac = nullptr;
};

class Mp3Decoder final : public detail::AudioDecoder
{
  public:
    ~Mp3Decoder() override
    {
        if (m_initialized)
        {
            drmp3_uninit(&m_mp3);
        }
    }

    bool init() override
    {
        m_initialized = drmp3_init(&m_mp3, readBytes, seekBytes<drmp3_seek_origin>, tellBytes<drmp3_int64>, nullptr,
                                   &stream, nullptr) == DRMP3_TRUE;
        if (!m_initialized)
        {
            return false;
        }
        channels = m_mp3.channels;
        sampleRate = m_mp3.sampleRate;
        // MP3 has no reliable length header, so this decodes the whole file once.
        frameCount = static_cast<SampleCount>(drmp3_get_pcm_frame_count(&m_mp3));
        return true;
    }

    std::uint64_t readInterleaved(float* samples, std::uint64_t count) override
    {
        return drmp3_read_pcm_frames_f32(&m_mp3, count, samples);
    }

    bool seekToFrame(std::uint64_t frame) override
    {
        return drmp3_seek_to_pcm_frame(&m_mp3, frame) == DRMP3_TRUE;
    }

  private:
    drmp3 m_mp3{};
    bool m_initialized = false;
};

std::unique_ptr<detail::AudioDecoder> makeDecoder(const std::string& extension)
{
    if (extension == ".wav")
    {
        return std::make_unique<WavDecoder>();
    }
    if (extension == ".flac")
    {
        return std::make_unique<FlacDecoder>();
    }
    if (extension == ".mp3")
    {
        return std::make_unique<Mp3Decoder>();
    }
    return nullptr;
}

} // namespace

std::optional<AudioFileReader> AudioFileReader::open(const std::filesystem::path& path)
{
    std::unique_ptr<detail::AudioDecoder> decoder = makeDecoder(lowercaseExtension(path));
    if (!decoder)
    {
        return std::nullopt;
    }

    decoder->stream.file.open(path, std::ios::binary | std::ios::ate);
    if (!decoder->stream.file)
    {
        return std::nullopt;
    }
    decoder->stream.size = static_cast<std::int64_t>(decoder->stream.file.tellg());
    decoder->stream.file.seekg(0);

    if (!decoder->init() || decoder->channels == 0)
    {
        return std::nullopt;
    }
    decoder->interleaved.resize(chunkFrames * decoder->channels);
    return AudioFileReader(std::move(decoder));
}

AudioFileReader::AudioFileReader(std::unique_ptr<detail::AudioDecoder> decoder) noexcept : m_decoder(std::move(decoder))
{
}

AudioFileReader::AudioFileReader(AudioFileReader&&) noexcept = default;
AudioFileReader& AudioFileReader::operator=(AudioFileReader&&) noexcept = default;
AudioFileReader::~AudioFileReader() = default;

std::size_t AudioFileReader::channelCount() const noexcept
{
    return m_decoder->channels;
}

double AudioFileReader::sampleRate() const noexcept
{
    return m_decoder->sampleRate;
}

SampleCount AudioFileReader::frameCount() const noexcept
{
    return m_decoder->frameCount;
}

bool AudioFileReader::seek(SampleCount frame)
{
    if (frame < 0 || frame > m_decoder->frameCount)
    {
        return false;
    }
    return m_decoder->seekToFrame(static_cast<std::uint64_t>(frame));
}

std::size_t AudioFileReader::read(AudioBufferView destination)
{
    assert(destination.channelCount() == m_decoder->channels);
    const std::size_t channels = m_decoder->channels;
    float* interleaved = m_decoder->interleaved.data();

    std::size_t framesRead = 0;
    while (framesRead < destination.frameCount())
    {
        const std::size_t wanted = std::min(chunkFrames, destination.frameCount() - framesRead);
        const auto decoded = static_cast<std::size_t>(m_decoder->readInterleaved(interleaved, wanted));
        for (std::size_t channel = 0; channel < channels; ++channel)
        {
            const std::span<float> samples = destination.channel(channel).subspan(framesRead, decoded);
            for (std::size_t frame = 0; frame < decoded; ++frame)
            {
                samples[frame] = interleaved[frame * channels + channel];
            }
        }
        framesRead += decoded;
        if (decoded < wanted)
        {
            break;
        }
    }
    return framesRead;
}

std::optional<DecodedAudio> decodeAudioFile(const std::filesystem::path& path)
{
    std::optional<AudioFileReader> reader = AudioFileReader::open(path);
    if (!reader)
    {
        return std::nullopt;
    }

    const std::size_t channels = reader->channelCount();
    AudioBuffer samples(channels, static_cast<std::size_t>(reader->frameCount()));
    const std::size_t framesRead = reader->read(samples.view());
    if (framesRead == samples.frameCount())
    {
        return DecodedAudio{std::move(samples), reader->sampleRate()};
    }

    // A truncated file can hold fewer frames than its header promises.
    AudioBuffer truncated(channels, framesRead);
    for (std::size_t channel = 0; channel < channels; ++channel)
    {
        std::ranges::copy(samples.view().channel(channel).first(framesRead), truncated.view().channel(channel).begin());
    }
    return DecodedAudio{std::move(truncated), reader->sampleRate()};
}

} // namespace sessionflow
