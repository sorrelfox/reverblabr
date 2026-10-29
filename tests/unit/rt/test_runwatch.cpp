// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "rt/runwatch.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <thread>

using namespace rb::rt;
using namespace std::chrono_literals;

namespace
{

constexpr std::uint64_t kMs = 1'000'000;

[[nodiscard]] ThreadRunReading reading(std::uint64_t cpuMs, std::int64_t sleeps)
{
    return ThreadRunReading{cpuMs * kMs, sleeps};
}

/// Uses \a duration of CPU on the calling thread without sleeping.
void spin(std::chrono::milliseconds duration)
{
    ThreadRunReading start;
    ASSERT_TRUE(readThreadRun(start));
    ThreadRunReading now = start;
    while (now.cpuNanos - start.cpuNanos < static_cast<std::uint64_t>(duration.count()) * kMs) {
        ASSERT_TRUE(readThreadRun(now));
    }
}

} // namespace

TEST(RunWatch, TheFirstReadingIsARunOfNothing)
{
    RunWatch watch;
    EXPECT_EQ(watch.update(reading(500, 7)), 0U);
}

TEST(RunWatch, AThreadThatNeverSleepsRunsOnFromTheFirstReading)
{
    RunWatch watch;
    (void)watch.update(reading(100, 3));
    EXPECT_TRUE(watch.sleptSinceLastReading()) << "one reading is no evidence of a run";
    EXPECT_EQ(watch.update(reading(105, 3)), 5 * kMs);
    EXPECT_FALSE(watch.sleptSinceLastReading());
    EXPECT_EQ(watch.update(reading(160, 3)), 60 * kMs);
}

// The thread woke somewhere between the two readings, so the run is counted
// from the earlier one: at most what it did before it slept is too much.
TEST(RunWatch, ASleepStartsTheRunAgainFromTheReadingBeforeIt)
{
    RunWatch watch;
    (void)watch.update(reading(100, 3));
    (void)watch.update(reading(150, 3));
    EXPECT_EQ(watch.update(reading(153, 4)), 3 * kMs);
    EXPECT_TRUE(watch.sleptSinceLastReading());
    EXPECT_EQ(watch.update(reading(160, 4)), 10 * kMs);
    EXPECT_FALSE(watch.sleptSinceLastReading());
    EXPECT_EQ(watch.update(reading(162, 9)), 2 * kMs);
}

// What the kernel says, rather than what a test says it says: CPU used
// without a sleep is a run, and a sleep ends it.
TEST(RunWatch, TheKernelsFiguresGiveTheRunSinceTheThreadLastSlept)
{
    RunWatch watch;
    ThreadRunReading now;
    ASSERT_TRUE(readThreadRun(now));
    (void)watch.update(now);

    spin(30ms);
    ASSERT_TRUE(readThreadRun(now));
    const std::uint64_t ran = watch.update(now);
    EXPECT_GE(ran, 25 * kMs) << "30 ms of CPU without a sleep";

    std::this_thread::sleep_for(5ms);
    spin(2ms);
    ASSERT_TRUE(readThreadRun(now));
    EXPECT_LT(watch.update(now), 15 * kMs) << "the sleep did not start the run again";
}

TEST(RunWatch, AnOrdinaryThreadIsNotRealtime)
{
    EXPECT_FALSE(callingThreadIsRealtime());
}
