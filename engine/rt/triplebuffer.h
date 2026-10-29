// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "rt/realtime.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace rb::rt
{

/// Three copies of a value, so that a writer can fill one while a reader uses
/// another and neither ever waits.
///
/// Like SeqlockValue this carries "the latest state" one way, and the reader
/// may miss values. The difference is where the copying happens: a seqlock
/// copies the value on every read and every write, while here the writer
/// fills a slot in place and publishing is one atomic exchange, whatever the
/// value's size. That makes this the one to use for a block of meter data or
/// a whole telemetry frame, and the seqlock the one for a handful of floats.
///
/// The writer owns one slot, the reader owns another and a third is always
/// free, which is what lets a publish happen while a read is in progress. No
/// read ever sees a half-filled slot, and no attempt ever fails.
///
/// **One writer and one reader**, each keeping to its own methods:
///
///     Telemetry &slot = buffer.writeSlot();  // audio thread
///     slot.peak = ...;
///     buffer.publish();
///
///     const Telemetry &latest = buffer.read(); // sampler thread
template <typename T>
class TripleBuffer
{
    static_assert(std::is_default_constructible_v<T>, "the three slots are made when the buffer is built");

public:
    TripleBuffer() = default;

    explicit TripleBuffer(const T &initial)
        : m_slots{initial, initial, initial}
    {
    }

    TripleBuffer(const TripleBuffer &) = delete;
    TripleBuffer &operator=(const TripleBuffer &) = delete;

    /// The slot the writer fills. It stays the writer's until publish(), and
    /// what it holds is whatever was left there two publishes ago, so fill
    /// every field.
    [[nodiscard]] T &writeSlot() noexcept RB_NONBLOCKING
    {
        return m_slots[m_writeIndex];
    }

    /// Makes the filled slot the latest one and takes the free slot to fill
    /// next. Only the writing thread may call this.
    void publish() noexcept RB_NONBLOCKING
    {
        const std::uint32_t previous = m_shared.exchange(m_writeIndex | kFresh, std::memory_order_acq_rel);
        m_writeIndex = previous & kIndexMask;
    }

    /// Whether a value has been published that this reader has not taken yet.
    [[nodiscard]] bool hasNewValue() const noexcept RB_NONBLOCKING
    {
        return (m_shared.load(std::memory_order_acquire) & kFresh) != 0;
    }

    /// The latest published value, or the one read before if nothing new has
    /// been published. The reference stays valid and unchanged until the next
    /// call to read(). Only the reading thread may call this.
    [[nodiscard]] const T &read() noexcept RB_NONBLOCKING
    {
        if ((m_shared.load(std::memory_order_relaxed) & kFresh) != 0) {
            const std::uint32_t previous = m_shared.exchange(m_readIndex, std::memory_order_acq_rel);
            m_readIndex = previous & kIndexMask;
        }
        return m_slots[m_readIndex];
    }

private:
    /// Set on the shared index while it names a slot nobody has read yet.
    static constexpr std::uint32_t kFresh = 0b100;
    static constexpr std::uint32_t kIndexMask = 0b011;

    std::array<T, 3> m_slots{};
    /// Which slot holds the latest value, plus the flag above. The writer and
    /// the reader swap their own slot for it, so the three indices are always
    /// a slot each and never the same one twice.
    std::atomic<std::uint32_t> m_shared{0};
    std::uint32_t m_writeIndex = 1;
    std::uint32_t m_readIndex = 2;
};

} // namespace rb::rt
