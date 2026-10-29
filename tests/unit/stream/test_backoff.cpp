// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// How long a stream waits before trying a server again.

#include "stream/backoff.h"

#include <gtest/gtest.h>

using namespace rb::stream;
using namespace std::chrono_literals;

namespace
{

[[nodiscard]] Backoff fixed(double random)
{
    Backoff::Options options;
    options.random = [random] { return random; };
    return Backoff(options);
}

} // namespace

TEST(BackoffTest, EachWaitIsTwiceTheLastUpToAMinute)
{
    Backoff backoff = fixed(0.0);
    EXPECT_EQ(backoff.next(), 1000ms);
    EXPECT_EQ(backoff.next(), 2000ms);
    EXPECT_EQ(backoff.next(), 4000ms);
    EXPECT_EQ(backoff.next(), 8000ms);
    EXPECT_EQ(backoff.next(), 16000ms);
    EXPECT_EQ(backoff.next(), 32000ms);
    EXPECT_EQ(backoff.next(), 60000ms);
    EXPECT_EQ(backoff.next(), 60000ms);
    EXPECT_EQ(backoff.failures(), 8U);
    // However long it goes on, the doubling never overflows.
    for (int index = 0; index < 100; ++index) {
        EXPECT_EQ(backoff.next(), 60000ms);
    }
}

TEST(BackoffTest, TheRandomPartShortensAWaitByAFifthAtMost)
{
    Backoff most = fixed(1.0);
    EXPECT_EQ(most.next(), 800ms);
    EXPECT_EQ(most.next(), 1600ms);

    Backoff own;
    for (int index = 0; index < 50; ++index) {
        const std::chrono::milliseconds wait = own.next();
        const std::chrono::milliseconds full =
            std::min<std::chrono::milliseconds>(1000ms * (1LL << std::min(index, 20)), 60000ms);
        EXPECT_LE(wait, full);
        EXPECT_GE(wait, full * 8 / 10);
    }
}

TEST(BackoffTest, OnlyAConnectionThatLastedStartsTheCountAgain)
{
    Backoff backoff = fixed(0.0);
    (void)backoff.next();
    (void)backoff.next();
    (void)backoff.next();
    // Taken and dropped at once: the server is not well, and the next wait
    // is longer still.
    backoff.connectionLasted(500ms);
    EXPECT_EQ(backoff.next(), 8000ms);
    backoff.connectionLasted(30s);
    EXPECT_EQ(backoff.next(), 1000ms);
    backoff.reset();
    EXPECT_EQ(backoff.failures(), 0U);
}
