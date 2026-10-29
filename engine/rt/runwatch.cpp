// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "rt/runwatch.h"

#include <sched.h>
#include <sys/resource.h>

namespace rb::rt
{

// Both calls are plain system calls: no lock, no allocation, bounded. The
// compiler cannot know that of a C function, so, as with monotonicNanos(),
// each goes through a pointer that carries the claim.

bool readThreadRun(ThreadRunReading &reading) noexcept RB_NONBLOCKING
{
    using GetRusage = int (*)(int, rusage *) RB_NONBLOCKING;
    const auto read = reinterpret_cast<GetRusage>(&getrusage);

    rusage usage{};
    if (read(RUSAGE_THREAD, &usage) != 0) {
        return false;
    }
    const auto nanos = [](const timeval &time) {
        return static_cast<std::uint64_t>(time.tv_sec) * 1'000'000'000ULL
            + static_cast<std::uint64_t>(time.tv_usec) * 1'000ULL;
    };
    reading.cpuNanos = nanos(usage.ru_utime) + nanos(usage.ru_stime);
    reading.sleeps = usage.ru_nvcsw;
    return true;
}

bool callingThreadIsRealtime() noexcept RB_NONBLOCKING
{
    using GetScheduler = int (*)(pid_t) RB_NONBLOCKING;
    const auto get = reinterpret_cast<GetScheduler>(&sched_getscheduler);

    // SCHED_RESET_ON_FORK is or'ed into the policy PipeWire's threads get.
    const int policy = get(0) & ~SCHED_RESET_ON_FORK;
    return policy == SCHED_FIFO || policy == SCHED_RR;
}

} // namespace rb::rt
