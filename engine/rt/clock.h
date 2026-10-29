// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "rt/realtime.h"

#include <cstdint>
#include <ctime>

namespace rb::rt
{

/// Nanoseconds since an arbitrary fixed point, readable on the audio thread.
///
/// This is how a process callback times itself, which is where the DSP load
/// figure comes from. `CLOCK_MONOTONIC` is answered by the vDSO: the call
/// reads a page the kernel keeps up to date and returns, with no system call,
/// no lock and no allocation. It is also monotonic, so a change of the
/// wall-clock time cannot make a cycle look negative.
///
/// The compiler cannot know any of that — `clock_gettime` is an ordinary C
/// function to it, and calling it from a function marked RB_NONBLOCKING would
/// rightly be a warning. So the call goes through a pointer that carries the
/// claim, and the claim is stated here, once, rather than at each use.
[[nodiscard]] inline std::uint64_t monotonicNanos() noexcept RB_NONBLOCKING
{
    using ClockGetTime = int (*)(clockid_t, timespec *) RB_NONBLOCKING;
    const auto read = reinterpret_cast<ClockGetTime>(&clock_gettime);

    timespec now{};
    if (read(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(now.tv_sec) * 1'000'000'000ULL + static_cast<std::uint64_t>(now.tv_nsec);
}

} // namespace rb::rt
