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
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace rb::rt
{

/// One value that a real-time writer publishes and any number of readers
/// take a consistent copy of, without either side ever waiting for the other.
///
/// This is the telemetry slot: the audio thread writes a meter reading or a
/// gain reduction at the end of every cycle, and the sampler that feeds the
/// interface reads whatever is there whenever it likes. A reader that catches
/// the writer mid-update notices and reads again; the writer is never held up
/// and never sees the readers at all.
///
/// A reader therefore sees *a* value the writer published, never half of one
/// and half of another. It may miss values entirely, which is what a meter
/// wants: the current reading matters and the one before it does not.
///
/// \a T must be trivially copyable, and small: it is copied word by word on
/// every read and write, and a reader that keeps losing the race to a large
/// value will keep retrying. Anything bigger than a few dozen bytes belongs
/// in a TripleBuffer instead, which hands over a pointer rather than a copy.
///
/// The value is held as plain 32-bit words, each read and written as its own
/// atomic. Copying it as an ordinary struct instead would be a data race by
/// the letter of the language, whatever the processor makes of it, and
/// ThreadSanitizer would rightly say so. The words carry acquire and release
/// ordering themselves rather than being fenced as a group, because a
/// standalone fence is something ThreadSanitizer cannot model, and a
/// primitive this delicate is worth being able to check.
template <typename T>
class SeqlockValue
{
    static_assert(std::is_trivially_copyable_v<T>, "a snapshot is copied byte for byte");

public:
    SeqlockValue() = default;

    explicit SeqlockValue(const T &initial) noexcept RB_NONBLOCKING
    {
        store(initial);
    }

    SeqlockValue(const SeqlockValue &) = delete;
    SeqlockValue &operator=(const SeqlockValue &) = delete;

    /// Publishes \a value. Only one thread may write.
    void store(const T &value) noexcept RB_NONBLOCKING
    {
        std::array<std::uint32_t, kWords> words{};
        // The cast to void * is how a deliberate byte-for-byte copy of a
        // trivially copyable type is spelled; without it the compiler warns
        // about memcpy on a class type, which is exactly what this is.
        std::memcpy(words.data(), static_cast<const void *>(&value), sizeof(T));

        const std::uint32_t sequence = m_sequence.load(std::memory_order_relaxed);
        // An odd sequence number means "being written"; a reader that sees
        // one, or sees the number change, reads again.
        m_sequence.store(sequence + 1, std::memory_order_relaxed);
        for (std::size_t word = 0; word < kWords; ++word) {
            // Releasing each word is what keeps it behind the odd sequence
            // number above: a reader that sees a word of this update also
            // sees that the update had started.
            m_words[word].store(words[word], std::memory_order_release);
        }
        m_sequence.store(sequence + 2, std::memory_order_release);
    }

    /// The most recently published value. Reads again for as long as it takes
    /// to catch the writer between two updates, so a reader on the audio
    /// thread should use tryLoad() instead and carry on if it loses.
    [[nodiscard]] T load() const noexcept
    {
        T value{};
        while (!tryLoadOnce(value)) {
            // Nothing to do but read again: the writer is mid-update and is
            // never waiting for us.
        }
        return value;
    }

    /// Puts the most recently published value in \a value, giving up after
    /// \a attempts tries. Returns false if every one of them caught the
    /// writer mid-update, in which case \a value is untouched.
    [[nodiscard]] bool tryLoad(T &value, unsigned attempts = 4) const noexcept RB_NONBLOCKING
    {
        for (unsigned attempt = 0; attempt < attempts; ++attempt) {
            if (tryLoadOnce(value)) {
                return true;
            }
        }
        return false;
    }

    /// How many values have been published. Useful to tell a value that is
    /// repeated from one that is merely old.
    [[nodiscard]] std::uint64_t publishCount() const noexcept RB_NONBLOCKING
    {
        return m_sequence.load(std::memory_order_acquire) / 2;
    }

private:
    static constexpr std::size_t kWords = (sizeof(T) + sizeof(std::uint32_t) - 1) / sizeof(std::uint32_t);

    [[nodiscard]] bool tryLoadOnce(T &value) const noexcept RB_NONBLOCKING
    {
        const std::uint32_t before = m_sequence.load(std::memory_order_acquire);
        if (before % 2 != 0) {
            return false; // a write is in progress
        }
        std::array<std::uint32_t, kWords> words{};
        for (std::size_t word = 0; word < kWords; ++word) {
            // Acquiring each word keeps the second look at the sequence
            // number below it, so a word that was rewritten while we read
            // cannot be paired with a sequence number from before it was.
            words[word] = m_words[word].load(std::memory_order_acquire);
        }
        if (m_sequence.load(std::memory_order_acquire) != before) {
            return false; // a write started and possibly finished while we read
        }
        std::memcpy(static_cast<void *>(&value), words.data(), sizeof(T));
        return true;
    }

    std::atomic<std::uint32_t> m_sequence{0};
    std::array<std::atomic<std::uint32_t>, kWords> m_words{};
};

} // namespace rb::rt
