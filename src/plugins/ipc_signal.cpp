#include "plugins/ipc_signal.hpp"

#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <os/os_sync_wait_on_address.h>
#elif defined(__linux__)
#include <ctime>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace sessionflow
{

namespace
{

using Clock = std::chrono::steady_clock;

#if defined(_WIN32)

DWORD toWaitMilliseconds(Clock::duration remaining)
{
    // Round up: a zero wait would spin instead of sleeping.
    const auto milliseconds = std::chrono::ceil<std::chrono::milliseconds>(remaining).count();
    return milliseconds > 0 ? static_cast<DWORD>(milliseconds) : 0;
}

#endif

} // namespace

IpcSignal::IpcSignal([[maybe_unused]] const std::string& name, std::atomic<std::uint32_t>& counter) : m_counter(counter)
{
#if defined(_WIN32)
    // Auto-reset: a wake that lands before the wait is not lost, and stale wakes are harmless
    // because the waiter checks the counter again.
    m_event = CreateEventA(nullptr, FALSE, FALSE, name.c_str());
    if (m_event == nullptr)
    {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "ipc signal");
    }
#endif
}

IpcSignal::~IpcSignal()
{
#if defined(_WIN32)
    CloseHandle(m_event);
#endif
}

void IpcSignal::notify() noexcept
{
    m_counter.fetch_add(1, std::memory_order_acq_rel);
#if defined(_WIN32)
    SetEvent(m_event);
#elif defined(__APPLE__)
    os_sync_wake_by_address_all(&m_counter, sizeof(std::uint32_t), OS_SYNC_WAKE_BY_ADDRESS_SHARED);
#elif defined(__linux__)
    syscall(SYS_futex, &m_counter, FUTEX_WAKE, 1, nullptr, nullptr, 0);
#endif
}

std::uint32_t IpcSignal::waitWhile(std::uint32_t seen, std::chrono::nanoseconds timeout) noexcept
{
    const Clock::time_point deadline = Clock::now() + timeout;
    while (true)
    {
        const std::uint32_t current = value();
        if (current != seen)
        {
            return current;
        }
        const Clock::duration remaining = deadline - Clock::now();
        if (remaining <= Clock::duration::zero())
        {
            return seen;
        }
#if defined(_WIN32)
        WaitForSingleObject(m_event, toWaitMilliseconds(remaining));
#elif defined(__APPLE__)
        const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count();
        os_sync_wait_on_address_with_timeout(&m_counter, seen, sizeof(std::uint32_t), OS_SYNC_WAIT_ON_ADDRESS_SHARED,
                                             OS_CLOCK_MACH_ABSOLUTE_TIME, static_cast<std::uint64_t>(nanoseconds));
#elif defined(__linux__)
        const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count();
        const timespec relative{.tv_sec = static_cast<time_t>(nanoseconds / 1'000'000'000),
                                .tv_nsec = static_cast<long>(nanoseconds % 1'000'000'000)};
        syscall(SYS_futex, &m_counter, FUTEX_WAIT, seen, &relative, nullptr, 0);
#endif
    }
}

} // namespace sessionflow
