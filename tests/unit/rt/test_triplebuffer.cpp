// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "rt/triplebuffer.h"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>

using namespace rb::rt;

namespace
{

/// A telemetry frame big enough that copying it on every read would be the
/// wrong thing to do, which is why it is here and not in a seqlock.
struct MeterFrame {
    std::uint64_t frame = 0;
    std::array<float, 64> bins{};

    void fill(std::uint64_t number) noexcept
    {
        frame = number;
        for (std::size_t bin = 0; bin < bins.size(); ++bin) {
            bins[bin] = static_cast<float>(number) + static_cast<float>(bin);
        }
    }

    [[nodiscard]] bool isConsistent() const noexcept
    {
        for (std::size_t bin = 0; bin < bins.size(); ++bin) {
            if (bins[bin] != static_cast<float>(frame) + static_cast<float>(bin)) {
                return false;
            }
        }
        return true;
    }
};

constexpr std::uint64_t kStressPublishes = 2'000'000;

} // namespace

TEST(TripleBuffer, ReadsTheValueThatWasPublished)
{
    TripleBuffer<int> buffer;
    buffer.writeSlot() = 5;
    buffer.publish();
    EXPECT_EQ(buffer.read(), 5);
}

TEST(TripleBuffer, ReadsTheLatestAndSkipsWhatItMissed)
{
    TripleBuffer<int> buffer;
    for (int value = 1; value <= 10; ++value) {
        buffer.writeSlot() = value;
        buffer.publish();
    }
    EXPECT_EQ(buffer.read(), 10) << "a reader that is behind gets the newest, not the next";
}

TEST(TripleBuffer, KeepsTheLastValueWhenNothingNewArrives)
{
    TripleBuffer<int> buffer;
    buffer.writeSlot() = 3;
    buffer.publish();
    EXPECT_EQ(buffer.read(), 3);
    EXPECT_FALSE(buffer.hasNewValue());
    EXPECT_EQ(buffer.read(), 3) << "reading again without a publish gives the same value";
}

TEST(TripleBuffer, SaysWhenSomethingNewIsWaiting)
{
    TripleBuffer<int> buffer;
    EXPECT_FALSE(buffer.hasNewValue());
    buffer.writeSlot() = 1;
    buffer.publish();
    EXPECT_TRUE(buffer.hasNewValue());
    static_cast<void>(buffer.read());
    EXPECT_FALSE(buffer.hasNewValue());
}

TEST(TripleBuffer, StartsAtTheValueItWasGiven)
{
    const int initial = 11;
    TripleBuffer<int> buffer(initial);
    EXPECT_EQ(buffer.read(), 11);
}

TEST(TripleBuffer, TheWriterAndTheReaderNeverHoldTheSameSlot)
{
    // If they ever did, the reader would see a half-filled frame. Each frame
    // is self-describing, so a frame that does not add up proves the slots
    // overlapped.
    TripleBuffer<MeterFrame> buffer;
    buffer.writeSlot().fill(0);
    buffer.publish();

    std::atomic<bool> writing{true};
    std::atomic<std::uint64_t> inconsistent{0};
    std::atomic<std::uint64_t> newestSeen{0};

    std::thread reader([&] {
        while (writing.load(std::memory_order_relaxed)) {
            const MeterFrame &frame = buffer.read();
            if (!frame.isConsistent()) {
                inconsistent.fetch_add(1, std::memory_order_relaxed);
            }
            newestSeen.store(frame.frame, std::memory_order_relaxed);
        }
    });

    for (std::uint64_t number = 1; number <= kStressPublishes; ++number) {
        buffer.writeSlot().fill(number);
        buffer.publish();
    }
    writing.store(false, std::memory_order_relaxed);
    reader.join();

    EXPECT_EQ(inconsistent.load(std::memory_order_relaxed), 0u);
    EXPECT_EQ(buffer.read().frame, kStressPublishes) << "the last publish has to be readable";
}

TEST(TripleBuffer, ThePublisherNeverWaits)
{
    // Publishing while nothing is reading must still cycle through the slots
    // rather than run out of them.
    TripleBuffer<MeterFrame> buffer;
    for (std::uint64_t number = 1; number <= 1000; ++number) {
        buffer.writeSlot().fill(number);
        buffer.publish();
    }
    const MeterFrame &frame = buffer.read();
    EXPECT_EQ(frame.frame, 1000u);
    EXPECT_TRUE(frame.isConsistent());
}
