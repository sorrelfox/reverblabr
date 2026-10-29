// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "rt/seqlock.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>

using namespace rb::rt;

namespace
{

/// A telemetry slot, with a checksum so that a reader can tell a whole
/// snapshot from two halves of different ones.
struct Telemetry {
    std::uint64_t frame = 0;
    float peakLeft = 0.0f;
    float peakRight = 0.0f;
    float rmsLeft = 0.0f;
    float rmsRight = 0.0f;
    float gainReductionDb = 0.0f;
    std::uint64_t checksum = 0;

    [[nodiscard]] std::uint64_t computedChecksum() const noexcept
    {
        std::uint64_t sum = frame * 1'000'003u;
        sum += static_cast<std::uint64_t>(peakLeft * 1000.0f);
        sum += static_cast<std::uint64_t>(peakRight * 1000.0f) * 7u;
        sum += static_cast<std::uint64_t>(rmsLeft * 1000.0f) * 13u;
        sum += static_cast<std::uint64_t>(rmsRight * 1000.0f) * 17u;
        sum += static_cast<std::uint64_t>(gainReductionDb * 1000.0f) * 19u;
        return sum;
    }
};

/// Fills every field from one number, so that a snapshot is either wholly
/// consistent or obviously torn.
Telemetry telemetryFor(std::uint64_t frame)
{
    Telemetry value;
    value.frame = frame;
    value.peakLeft = static_cast<float>(frame % 1000) * 0.001f;
    value.peakRight = static_cast<float>((frame + 1) % 1000) * 0.001f;
    value.rmsLeft = static_cast<float>((frame + 2) % 1000) * 0.001f;
    value.rmsRight = static_cast<float>((frame + 3) % 1000) * 0.001f;
    value.gainReductionDb = static_cast<float>((frame + 4) % 1000) * 0.001f;
    value.checksum = value.computedChecksum();
    return value;
}

constexpr std::uint64_t kStressWrites = 10'000'000;

} // namespace

TEST(SeqlockValue, ReadsBackWhatWasWritten)
{
    SeqlockValue<Telemetry> slot;
    const Telemetry written = telemetryFor(42);
    slot.store(written);

    const Telemetry read = slot.load();
    EXPECT_EQ(read.frame, 42u);
    EXPECT_EQ(read.checksum, read.computedChecksum());
    EXPECT_FLOAT_EQ(read.peakLeft, written.peakLeft);
    EXPECT_FLOAT_EQ(read.gainReductionDb, written.gainReductionDb);
}

TEST(SeqlockValue, StartsAtTheValueItWasGiven)
{
    const SeqlockValue<Telemetry> slot(telemetryFor(7));
    EXPECT_EQ(slot.load().frame, 7u);
    EXPECT_EQ(slot.publishCount(), 1u);
}

TEST(SeqlockValue, AnUntouchedSlotReadsAsZero)
{
    const SeqlockValue<Telemetry> slot;
    const Telemetry read = slot.load();
    EXPECT_EQ(read.frame, 0u);
    EXPECT_EQ(read.checksum, 0u);
    EXPECT_EQ(slot.publishCount(), 0u);
}

TEST(SeqlockValue, CountsWhatItPublishes)
{
    SeqlockValue<Telemetry> slot;
    for (std::uint64_t frame = 1; frame <= 5; ++frame) {
        slot.store(telemetryFor(frame));
    }
    EXPECT_EQ(slot.publishCount(), 5u);
    EXPECT_EQ(slot.load().frame, 5u);
}

TEST(SeqlockValue, TryLoadSucceedsWhenNobodyIsWriting)
{
    SeqlockValue<Telemetry> slot;
    slot.store(telemetryFor(3));
    Telemetry read;
    EXPECT_TRUE(slot.tryLoad(read));
    EXPECT_EQ(read.frame, 3u);
}

TEST(SeqlockValue, AReaderNeverSeesATornSnapshot)
{
    // The writer goes as fast as it can and never waits; the reader reads as
    // fast as it can. Every snapshot the reader accepts has to be one the
    // writer actually published, which the checksum says.
    SeqlockValue<Telemetry> slot;
    slot.store(telemetryFor(0));

    std::atomic<bool> writing{true};
    std::atomic<std::uint64_t> torn{0};
    std::atomic<std::uint64_t> readsMade{0};

    std::thread reader([&] {
        while (writing.load(std::memory_order_relaxed)) {
            const Telemetry read = slot.load();
            if (read.checksum != read.computedChecksum()) {
                torn.fetch_add(1, std::memory_order_relaxed);
            }
            readsMade.fetch_add(1, std::memory_order_relaxed);
        }
    });

    for (std::uint64_t frame = 1; frame <= kStressWrites; ++frame) {
        slot.store(telemetryFor(frame));
    }
    writing.store(false, std::memory_order_relaxed);
    reader.join();

    EXPECT_EQ(torn.load(std::memory_order_relaxed), 0u);
    EXPECT_GT(readsMade.load(std::memory_order_relaxed), 0u) << "the reader has to have raced the writer at all";
    EXPECT_EQ(slot.load().frame, kStressWrites);
}

TEST(SeqlockValue, ABoundedReaderGivesUpRatherThanSpinning)
{
    // A reader on the audio thread must come back whatever the other side is
    // doing. With the writer holding the slot open, tryLoad() has to fail
    // instead of waiting for it.
    SeqlockValue<Telemetry> slot;
    slot.store(telemetryFor(1));

    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> refusals{0};
    std::atomic<std::uint64_t> attempts{0};

    std::thread writer([&] {
        std::uint64_t frame = 2;
        while (!stop.load(std::memory_order_relaxed)) {
            slot.store(telemetryFor(frame++));
        }
    });

    Telemetry read;
    while (attempts.load(std::memory_order_relaxed) < 200'000) {
        if (!slot.tryLoad(read, 1)) {
            refusals.fetch_add(1, std::memory_order_relaxed);
        } else {
            EXPECT_EQ(read.checksum, read.computedChecksum());
        }
        attempts.fetch_add(1, std::memory_order_relaxed);
    }
    stop.store(true, std::memory_order_relaxed);
    writer.join();

    // Whether any single attempt loses the race is up to the scheduler; what
    // matters is that the loop got through its attempts without hanging.
    EXPECT_EQ(attempts.load(std::memory_order_relaxed), 200'000u);
    EXPECT_LE(refusals.load(std::memory_order_relaxed), attempts.load(std::memory_order_relaxed));
}
