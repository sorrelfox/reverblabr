// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "rt/realtime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>

namespace rb::rt
{

/// **The sound itself, for surfaces that draw it**: the path a Waveform block
/// writes into and a window reads, one writer and any number of readers, in
/// memory a window maps read-only (ADR-0018).
///
/// A snapshot is a place for numbers about the sound and is copied whole
/// sixty times a second, so sound does not go in it. Here each block that is
/// being looked at has a slot: the audio thread writes straight into rings in
/// the shared memory, and a window copies out as much as it wants, as often
/// as it wants, without anybody between them. Nothing in a slot depends on the
/// sampler, so a window can read it faster than the sampler runs.
///
/// **A slot keeps the sound at four resolutions**, each ring eight times as
/// coarse as the last: every sample, then the lowest and the highest of every
/// 8, 64 and 512. So how much of the past a surface shows is the surface's
/// business alone. It takes the finest ring that reaches back far enough, and
/// nothing on the audio thread has to change or start again when somebody
/// drags a slider. Holding the extremes rather than a mean is what lets a
/// single click stay in a ten-second view.
///
/// **Four traces, not two**: left, right, mid and side. The lowest of a mid
/// can't be worked out from the lowest of the left and of the right, because
/// the two need not happen on the same sample: a left and right in opposite
/// phase have a mid of nothing, and adding their extremes would draw it at
/// full height.
///
/// **A reader finds out what it lost, and never gets half a point.** The
/// writer does not wait for readers and does not know how many there are. It
/// goes round and overwrites, so a reader copies what it wants and then looks
/// at where the writer has got to: what the writer may have reached by then
/// is dropped from the old end of the copy. readScope() does this.
///
/// Everything here is plain words reached through std::atomic_ref, not
/// std::atomic: an atomic zeroes itself when it is constructed, which would
/// touch every page of a pool most sessions never use. A memfd reads as zeroes
/// until written, and costs nothing until then.
inline constexpr std::size_t kScopeTraces = 4;
/// How many rings a slot has, and how much coarser each is than the last.
inline constexpr std::size_t kScopeLevels = 4;
inline constexpr std::uint32_t kScopeLevelStep = 8;
/// How many points a ring holds: a power of two.
inline constexpr std::uint32_t kScopePoints = 8192;
/// How many blocks of one session can be drawn at once.
inline constexpr std::size_t kScopeSlots = 16;

static_assert((kScopePoints & (kScopePoints - 1)) == 0, "a ring is indexed by masking");

/// Which trace of a point is which.
enum ScopeTrace : std::uint32_t {
    ScopeLeft = 0,
    ScopeRight,
    ScopeMid, ///< (left + right) / 2
    ScopeSide, ///< (left - right) / 2
};

/// How many samples one point of ring \a level stands for.
[[nodiscard]] constexpr std::uint32_t scopeFramesPerPoint(std::size_t level) noexcept
{
    std::uint32_t frames = 1;
    for (std::size_t step = 0; step < level; ++step) {
        frames *= kScopeLevelStep;
    }
    return frames;
}

/// The lowest and highest of each trace over one point's samples. In the
/// finest ring the two are the same sample.
struct ScopePoint {
    float low[kScopeTraces];
    float high[kScopeTraces];
};

/// The points one writer goes round, and how far it has got. \a head counts
/// points from the start of the slot's generation and wraps at 2^32: every
/// point before it is whole, and the point at head % kScopePoints is the one
/// that may be being written.
struct ScopeRing {
    alignas(64) std::uint32_t head;
    alignas(64) ScopePoint points[kScopePoints];
};

/// One block's rings. \a generation moves on whenever the slot is given out,
/// so that a reader never takes what it holds for a continuation, and is odd
/// while that is going on.
struct ScopeSlot {
    alignas(64) std::uint32_t generation;
    /// The rate the samples are at, written by the writer.
    std::uint32_t sampleRate;
    ScopeRing rings[kScopeLevels];
};

/// One session's slots. A plain block of memory: see above for why.
struct ScopePool {
    ScopeSlot slots[kScopeSlots];
};

static_assert(std::is_trivially_copyable_v<ScopePoint> && std::is_trivially_default_constructible_v<ScopePool>,
              "a pool must be usable as the zeroes a memfd starts with");
static_assert(std::atomic_ref<std::uint32_t>::is_always_lock_free && std::atomic_ref<float>::is_always_lock_free,
              "shared between processes, so never a lock");

namespace scopeword
{

template <typename T>
[[nodiscard]] inline T load(const T &word, std::memory_order order) noexcept RB_NONBLOCKING
{
    // **The const_cast writes nothing.** atomic_ref takes no const type
    // before C++26, and a lock-free load of a word is a plain read, which is
    // what a window's read-only mapping allows.
    return std::atomic_ref<T>(const_cast<T &>(word)).load(order);
}

template <typename T>
inline void store(T &word, T value, std::memory_order order) noexcept RB_NONBLOCKING
{
    std::atomic_ref<T>(word).store(value, order);
}

} // namespace scopeword

/// What a writer needs between cycles: the half-filled point of each coarser
/// ring. One a block, held by the block, so that it is the audio thread's
/// alone and is gone with the block.
class ScopeWriter
{
public:
    /// Adds a cycle of \a frames samples to \a slot. Null is a side of
    /// silence. Real-time: a slot's pages were touched when it was given out
    /// (claimScopeSlot()), so this does not fault either.
    void write(ScopeSlot &slot,
               const float *left,
               const float *right,
               std::uint32_t frames,
               std::uint32_t sampleRate) noexcept RB_NONBLOCKING
    {
        const std::uint32_t generation = scopeword::load(slot.generation, std::memory_order_acquire);
        if (&slot != m_slot || generation != m_generation) {
            // New to this writer, which may be a block made anew around a
            // meter that had the slot already: it goes on from where the
            // rings have got to, never from nought, and what was half formed
            // was for somebody else.
            m_slot = &slot;
            m_generation = generation;
            m_rate = 0;
            forget();
            for (std::size_t level = 0; level < kScopeLevels; ++level) {
                m_heads[level] = scopeword::load(slot.rings[level].head, std::memory_order_relaxed);
            }
        }
        if (sampleRate != m_rate) {
            m_rate = sampleRate;
            scopeword::store(slot.sampleRate, sampleRate, std::memory_order_release);
        }
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const float first = left != nullptr ? left[frame] : 0.0f;
            const float second = right != nullptr ? right[frame] : 0.0f;
            const float mid = 0.5f * (first + second);
            const float side = 0.5f * (first - second);
            const ScopePoint sample{{first, second, mid, side}, {first, second, mid, side}};
            put(slot.rings[0], 0, sample);
            carry(slot, sample);
        }
    }

    /// Lets go of the half-filled points, for a sound that has broken off.
    void forget() noexcept RB_NONBLOCKING
    {
        for (Forming &forming : m_forming) {
            forming.frames = 0;
        }
    }

private:
    struct Forming {
        ScopePoint point;
        std::uint32_t frames = 0;
    };

    void put(ScopeRing &ring, std::size_t level, const ScopePoint &point) noexcept RB_NONBLOCKING
    {
        const std::uint32_t head = m_heads[level];
        ScopePoint &into = ring.points[head & (kScopePoints - 1)];
        // **Releasing each word** is what keeps them behind the head that was
        // published at the end of the last point: a reader that sees a word
        // of this point also sees the head that says this slot has gone. The
        // same reasoning as SeqlockValue, and no fence, which ThreadSanitizer
        // can't follow.
        for (std::size_t trace = 0; trace < kScopeTraces; ++trace) {
            scopeword::store(into.low[trace], point.low[trace], std::memory_order_release);
            scopeword::store(into.high[trace], point.high[trace], std::memory_order_release);
        }
        m_heads[level] = head + 1;
        scopeword::store(ring.head, head + 1, std::memory_order_release);
    }

    /// Folds \a point into the coarser rings. Every sample passes through
    /// the first one, and one in eight on to the next.
    void carry(ScopeSlot &slot, const ScopePoint &point) noexcept RB_NONBLOCKING
    {
        ScopePoint passing = point;
        for (std::size_t level = 1; level < kScopeLevels; ++level) {
            Forming &forming = m_forming[level - 1];
            if (forming.frames == 0) {
                forming.point = passing;
            } else {
                for (std::size_t trace = 0; trace < kScopeTraces; ++trace) {
                    // A NaN compares false and stays out of the extremes,
                    // unless it came first, when the next sample replaces it.
                    const float low = forming.point.low[trace];
                    const float high = forming.point.high[trace];
                    forming.point.low[trace] = passing.low[trace] < low || low != low ? passing.low[trace] : low;
                    forming.point.high[trace] = passing.high[trace] > high || high != high ? passing.high[trace] : high;
                }
            }
            if (++forming.frames < kScopeLevelStep) {
                return;
            }
            forming.frames = 0;
            put(slot.rings[level], level, forming.point);
            passing = forming.point;
        }
    }

    ScopeSlot *m_slot = nullptr;
    std::uint32_t m_generation = 0;
    std::uint32_t m_rate = 0;
    std::array<std::uint32_t, kScopeLevels> m_heads{};
    std::array<Forming, kScopeLevels - 1> m_forming{};
};

/// What readScope() found.
struct ScopeRead {
    std::uint32_t generation = 0;
    std::uint32_t sampleRate = 0;
    /// The ring's head when it was read: the newest point copied is the one
    /// before this.
    std::uint32_t head = 0;
    /// How many points were copied, oldest first. Fewer than asked for when
    /// the ring holds fewer, or the writer overtook part of the copy.
    std::size_t points = 0;
};

/// Copies the newest points of \a slot's ring \a level into \a out, as many
/// as it holds. Any thread, any number of readers, on read-only memory. Takes
/// as long as the copy, and may come back with nothing while the slot is being
/// given out.
///
/// One point is never available: the one the writer may be in. A ring never
/// gives more than kScopePoints - 1.
[[nodiscard]] inline ScopeRead
readScope(const ScopeSlot &slot, std::size_t level, std::span<ScopePoint> out) noexcept RB_NONBLOCKING
{
    ScopeRead read;
    if (level >= kScopeLevels) {
        return read;
    }
    const ScopeRing &ring = slot.rings[level];
    read.generation = scopeword::load(slot.generation, std::memory_order_acquire);
    if (read.generation % 2 != 0) {
        return read;
    }
    read.sampleRate = scopeword::load(slot.sampleRate, std::memory_order_acquire);
    read.head = scopeword::load(ring.head, std::memory_order_acquire);

    const std::size_t count =
        std::min(std::min(static_cast<std::size_t>(read.head), static_cast<std::size_t>(kScopePoints - 1)), out.size());
    const std::uint32_t first = read.head - static_cast<std::uint32_t>(count);
    for (std::size_t index = 0; index < count; ++index) {
        const ScopePoint &from = ring.points[(first + static_cast<std::uint32_t>(index)) & (kScopePoints - 1)];
        ScopePoint &into = out[index];
        for (std::size_t trace = 0; trace < kScopeTraces; ++trace) {
            // Acquiring each word keeps the second look at the head below
            // them, as in SeqlockValue.
            into.low[trace] = scopeword::load(from.low[trace], std::memory_order_acquire);
            into.high[trace] = scopeword::load(from.high[trace], std::memory_order_acquire);
        }
    }

    const std::uint32_t headAfter = scopeword::load(ring.head, std::memory_order_acquire);
    if (scopeword::load(slot.generation, std::memory_order_acquire) != read.generation) {
        return read;
    }
    // A slot's new point overwrites the one a ring behind it, so the writer
    // at headAfter may be inside the slot of headAfter - kScopePoints.
    // Compared by distance, so that the count may wrap.
    const auto spoiled = static_cast<std::int32_t>(headAfter - kScopePoints + 1U - first);
    if (spoiled > 0) {
        const auto gone = std::min(static_cast<std::size_t>(spoiled), count);
        std::copy(out.begin() + static_cast<std::ptrdiff_t>(gone),
                  out.begin() + static_cast<std::ptrdiff_t>(count),
                  out.begin());
        read.points = count - gone;
    } else {
        read.points = count;
    }
    return read;
}

/// **Gets \a slot ready to be written and says so to readers.** Engine thread,
/// with no writer on the slot. Touches every page, so that the audio thread
/// never takes a page fault for a page nobody had used: on a memfd that is
/// the kernel finding memory, which can take as long as it likes.
inline void claimScopeSlot(ScopeSlot &slot) noexcept
{
    const std::uint32_t generation = scopeword::load(slot.generation, std::memory_order_relaxed);
    scopeword::store(slot.generation, generation + 1, std::memory_order_release);
    constexpr std::size_t kPointsPerPage = 4096 / sizeof(ScopePoint);
    for (ScopeRing &ring : slot.rings) {
        scopeword::store(ring.head, std::uint32_t{0}, std::memory_order_release);
        for (std::size_t point = 0; point < kScopePoints; point += kPointsPerPage) {
            scopeword::store(ring.points[point].low[0], 0.0f, std::memory_order_relaxed);
        }
        // A slot starts part of the way into a page, so the last points are
        // on a page the steps above went past.
        scopeword::store(ring.points[kScopePoints - 1].low[0], 0.0f, std::memory_order_relaxed);
    }
    scopeword::store(slot.generation, generation + 2, std::memory_order_release);
}

} // namespace rb::rt
