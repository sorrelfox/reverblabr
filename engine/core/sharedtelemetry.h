// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/telemetry.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>

namespace rb::core
{

/// "RBTL", and the version of what follows it: a window that finds anything
/// else in the memory it was handed does not read it (ADR-0018).
///
/// - 1: levels, block values and the analysed block's spectrum.
/// - 2: the analysed block's spectrum before it, its band values and the
///   channel both spectra are of.
/// - 3: 128 band values rather than 64, for the 64-band block's gains and
///   levels.
inline constexpr std::uint32_t kTelemetryMagic = 0x4c544252;
inline constexpr std::uint32_t kTelemetryVersion = 3;

/// **A fingerprint of the snapshot's layout**: its size, its blocks' size
/// and every bound that shapes it. Two builds that would read the same
/// bytes differently disagree here even when nobody remembered to raise
/// kTelemetryVersion, which is the mistake a version number alone cannot
/// catch.
[[nodiscard]] constexpr std::uint64_t telemetryLayout() noexcept
{
    const std::uint64_t parts[] = {
        sizeof(TelemetrySnapshot),
        alignof(TelemetrySnapshot),
        sizeof(BlockTelemetry),
        offsetof(TelemetrySnapshot, blocks),
        offsetof(TelemetrySnapshot, inputSpectrum),
        offsetof(TelemetrySnapshot, bandValues),
        sizeof(TelemetrySlot),
        kMaxMeteredBlocks,
        kMaxTelemetryIdBytes,
        kSpectrumBands,
        rt::kMaxMeterChannels,
        rt::kMaxMeterValues,
        rt::kMaxBandValues,
    };
    // FNV-1a, a part at a time: any change to any of them changes this.
    std::uint64_t hash = 14695981039346656037ULL;
    for (const std::uint64_t part : parts) {
        hash = (hash ^ part) * 1099511628211ULL;
    }
    return hash;
}

/// **One session's telemetry in memory another process can map** (ADR-0018).
///
/// The seqlock the sampler has always published through, unchanged: a
/// sequence number and an array of 32-bit words, every one a lock-free
/// atomic, so it means the same thing mapped into two processes at two
/// addresses. The header in front says what it is.
struct TelemetryRegion {
    std::uint32_t magic = kTelemetryMagic;
    std::uint32_t version = kTelemetryVersion;
    std::uint64_t layout = telemetryLayout();

    alignas(64) TelemetrySlot slot;

    static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "shared between processes, so never a lock");
};

enum class SharedTelemetryError : std::uint8_t {
    NotCreated, ///< memfd_create, ftruncate, the seals or mmap refused.
    NotMapped, ///< The descriptor could not be mapped.
    NotSealed, ///< Its size could still change under the reader.
    WrongLayout, ///< The memory does not hold a TelemetryRegion of this build's layout.
};

/// A TelemetryRegion in a memfd: the engine's, which it writes, or a
/// window's read-only mapping of it.
///
/// **The engine's is sealed at its size** (F_SEAL_SHRINK, F_SEAL_GROW), so
/// a window's mapping can never find the file shorter than the region and
/// take a SIGBUS for it; a window refuses one that is not. **A window's is
/// read-only** and keeps no descriptor open: the mapping holds the memory
/// for as long as it exists, and letting go of it lets go of everything.
class SharedTelemetry
{
public:
    /// A fresh region, with nothing published yet, in a new sealed memfd.
    [[nodiscard]] static std::expected<SharedTelemetry, SharedTelemetryError> create();
    /// The region behind \a fd, checked and mapped read-only. Takes
    /// ownership of \a fd and closes it, mapped or not.
    [[nodiscard]] static std::expected<SharedTelemetry, SharedTelemetryError> map(int fd);

    SharedTelemetry(SharedTelemetry &&other) noexcept;
    SharedTelemetry &operator=(SharedTelemetry &&other) noexcept;
    SharedTelemetry(const SharedTelemetry &) = delete;
    SharedTelemetry &operator=(const SharedTelemetry &) = delete;
    ~SharedTelemetry();

    [[nodiscard]] const TelemetrySlot &slot() const noexcept
    {
        return m_region->slot;
    }

    /// The slot to publish into, or null for a window's read-only mapping.
    [[nodiscard]] TelemetrySlot *writableSlot() noexcept
    {
        return m_fd >= 0 ? &m_region->slot : nullptr;
    }

    /// **A new descriptor for the same memory, opened read-only**, for
    /// handing to a window: it can map the region but never write it. The
    /// caller owns it. -1 for a window's mapping, which has no descriptor,
    /// or when the reopen is refused.
    [[nodiscard]] int shareReadOnly() const;

private:
    SharedTelemetry(int fd, TelemetryRegion *region) noexcept
        : m_fd(fd)
        , m_region(region)
    {
    }

    /// The memfd, for the engine's own; -1 for a window's.
    int m_fd = -1;
    TelemetryRegion *m_region = nullptr;
};

/// **Where one session's snapshots are published**: a TelemetrySlot in
/// shared memory that a window in another process can map, or, when the
/// kernel will not make a memfd, one of this process's own that nobody
/// outside can read. **Neither ever moves**, moved or not, because the
/// interface keeps the address it was given.
class PublishedSlot
{
public:
    PublishedSlot();

    [[nodiscard]] TelemetrySlot &slot() noexcept
    {
        return m_shared ? *m_shared->writableSlot() : *m_own;
    }

    [[nodiscard]] const TelemetrySlot &slot() const noexcept
    {
        return m_shared ? m_shared->slot() : *m_own;
    }

    /// A read-only descriptor for it, for a window in another process; the
    /// caller owns it. -1 when it is not in shared memory.
    [[nodiscard]] int shareReadOnly() const
    {
        return m_shared ? m_shared->shareReadOnly() : -1;
    }

private:
    std::optional<SharedTelemetry> m_shared;
    std::unique_ptr<TelemetrySlot> m_own;
};

} // namespace rb::core
