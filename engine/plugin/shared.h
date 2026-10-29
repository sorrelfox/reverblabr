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
#include <expected>

namespace rb::plugin
{

/// "RBPH", and the layout's version: a host that finds anything else in the
/// memory it was handed refuses to run rather than misread it.
inline constexpr std::uint32_t kSharedMagic = 0x48504252;
inline constexpr std::uint32_t kProtocolVersion = 1;

/// How many parameter changes can be on their way to a host at once. A
/// knob dragged across its range sends a few hundred a second; a host drains
/// them every cycle.
inline constexpr std::size_t kParamSlots = 1024;

/// One parameter change: which of the plugin's parameters, and its value.
struct ParamChange {
    std::uint32_t index = 0;
    float value = 0.0f;
};

/// **The memory an engine and one plugin host share** (ADR-0014).
///
/// Everything in it is a plain value or a lock-free atomic, so it means the
/// same thing mapped into two processes at two addresses. Two channels go
/// through it, each with one writer:
///
/// - **parameters, engine to host**: a ring of ParamChange, pushed by the
///   engine's thread and drained by the host's audio thread at the start of
///   every cycle, so a knob is heard from the next cycle on with nothing
///   allocated, locked or sent through a socket;
/// - **what the host says of itself, host to engine**: a count of the cycles
///   it has run, which is its heartbeat — the engine's watchdog knows a host
///   that has stopped by this count standing still while the audio around
///   it runs — and the latency its plugin reports.
///
/// State, presets and anything else that owns memory go through the control
/// socket instead (messages.h).
struct SharedBlock {
    std::uint32_t magic = kSharedMagic;
    std::uint32_t version = kProtocolVersion;

    alignas(64) std::atomic<std::uint64_t> paramWrite{0};
    alignas(64) std::atomic<std::uint64_t> paramRead{0};
    std::array<ParamChange, kParamSlots> params{};
    /// Changes that did not fit, which a full ring refuses rather than waits
    /// for. Counted, so a test can say none were.
    std::atomic<std::uint64_t> paramsDropped{0};

    alignas(64) std::atomic<std::uint64_t> cycles{0};
    std::atomic<std::uint32_t> latencySamples{0};
    /// The rate the host's plugin is prepared for, and the one PipeWire runs
    /// it at when the two differ (zero when they agree).
    std::atomic<std::uint32_t> preparedRate{0};
    std::atomic<std::uint32_t> rateWanted{0};

    static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "shared between processes, so never a lock");
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "shared between processes, so never a lock");
};

/// Queues a change for the host. False when the ring is full; the change is
/// counted as dropped, since a value that late is out of date anyway. The
/// engine's thread only.
bool pushParam(SharedBlock &shared, const ParamChange &change) noexcept;

/// Takes the oldest change, or false if there is none. The host's audio
/// thread only.
[[nodiscard]] bool popParam(SharedBlock &shared, ParamChange &change) noexcept RB_NONBLOCKING;

enum class SharedError : std::uint8_t {
    NotCreated, ///< memfd_create, ftruncate or mmap refused.
    NotMapped, ///< The descriptor could not be mapped.
    WrongLayout, ///< The memory does not hold a SharedBlock of this version.
};

/// One SharedBlock in memory backed by a file descriptor, which is how it
/// crosses into the host: the engine creates it and hands the descriptor to
/// the host at exec.
class SharedMemory
{
public:
    /// A fresh block, initialised, in a new memfd.
    [[nodiscard]] static std::expected<SharedMemory, SharedError> create();
    /// The block behind \a fd, checked. Takes ownership of \a fd.
    [[nodiscard]] static std::expected<SharedMemory, SharedError> map(int fd);

    SharedMemory(SharedMemory &&other) noexcept;
    SharedMemory &operator=(SharedMemory &&other) noexcept;
    SharedMemory(const SharedMemory &) = delete;
    SharedMemory &operator=(const SharedMemory &) = delete;
    ~SharedMemory();

    [[nodiscard]] SharedBlock &block() const noexcept
    {
        return *m_block;
    }

    [[nodiscard]] int fd() const noexcept
    {
        return m_fd;
    }

private:
    SharedMemory(int fd, SharedBlock *block) noexcept
        : m_fd(fd)
        , m_block(block)
    {
    }

    int m_fd = -1;
    SharedBlock *m_block = nullptr;
};

} // namespace rb::plugin
