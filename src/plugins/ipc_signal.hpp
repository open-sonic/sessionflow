#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

namespace sessionflow
{

// Wakes a thread in another process. The state is a counter in shared memory: notify()
// increments it, waitWhile() sleeps until it differs from a value the waiter has seen.
// Linux and macOS wait on the counter's address directly. Windows can't wait on an address
// across processes, so a named event with the same name does the waking there.
class IpcSignal
{
  public:
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

    // counter must live in memory shared by both processes. Both sides pass the same name.
    // Throws std::system_error when the Windows event can't be created.
    IpcSignal(const std::string& name, std::atomic<std::uint32_t>& counter);
    ~IpcSignal();

    IpcSignal(const IpcSignal&) = delete;
    IpcSignal& operator=(const IpcSignal&) = delete;

    std::uint32_t value() const noexcept
    {
        return m_counter.load(std::memory_order_acquire);
    }

    void notify() noexcept;

    // Returns the counter once it differs from seen, or seen when the timeout passes first.
    std::uint32_t waitWhile(std::uint32_t seen, std::chrono::nanoseconds timeout) noexcept;

  private:
    std::atomic<std::uint32_t>& m_counter;
#if defined(_WIN32)
    void* m_event = nullptr;
#endif
};

} // namespace sessionflow
