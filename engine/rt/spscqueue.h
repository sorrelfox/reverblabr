// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "rt/realtime.h"

#include <atomic>
#include <cstddef>
#include <type_traits>
#include <vector>

namespace rb::rt
{

/// The distance the two ends of a queue are kept apart in memory, so that the
/// producer's index and the consumer's index never share a cache line and
/// every push does not cost the other thread a cache miss. Sixty-four bytes
/// is the line size of every processor we support;
/// std::hardware_destructive_interference_size is deliberately not used,
/// because its value is part of the ABI and changes with the compiler.
inline constexpr std::size_t kCacheLineBytes = 64;

/// A queue between exactly two threads, one pushing and one popping, that
/// never allocates, never locks and never waits.
///
/// This is how the engine thread sends parameter changes and commands into
/// the audio thread, and how the audio thread sends events back out. The
/// capacity is fixed when the queue is built, which is the point: a full
/// queue makes tryPush() fail, and the caller decides what that means, rather
/// than the audio thread stopping to allocate.
///
/// \a T is copied in and out by value, so it must be trivially copyable — a
/// parameter change, an event, a pointer. Anything that owns memory is
/// handed over as a pointer and freed elsewhere; see RetireQueue.
///
/// **One producer and one consumer.** Two threads pushing, or two popping,
/// is not supported and is not detected.
template <typename T>
class SpscQueue
{
    static_assert(std::is_trivially_copyable_v<T>, "a queue slot is copied, not constructed, on the audio thread");
    static_assert(std::is_default_constructible_v<T>, "the slots are made when the queue is built");

public:
    /// Builds a queue that holds \a capacity items. This allocates, so it
    /// belongs in start-up or prepare(), never on the audio thread.
    explicit SpscQueue(std::size_t capacity)
        : m_slots(capacity + 1) // one slot stays empty, to tell full from empty
    {
    }

    SpscQueue(const SpscQueue &) = delete;
    SpscQueue &operator=(const SpscQueue &) = delete;

    /// How many items the queue holds when it is full.
    [[nodiscard]] std::size_t capacity() const noexcept RB_NONBLOCKING
    {
        return m_slots.size() - 1;
    }

    /// Adds \a value, or returns false if the queue is full. Only the
    /// producing thread may call this.
    [[nodiscard]] bool tryPush(const T &value) noexcept RB_NONBLOCKING
    {
        const std::size_t write = m_writeIndex.load(std::memory_order_relaxed);
        const std::size_t next = nextIndex(write);
        if (next == m_readIndex.load(std::memory_order_acquire)) {
            return false;
        }
        m_slots[write] = value;
        // Releases the slot: a consumer that sees this index also sees the
        // value written just above it.
        m_writeIndex.store(next, std::memory_order_release);
        return true;
    }

    /// Takes the oldest item into \a value, or returns false if the queue is
    /// empty. Only the consuming thread may call this.
    [[nodiscard]] bool tryPop(T &value) noexcept RB_NONBLOCKING
    {
        const std::size_t read = m_readIndex.load(std::memory_order_relaxed);
        if (read == m_writeIndex.load(std::memory_order_acquire)) {
            return false;
        }
        value = m_slots[read];
        m_readIndex.store(nextIndex(read), std::memory_order_release);
        return true;
    }

    /// Whether the queue holds nothing. Exact on the consuming thread.
    [[nodiscard]] bool isEmpty() const noexcept RB_NONBLOCKING
    {
        return m_readIndex.load(std::memory_order_acquire) == m_writeIndex.load(std::memory_order_acquire);
    }

    /// How many items are waiting. Exact only if read from a thread while the
    /// other one is idle; otherwise it is a number that was true a moment ago,
    /// which is all a diagnostic needs.
    [[nodiscard]] std::size_t size() const noexcept RB_NONBLOCKING
    {
        const std::size_t write = m_writeIndex.load(std::memory_order_acquire);
        const std::size_t read = m_readIndex.load(std::memory_order_acquire);
        return write >= read ? write - read : m_slots.size() - read + write;
    }

private:
    [[nodiscard]] std::size_t nextIndex(std::size_t index) const noexcept RB_NONBLOCKING
    {
        const std::size_t next = index + 1;
        return next == m_slots.size() ? 0 : next;
    }

    std::vector<T> m_slots;
    alignas(kCacheLineBytes) std::atomic<std::size_t> m_writeIndex{0};
    alignas(kCacheLineBytes) std::atomic<std::size_t> m_readIndex{0};
};

} // namespace rb::rt
