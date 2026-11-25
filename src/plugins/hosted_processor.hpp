#pragma once

#include "engine/processor.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <thread>

namespace sessionflow
{

enum class HostState
{
    Running,
    // Missed a block deadline. Outputs silence until the host answers again.
    Unresponsive,
    // The host process has exited. Outputs silence from then on.
    Crashed,
};

// A Processor that runs in a separate plugin host process, so a crashing plugin only silences
// its own track. Audio, MIDI and transport go through shared memory once per block.
class HostedProcessor final : public Processor
{
  public:
    // Starts hostExecutable with the connection name as its only argument. Throws
    // std::system_error when the process or the shared memory can't be set up.
    explicit HostedProcessor(const std::filesystem::path& hostExecutable);
    ~HostedProcessor() override;

    HostedProcessor(const HostedProcessor&) = delete;
    HostedProcessor& operator=(const HostedProcessor&) = delete;

    // Waits for the host to prepare. Leaves the state Unresponsive when it doesn't answer in time.
    void prepare(const ProcessSetup& setup) override;

    // Hands the block to the host and waits for it until half the block's duration has passed.
    void process(const ProcessBlock& block) noexcept SESSIONFLOW_NONBLOCKING override;

    // Queues a parameter change for the next block. Call from one thread only. Returns false
    // when the queue is full.
    bool setParameter(std::uint32_t id, float value) noexcept;

    HostState state() const noexcept;

  private:
    struct Connection;

    void watchHost() noexcept;
    bool waitForResponse(std::chrono::nanoseconds timeout) noexcept;
    void silence(const ProcessBlock& block) noexcept SESSIONFLOW_NONBLOCKING;

    std::unique_ptr<Connection> m_connection;
    std::atomic<HostState> m_state{HostState::Running};
    std::thread m_watchdog;
};

} // namespace sessionflow
