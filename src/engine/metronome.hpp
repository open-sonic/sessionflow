#pragma once

#include "core/audio_buffer.hpp"
#include "core/realtime.hpp"
#include "engine/time_map.hpp"
#include "engine/transport.hpp"

#include <atomic>
#include <cstddef>
#include <span>
#include <vector>

namespace sessionflow
{

// Clicks on every beat, higher on the first beat of a bar. Each click starts on the sample
// TimeMap::sampleAt() gives for its beat, also right after a loop wrap.
class Metronome
{
  public:
    // Control thread. Renders the two clicks, so it allocates.
    void prepare(double sampleRate);

    // Safe from any thread.
    void setEnabled(bool isEnabled) noexcept;

    // Audio thread. Adds clicks for the transport segments of this block into every channel
    // of output. A click that started earlier rings out, also while stopped.
    void render(AudioBufferView output, std::span<const TransportSegment> segments,
                const TimeMap& timeMap) noexcept SESSIONFLOW_NONBLOCKING;

  private:
    void mixClick(AudioBufferView output, std::size_t from, std::size_t to) noexcept SESSIONFLOW_NONBLOCKING;

    std::atomic<bool> m_isEnabled{true};
    std::vector<float> m_downbeatClick;
    std::vector<float> m_beatClick;
    const std::vector<float>* m_click = nullptr;
    std::size_t m_clickPosition = 0;
};

} // namespace sessionflow
