#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

namespace sessionflow
{

#if defined(_MSC_VER)
#pragma warning(push)
// The padding from alignas below is intended.
#pragma warning(disable : 4324)
#endif

// Fixed-capacity ring buffer for exactly one producer thread and one consumer thread.
// Wait-free on both ends and never allocates, so either end may be the audio thread.
// Large capacities belong on the heap: the slots are stored inline.
template <typename T, std::size_t Capacity> class SpscQueue
{
    static_assert(Capacity >= 2 && std::has_single_bit(Capacity), "Capacity must be a power of two");
    static_assert(std::is_nothrow_default_constructible_v<T> && std::is_nothrow_move_assignable_v<T>);

  public:
    bool tryPush(T value) noexcept
    {
        const std::size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail - m_head.load(std::memory_order_acquire) == Capacity)
        {
            return false;
        }
        m_slots[tail & s_mask] = std::move(value);
        m_tail.store(tail + 1, std::memory_order_release);
        return true;
    }

    std::optional<T> tryPop() noexcept
    {
        const std::size_t head = m_head.load(std::memory_order_relaxed);
        if (head == m_tail.load(std::memory_order_acquire))
        {
            return std::nullopt;
        }
        std::optional<T> value{std::move(m_slots[head & s_mask])};
        m_head.store(head + 1, std::memory_order_release);
        return value;
    }

    static constexpr std::size_t capacity() noexcept
    {
        return Capacity;
    }

  private:
    static constexpr std::size_t s_mask = Capacity - 1;

    // Producer and consumer counters on separate cache lines to avoid false sharing.
    alignas(64) std::atomic<std::size_t> m_head{0};
    alignas(64) std::atomic<std::size_t> m_tail{0};
    std::array<T, Capacity> m_slots{};
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

} // namespace sessionflow
