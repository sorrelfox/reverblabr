// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace rb::model
{

/// "RMDL", and the layout's version: a host that finds anything else in the
/// memory it was handed refuses to run rather than misread it.
inline constexpr std::uint32_t kSharedMagic = 0x4C444D52;
inline constexpr std::uint32_t kProtocolVersion = 1;
/// A session's lane: every Model block has two channels each way.
inline constexpr std::uint32_t kRingChannels = 2;

/// How far a host has come.
enum class Phase : std::uint32_t {
    Starting = 0, ///< No host has said anything yet.
    Loading = 1, ///< The model is being read and tried.
    Ready = 2, ///< Runs are going into the rings.
    Failed = 3, ///< It will not run: see SharedHeader::reason.
};

/// What ran the model, for the panel.
enum class Device : std::uint32_t {
    Unknown = 0,
    Cpu = 1,
    Gpu = 2,
};

/// **The start of the memory a block and its model host share** (ADR-0031).
/// The rings follow it. Each value has one writer. Plain values and lock-free
/// atomics only, so it means the same mapped at two addresses.
///
/// **The block treats everything the host writes as untrusted**: it keeps
/// its own copy of the layout and clamps what it reads.
struct SharedHeader {
    std::uint32_t magic = kSharedMagic;
    std::uint32_t version = kProtocolVersion;
    std::uint32_t channels = kRingChannels;
    /// Frames in each ring, a power of two.
    std::uint32_t capacity = 0;

    /// **Written by the block.** Input frames written so far, from the start.
    alignas(64) std::atomic<std::uint64_t> written{0};
    /// Moved on when the signal path is cut: the host starts clean.
    std::atomic<std::uint32_t> resetEpoch{0};

    /// **Written by the host.** Output frames in [wetFrom, wetEnd) hold the
    /// model's answer to the input frames of the same number.
    alignas(64) std::atomic<std::uint64_t> wetFrom{0};
    std::atomic<std::uint64_t> wetEnd{0};
    std::atomic<std::uint32_t> phase{static_cast<std::uint32_t>(Phase::Starting)};
    /// A Refusal, while phase is Failed.
    std::atomic<std::uint32_t> reason{0};
    /// A run's time as thousandths of the sound it makes, smoothed: above
    /// a thousand, the model is slower than the sound.
    std::atomic<std::uint32_t> load{0};
    std::atomic<std::uint32_t> device{static_cast<std::uint32_t>(Device::Unknown)};
    /// Moves on with every turn of the host's loop.
    std::atomic<std::uint64_t> beat{0};
    /// How many times the host fell so far behind that it skipped ahead.
    std::atomic<std::uint64_t> skipped{0};
    /// How many runs gave back something that is not sound: they count as
    /// silence.
    std::atomic<std::uint64_t> badRuns{0};

    static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "shared between processes, so never a lock");
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "shared between processes, so never a lock");
};

enum class SharedError : std::uint8_t {
    NotCreated, ///< memfd_create, ftruncate or mmap refused.
    NotMapped, ///< The descriptor could not be mapped.
    WrongLayout, ///< The memory does not hold rings of this version.
};

/// The rings a block and its host share, in memory backed by a descriptor,
/// which is how they cross into the host at exec.
class SharedRings
{
public:
    /// Fresh rings of at least \a frames frames, rounded up to a power of two.
    [[nodiscard]] static std::expected<SharedRings, SharedError> create(std::uint32_t frames);
    /// The rings behind \a fd, checked. Takes ownership of \a fd.
    [[nodiscard]] static std::expected<SharedRings, SharedError> map(int fd);

    SharedRings(SharedRings &&other) noexcept;
    SharedRings &operator=(SharedRings &&other) noexcept;
    SharedRings(const SharedRings &) = delete;
    SharedRings &operator=(const SharedRings &) = delete;
    ~SharedRings();

    [[nodiscard]] SharedHeader &header() const noexcept
    {
        return *static_cast<SharedHeader *>(m_memory);
    }

    /// The ring of input or output frames of \a channel.
    [[nodiscard]] std::span<float> input(std::uint32_t channel) const noexcept;
    [[nodiscard]] std::span<float> output(std::uint32_t channel) const noexcept;

    /// Frames in each ring, as this side knows it: never read back from the
    /// memory, which the other side can write.
    [[nodiscard]] std::uint32_t capacity() const noexcept
    {
        return m_capacity;
    }

    [[nodiscard]] int fd() const noexcept
    {
        return m_fd;
    }

private:
    SharedRings(int fd, void *memory, std::size_t bytes, std::uint32_t capacity) noexcept
        : m_fd(fd)
        , m_memory(memory)
        , m_bytes(bytes)
        , m_capacity(capacity)
    {
    }

    int m_fd = -1;
    void *m_memory = nullptr;
    std::size_t m_bytes = 0;
    std::uint32_t m_capacity = 0;
};

} // namespace rb::model
