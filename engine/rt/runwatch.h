// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "rt/realtime.h"

#include <cstdint>

namespace rb::rt
{

/// What the kernel says about the calling thread: the CPU time it has used,
/// and how often it has gone to sleep of its own accord.
struct ThreadRunReading {
    /// User and system time together.
    std::uint64_t cpuNanos = 0;
    /// Voluntary context switches: each is the thread having blocked, which
    /// for a data loop means it waited for the next cycle.
    std::int64_t sleeps = 0;
};

/// Reads the calling thread's figures: one `getrusage(RUSAGE_THREAD)`, a
/// system call that takes no lock and allocates nothing. False when the
/// kernel would not say, which leaves \a reading as it was.
[[nodiscard]] bool readThreadRun(ThreadRunReading &reading) noexcept RB_NONBLOCKING;

/// Whether the calling thread runs under a real-time policy (SCHED_FIFO or
/// SCHED_RR), which is what RLIMIT_RTTIME applies to. One system call.
[[nodiscard]] bool callingThreadIsRealtime() noexcept RB_NONBLOCKING;

/// **How long a thread has run without sleeping**, which is the
/// figure the kernel holds a real-time thread to.
///
/// rtkit makes PipeWire's data loop real-time and gives the process
/// RLIMIT_RTTIME, 200 ms with the soft limit equal to the hard one. The
/// kernel counts a real-time thread's CPU time from the moment it last woke
/// from a sleep, and at the limit sends SIGKILL, with no warning first. A
/// data loop sleeps between cycles; one whose every cycle takes longer than
/// its period never does, and is killed within a fraction of a second.
///
/// The watch is fed a reading at the end of each cycle. A reading whose
/// sleep count has moved says the thread slept since the last one, so the
/// run starts again **from the last reading**: the thread woke somewhere
/// after it, and counting from there overstates the run by at most what the
/// thread did between the reading and the sleep, which for a node read at
/// the end of its cycle is only other nodes' work, and the safe side to be
/// wrong on.
///
/// **A long run is not by itself a loop that never sleeps.** One cycle at a
/// large quantum can take more CPU than any threshold short of the kernel's
/// and still end in a sleep. What is fatal is the run going on across a
/// reading, which is what sleptSinceLastReading() says has not happened.
///
/// One watch belongs to one session node, not to the thread: several nodes
/// share a data loop, and a node that sees no sleep between two of its own
/// cycles has seen a whole cycle go by without the thread resting. The CPU
/// time is the thread's, so the run counts every node's work.
class RunWatch
{
public:
    /// Takes \a now and answers how long the thread has run since it last
    /// slept, in nanoseconds of CPU time. The first reading is a run of 0.
    [[nodiscard]] std::uint64_t update(const ThreadRunReading &now) noexcept RB_NONBLOCKING
    {
        m_slept = !m_seen || now.sleeps != m_sleeps;
        if (m_slept) {
            m_runStart = m_seen ? m_lastCpu : now.cpuNanos;
            m_sleeps = now.sleeps;
            m_seen = true;
        }
        m_lastCpu = now.cpuNanos;
        return now.cpuNanos >= m_runStart ? now.cpuNanos - m_runStart : 0;
    }

    /// Whether the thread slept between the last two readings (or there was
    /// only one): false is the loop having gone from one cycle into the
    /// next without resting.
    [[nodiscard]] bool sleptSinceLastReading() const noexcept RB_NONBLOCKING
    {
        return m_slept;
    }

private:
    std::uint64_t m_runStart = 0;
    std::uint64_t m_lastCpu = 0;
    std::int64_t m_sleeps = 0;
    bool m_seen = false;
    bool m_slept = true;
};

} // namespace rb::rt
