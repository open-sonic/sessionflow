#pragma once

#include "core/midi_event.hpp"
#include "core/spsc_queue.hpp"
#include "engine/processor.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

namespace sessionflow
{

// Shared memory layout between the engine and one plugin host process. Both sides are built
// from the same source, so the layout only has to match within one build.

inline constexpr std::uint32_t hostProtocolVersion = 1;
inline constexpr std::size_t hostMaxChannels = 8;
inline constexpr std::size_t hostMaxBlockSize = 4096;
inline constexpr std::size_t hostMaxMidiEvents = 1024;
inline constexpr std::size_t hostParameterQueueSize = 512;

enum class HostRequest : std::uint32_t
{
    Prepare,
    Process,
};

struct HostParameterChange
{
    std::uint32_t id = 0;
    float value = 0.0f;
};

struct HostSharedBlock
{
    std::uint32_t version = hostProtocolVersion;
    long long engineProcessId = 0;

    // Counters behind IpcSignal. The engine bumps request, the host answers by bumping
    // response to the same value.
    std::atomic<std::uint32_t> request{0};
    std::atomic<std::uint32_t> response{0};
    std::atomic<bool> shutdown{false};

    HostRequest kind = HostRequest::Process;
    ProcessSetup setup;
    TransportState transport;
    std::uint32_t frameCount = 0;
    std::uint32_t channelCount = 0;
    std::uint32_t midiCount = 0;
    std::array<MidiEvent, hostMaxMidiEvents> midi{};
    std::array<float, hostMaxChannels * hostMaxBlockSize> audio{};

    SpscQueue<HostParameterChange, hostParameterQueueSize> parameters;
};

static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::is_trivially_copyable_v<ProcessSetup> && std::is_trivially_copyable_v<TransportState>);

// Names of the shared memory region and the two signals that belong to one connection.
inline std::string hostMemoryName(const std::string& connection)
{
#if defined(_WIN32)
    return "Local\\" + connection;
#else
    return "/" + connection;
#endif
}

inline std::string hostRequestSignalName(const std::string& connection)
{
    return hostMemoryName(connection) + "-request";
}

inline std::string hostResponseSignalName(const std::string& connection)
{
    return hostMemoryName(connection) + "-response";
}

} // namespace sessionflow
