#include "plugins/hosted_processor.hpp"

#include "plugins/child_process.hpp"
#include "plugins/host_protocol.hpp"
#include "plugins/ipc_signal.hpp"
#include "plugins/shared_memory.hpp"

#include <algorithm>
#include <new>
#include <string>

namespace sessionflow
{

namespace
{

using Clock = std::chrono::steady_clock;

constexpr std::chrono::seconds s_prepareTimeout{2};
constexpr std::chrono::seconds s_shutdownTimeout{1};
constexpr std::chrono::milliseconds s_shutdownPollInterval{10};

std::string makeConnectionName()
{
    // Short: macOS limits shared memory names to 31 characters.
    static std::atomic<unsigned> s_nextConnection{0};
    return "sf-" + std::to_string(currentProcessId()) + "-" + std::to_string(s_nextConnection.fetch_add(1));
}

} // namespace

struct HostedProcessor::Connection
{
    explicit Connection(const std::string& name)
        : memory(SharedMemory::create(hostMemoryName(name), sizeof(HostSharedBlock))),
          shared(*new (memory.data()) HostSharedBlock{}), request(hostRequestSignalName(name), shared.request),
          response(hostResponseSignalName(name), shared.response)
    {
        shared.engineProcessId = currentProcessId();
    }

    SharedMemory memory;
    HostSharedBlock& shared;
    IpcSignal request;
    IpcSignal response;
    std::unique_ptr<ChildProcess> process;
    double sampleRate = 48000.0;
};

HostedProcessor::HostedProcessor(const std::filesystem::path& hostExecutable)
{
    const std::string name = makeConnectionName();
    m_connection = std::make_unique<Connection>(name);
    m_connection->process = std::make_unique<ChildProcess>(hostExecutable, std::vector<std::string>{name});
    m_watchdog = std::thread([this] { watchHost(); });
}

HostedProcessor::~HostedProcessor()
{
    Connection& connection = *m_connection;
    connection.shared.shutdown.store(true, std::memory_order_release);
    connection.request.notify();

    const Clock::time_point deadline = Clock::now() + s_shutdownTimeout;
    while (state() != HostState::Crashed && Clock::now() < deadline)
    {
        std::this_thread::sleep_for(s_shutdownPollInterval);
    }
    connection.process->kill();
    m_watchdog.join();
}

void HostedProcessor::prepare(const ProcessSetup& setup)
{
    Connection& connection = *m_connection;
    connection.sampleRate = setup.sampleRate;
    if (state() == HostState::Crashed)
    {
        return;
    }

    // A block the host missed must finish before the request area can be reused.
    if (!waitForResponse(s_prepareTimeout))
    {
        HostState running = HostState::Running;
        m_state.compare_exchange_strong(running, HostState::Unresponsive);
        return;
    }

    connection.shared.kind = HostRequest::Prepare;
    connection.shared.setup = setup;
    connection.request.notify();

    const bool answered = waitForResponse(s_prepareTimeout);
    HostState from = answered ? HostState::Unresponsive : HostState::Running;
    m_state.compare_exchange_strong(from, answered ? HostState::Running : HostState::Unresponsive);
}

// The wait for the host process is the one deliberate blocking call on the audio thread: the
// block can't be finished without the host's output. It is bounded by half a block.
#if defined(__clang__)
#pragma clang diagnostic push
#if __has_warning("-Wfunction-effects")
#pragma clang diagnostic ignored "-Wfunction-effects"
#endif
#endif

void HostedProcessor::process(const ProcessBlock& block) noexcept SESSIONFLOW_NONBLOCKING
{
    Connection& connection = *m_connection;
    HostSharedBlock& shared = connection.shared;

    HostState current = m_state.load(std::memory_order_acquire);
    if (current == HostState::Crashed)
    {
        silence(block);
        return;
    }
    if (connection.response.value() != connection.request.value())
    {
        // Still busy with a block it missed. Writing now would race with the host.
        m_state.compare_exchange_strong(current, HostState::Unresponsive);
        silence(block);
        return;
    }
    if (current == HostState::Unresponsive)
    {
        m_state.compare_exchange_strong(current, HostState::Running);
    }

    const std::size_t frames = std::min(block.audio.frameCount(), hostMaxBlockSize);
    const std::size_t channels = std::min(block.audio.channelCount(), hostMaxChannels);
    for (std::size_t channel = 0; channel < channels; ++channel)
    {
        std::ranges::copy(block.audio.channel(channel).first(frames),
                          shared.audio.begin() + static_cast<std::ptrdiff_t>(channel * hostMaxBlockSize));
    }
    const std::size_t midiCount = std::min(block.midi.size(), hostMaxMidiEvents);
    std::ranges::copy(block.midi.first(midiCount), shared.midi.begin());

    shared.kind = HostRequest::Process;
    shared.transport = block.transport;
    shared.frameCount = static_cast<std::uint32_t>(frames);
    shared.channelCount = static_cast<std::uint32_t>(channels);
    shared.midiCount = static_cast<std::uint32_t>(midiCount);
    connection.request.notify();

    const auto timeout = std::chrono::duration<double>(static_cast<double>(frames) / connection.sampleRate / 2.0);
    if (!waitForResponse(std::chrono::duration_cast<std::chrono::nanoseconds>(timeout)))
    {
        HostState running = HostState::Running;
        m_state.compare_exchange_strong(running, HostState::Unresponsive);
        silence(block);
        return;
    }

    block.audio.clear();
    for (std::size_t channel = 0; channel < channels; ++channel)
    {
        const auto source = shared.audio.begin() + static_cast<std::ptrdiff_t>(channel * hostMaxBlockSize);
        std::copy(source, source + static_cast<std::ptrdiff_t>(frames), block.audio.channel(channel).begin());
    }
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

bool HostedProcessor::setParameter(std::uint32_t id, float value) noexcept
{
    return m_connection->shared.parameters.tryPush({.id = id, .value = value});
}

HostState HostedProcessor::state() const noexcept
{
    return m_state.load(std::memory_order_acquire);
}

void HostedProcessor::watchHost() noexcept
{
    m_connection->process->waitForExit();
    m_state.store(HostState::Crashed, std::memory_order_release);
}

bool HostedProcessor::waitForResponse(std::chrono::nanoseconds timeout) noexcept
{
    Connection& connection = *m_connection;
    const std::uint32_t expected = connection.request.value();
    const Clock::time_point deadline = Clock::now() + timeout;

    std::uint32_t seen = connection.response.value();
    while (seen != expected)
    {
        const Clock::duration remaining = deadline - Clock::now();
        if (remaining <= Clock::duration::zero() || state() == HostState::Crashed)
        {
            return false;
        }
        seen = connection.response.waitWhile(seen, remaining);
    }
    return true;
}

void HostedProcessor::silence(const ProcessBlock& block) noexcept SESSIONFLOW_NONBLOCKING
{
    block.audio.clear();
}

} // namespace sessionflow
