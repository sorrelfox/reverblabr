// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "rt/spscqueue.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>

using namespace rb::rt;

namespace
{

/// The shape of a parameter change: what the audio thread is really sent.
struct ParameterChange {
    std::uint32_t blockId = 0;
    std::uint32_t parameterId = 0;
    float value = 0.0f;
};

/// Enough operations to shake out an ordering mistake, and few enough that
/// the test still finishes under ThreadSanitizer.
constexpr std::uint64_t kStressOperations = 10'000'000;

} // namespace

TEST(SpscQueue, HoldsExactlyItsCapacity)
{
    SpscQueue<int> queue(3);
    EXPECT_EQ(queue.capacity(), 3u);
    EXPECT_TRUE(queue.isEmpty());

    EXPECT_TRUE(queue.tryPush(1));
    EXPECT_TRUE(queue.tryPush(2));
    EXPECT_TRUE(queue.tryPush(3));
    EXPECT_EQ(queue.size(), 3u);
    EXPECT_FALSE(queue.tryPush(4)) << "a full queue refuses rather than allocating";
    EXPECT_EQ(queue.size(), 3u);
}

TEST(SpscQueue, ComesOutInTheOrderItWentIn)
{
    SpscQueue<int> queue(8);
    for (int value = 0; value < 8; ++value) {
        ASSERT_TRUE(queue.tryPush(value));
    }
    for (int expected = 0; expected < 8; ++expected) {
        int value = -1;
        ASSERT_TRUE(queue.tryPop(value));
        EXPECT_EQ(value, expected);
    }
    int value = -1;
    EXPECT_FALSE(queue.tryPop(value));
    EXPECT_TRUE(queue.isEmpty());
}

TEST(SpscQueue, KeepsWorkingAsTheIndicesWrap)
{
    SpscQueue<int> queue(4);
    for (int round = 0; round < 1000; ++round) {
        ASSERT_TRUE(queue.tryPush(round));
        int value = -1;
        ASSERT_TRUE(queue.tryPop(value));
        EXPECT_EQ(value, round);
    }
    EXPECT_TRUE(queue.isEmpty());
}

TEST(SpscQueue, CarriesAWholeStructUnchanged)
{
    SpscQueue<ParameterChange> queue(4);
    ASSERT_TRUE(queue.tryPush(ParameterChange{7, 2, -6.5f}));
    ParameterChange change;
    ASSERT_TRUE(queue.tryPop(change));
    EXPECT_EQ(change.blockId, 7u);
    EXPECT_EQ(change.parameterId, 2u);
    EXPECT_FLOAT_EQ(change.value, -6.5f);
}

TEST(SpscQueue, AZeroCapacityQueueIsAlwaysFull)
{
    SpscQueue<int> queue(0);
    EXPECT_EQ(queue.capacity(), 0u);
    EXPECT_FALSE(queue.tryPush(1));
    int value = 0;
    EXPECT_FALSE(queue.tryPop(value));
}

TEST(SpscQueue, PassesTenMillionItemsBetweenTwoThreads)
{
    // Every item is numbered, so a value that arrives out of order, twice or
    // not at all is caught. A small queue makes both ends meet a full and an
    // empty queue many times over.
    SpscQueue<std::uint64_t> queue(64);
    std::atomic<bool> failed{false};

    std::thread consumer([&] {
        std::uint64_t expected = 0;
        while (expected < kStressOperations) {
            std::uint64_t value = 0;
            if (!queue.tryPop(value)) {
                continue;
            }
            if (value != expected) {
                failed.store(true, std::memory_order_relaxed);
                return;
            }
            ++expected;
        }
    });

    for (std::uint64_t value = 0; value < kStressOperations; ++value) {
        while (!queue.tryPush(value)) {
            if (failed.load(std::memory_order_relaxed)) {
                break;
            }
        }
    }
    consumer.join();

    EXPECT_FALSE(failed.load(std::memory_order_relaxed)) << "an item arrived out of order, twice or not at all";
    EXPECT_TRUE(queue.isEmpty());
}

TEST(SpscQueue, TheProducerNeverWaitsForTheConsumer)
{
    // A producer whose queue is full carries on with its cycle. This is the
    // audio thread's contract: it drops a change rather than blocking.
    SpscQueue<int> queue(2);
    ASSERT_TRUE(queue.tryPush(1));
    ASSERT_TRUE(queue.tryPush(2));

    int refused = 0;
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (!queue.tryPush(attempt)) {
            ++refused;
        }
    }
    EXPECT_EQ(refused, 100);
    EXPECT_EQ(queue.size(), 2u);
}
