#include "plugins/plugin_host.hpp"

#include "plugins/child_process.hpp"
#include "plugins/host_protocol.hpp"
#include "plugins/ipc_signal.hpp"
#include "plugins/shared_memory.hpp"

#include <spdlog/spdlog.h>

#include <exception>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace sessionflow
{

namespace
{

// How often an idle host checks that the engine is still alive.
constexpr std::chrono::milliseconds s_idleCheckInterval{250};

void runBlock(HostSharedBlock& shared, Processor& processor, const HostParameterHandler& onParameter,
              std::array<float*, hostMaxChannels>& channels)
{
    while (const std::optional<HostParameterChange> change = shared.parameters.tryPop())
    {
        onParameter(change->id, change->value);
    }

    if (shared.kind == HostRequest::Prepare)
    {
        processor.prepare(shared.setup);
        return;
    }

    const ProcessBlock block{
        .audio = AudioBufferView(channels.data(), shared.channelCount, shared.frameCount),
        .sidechain = {},
        .midi = std::span<const MidiEvent>(shared.midi.data(), shared.midiCount),
        .transport = shared.transport,
    };
    processor.process(block);
}

} // namespace

int runPluginHost(const std::string& connection, Processor& processor, const HostParameterHandler& onParameter)
{
#if defined(_WIN32)
    // A crashing plugin must end this process, not leave it waiting on an error dialog.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif

    try
    {
        const SharedMemory memory = SharedMemory::open(hostMemoryName(connection), sizeof(HostSharedBlock));
        auto& shared = *static_cast<HostSharedBlock*>(memory.data());
        if (shared.version != hostProtocolVersion)
        {
            spdlog::error("plugin host: protocol {} does not match {}", shared.version, hostProtocolVersion);
            return 2;
        }
        IpcSignal request(hostRequestSignalName(connection), shared.request);
        IpcSignal response(hostResponseSignalName(connection), shared.response);

        std::array<float*, hostMaxChannels> channels{};
        for (std::size_t channel = 0; channel < hostMaxChannels; ++channel)
        {
            channels[channel] = shared.audio.data() + channel * hostMaxBlockSize;
        }

        std::uint32_t handled = response.value();
        while (!shared.shutdown.load(std::memory_order_acquire))
        {
            if (request.waitWhile(handled, s_idleCheckInterval) == handled)
            {
                if (!isProcessAlive(shared.engineProcessId))
                {
                    return 0;
                }
                continue;
            }
            if (shared.shutdown.load(std::memory_order_acquire))
            {
                break;
            }

            // The engine sends one request at a time and waits for this answer before the next,
            // so every request bumps the counter by exactly one.
            runBlock(shared, processor, onParameter, channels);
            ++handled;
            response.notify();
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        spdlog::error("plugin host: {}", error.what());
        return 1;
    }
}

} // namespace sessionflow
