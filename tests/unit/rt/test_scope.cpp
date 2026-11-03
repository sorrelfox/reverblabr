// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The path a block's sound takes to a surface that draws it: a writer on the
// audio thread, rings it goes round, and any number of readers it never hears
// from. What a reader gets has to be whole, in order and of one generation,
// whatever the writer is doing meanwhile.

#include "rt/meter.h"
#include "rt/scope.h"

#include <gtest/gtest.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <random>
#include <thread>
#include <vector>

using namespace rb::rt;

namespace
{

/// A pool as a memfd gives it: every byte nought.
struct ZeroedSlot {
    ZeroedSlot()
        : slot(static_cast<ScopeSlot *>(std::aligned_alloc(alignof(ScopeSlot), sizeof(ScopeSlot))))
    {
        std::memset(static_cast<void *>(slot), 0, sizeof(ScopeSlot));
    }
    ~ZeroedSlot()
    {
        std::free(slot);
    }
    ZeroedSlot(const ZeroedSlot &) = delete;
    ZeroedSlot &operator=(const ZeroedSlot &) = delete;

    ScopeSlot *slot;
};

[[nodiscard]] std::vector<ScopePoint> newest(const ScopeSlot &slot, std::size_t level, std::size_t count)
{
    std::vector<ScopePoint> points(count);
    points.resize(readScope(slot, level, points).points);
    return points;
}

} // namespace

TEST(Scope, TheFinestRingIsTheSamplesInOrderWithMidAndSide)
{
    ZeroedSlot memory;
    ScopeSlot &slot = *memory.slot;
    claimScopeSlot(slot);
    ScopeWriter writer;
    const std::array<float, 4> left{0.5f, -0.5f, 1.0f, 0.0f};
    const std::array<float, 4> right{0.25f, 0.5f, -1.0f, 0.0f};
    writer.write(slot, left.data(), right.data(), 4, 48000);

    const std::vector<ScopePoint> points = newest(slot, 0, 10);
    ASSERT_EQ(points.size(), 4U);
    for (std::size_t index = 0; index < points.size(); ++index) {
        EXPECT_EQ(points[index].low[ScopeLeft], left[index]);
        EXPECT_EQ(points[index].high[ScopeLeft], left[index]);
        EXPECT_EQ(points[index].low[ScopeRight], right[index]);
        EXPECT_EQ(points[index].low[ScopeMid], 0.5f * (left[index] + right[index]));
        EXPECT_EQ(points[index].low[ScopeSide], 0.5f * (left[index] - right[index]));
    }
    std::vector<ScopePoint> room(1);
    EXPECT_EQ(readScope(slot, 0, room).sampleRate, 48000U);
}

TEST(Scope, ACoarserPointHoldsTheExtremesOfItsSamplesHoweverTheCyclesFell)
{
    std::mt19937 random(7);
    std::uniform_real_distribution<float> amplitude(-1.0f, 1.0f);
    std::vector<float> left(4096);
    std::vector<float> right(4096);
    for (std::size_t frame = 0; frame < left.size(); ++frame) {
        left[frame] = amplitude(random);
        right[frame] = amplitude(random);
    }
    std::uniform_int_distribution<std::uint32_t> cycle(1, 300);

    ZeroedSlot memory;
    ScopeSlot &slot = *memory.slot;
    claimScopeSlot(slot);
    ScopeWriter writer;
    for (std::uint32_t frame = 0; frame < left.size();) {
        const std::uint32_t frames = std::min<std::uint32_t>(cycle(random), left.size() - frame);
        writer.write(slot, &left[frame], &right[frame], frames, 48000);
        frame += frames;
    }

    for (std::size_t level = 1; level < kScopeLevels; ++level) {
        const std::uint32_t perPoint = scopeFramesPerPoint(level);
        const std::vector<ScopePoint> points = newest(slot, level, kScopePoints);
        ASSERT_EQ(points.size(), left.size() / perPoint) << level;
        for (std::size_t point = 0; point < points.size(); ++point) {
            float low = 2.0f;
            float high = -2.0f;
            float midLow = 2.0f;
            float midHigh = -2.0f;
            for (std::size_t frame = point * perPoint; frame < (point + 1) * perPoint; ++frame) {
                low = std::min(low, left[frame]);
                high = std::max(high, left[frame]);
                const float mid = 0.5f * (left[frame] + right[frame]);
                midLow = std::min(midLow, mid);
                midHigh = std::max(midHigh, mid);
            }
            EXPECT_EQ(points[point].low[ScopeLeft], low);
            EXPECT_EQ(points[point].high[ScopeLeft], high);
            EXPECT_EQ(points[point].low[ScopeMid], midLow) << "a mid's extremes are its own, not the sides'";
            EXPECT_EQ(points[point].high[ScopeMid], midHigh);
        }
    }
}

TEST(Scope, ASidesInOppositePhaseHaveNoMidAtAnyResolution)
{
    std::vector<float> left(1024);
    std::vector<float> right(1024);
    for (std::size_t frame = 0; frame < left.size(); ++frame) {
        left[frame] = std::sin(static_cast<float>(frame) * 0.1f);
        right[frame] = -left[frame];
    }
    ZeroedSlot memory;
    ScopeWriter writer;
    claimScopeSlot(*memory.slot);
    writer.write(*memory.slot, left.data(), right.data(), 1024, 48000);
    for (const ScopePoint &point : newest(*memory.slot, 2, 16)) {
        EXPECT_EQ(point.low[ScopeMid], 0.0f);
        EXPECT_EQ(point.high[ScopeMid], 0.0f);
        EXPECT_GT(point.high[ScopeSide], 0.5f);
    }
}

TEST(Scope, OneSampleStaysInTheCoarsestRing)
{
    std::vector<float> left(4096, 0.0f);
    left[3001] = 0.9f;
    ZeroedSlot memory;
    ScopeWriter writer;
    claimScopeSlot(*memory.slot);
    writer.write(*memory.slot, left.data(), nullptr, 4096, 48000);
    const std::vector<ScopePoint> points = newest(*memory.slot, 3, 16);
    ASSERT_EQ(points.size(), 8U);
    EXPECT_EQ(points[5].high[ScopeLeft], 0.9f) << "a click was averaged away";
    EXPECT_EQ(points[5].low[ScopeLeft], 0.0f);
    EXPECT_EQ(points[4].high[ScopeLeft], 0.0f);
}

TEST(Scope, ANanStaysOutOfACoarserPoint)
{
    std::array<float, 8> left{0.25f, -0.25f, NAN, 0.5f, 0.0f, 0.0f, 0.0f, NAN};
    ZeroedSlot memory;
    ScopeWriter writer;
    claimScopeSlot(*memory.slot);
    writer.write(*memory.slot, left.data(), nullptr, 8, 48000);
    const std::vector<ScopePoint> points = newest(*memory.slot, 1, 4);
    ASSERT_EQ(points.size(), 1U);
    EXPECT_EQ(points[0].low[ScopeLeft], -0.25f);
    EXPECT_EQ(points[0].high[ScopeLeft], 0.5f);
}

TEST(Scope, AReaderGetsWhatTheRingStillHoldsAndNoMore)
{
    ZeroedSlot memory;
    ScopeSlot &slot = *memory.slot;
    claimScopeSlot(slot);
    EXPECT_TRUE(newest(slot, 0, 100).empty()) << "nothing written is nothing read";

    // Three times round.
    std::vector<float> left(kScopePoints * 3);
    for (std::size_t frame = 0; frame < left.size(); ++frame) {
        left[frame] = static_cast<float>(frame);
    }
    ScopeWriter writer;
    writer.write(slot, left.data(), nullptr, static_cast<std::uint32_t>(left.size()), 48000);

    const std::vector<ScopePoint> all = newest(slot, 0, kScopePoints * 2);
    ASSERT_EQ(all.size(), kScopePoints - 1) << "the point the writer may be in is never given";
    EXPECT_EQ(all.back().low[ScopeLeft], left.back());
    EXPECT_EQ(all.front().low[ScopeLeft], left[left.size() - (kScopePoints - 1)]);

    const std::vector<ScopePoint> few = newest(slot, 0, 3);
    ASSERT_EQ(few.size(), 3U);
    EXPECT_EQ(few[2].low[ScopeLeft], left.back());
    EXPECT_EQ(few[0].low[ScopeLeft], left[left.size() - 3]);
}

TEST(Scope, ACoarserRingIsThereFromTheStartAtAFewSamples)
{
    ZeroedSlot memory;
    ScopeWriter writer;
    claimScopeSlot(*memory.slot);
    const std::array<float, 7> left{};
    writer.write(*memory.slot, left.data(), nullptr, 7, 48000);
    EXPECT_TRUE(newest(*memory.slot, 1, 4).empty()) << "a point of eight, with seven heard";
}

TEST(Scope, ASlotGivenOutAgainStartsClean)
{
    ZeroedSlot memory;
    ScopeSlot &slot = *memory.slot;
    claimScopeSlot(slot);
    ScopeWriter writer;
    std::vector<float> left(100, 0.5f);
    writer.write(slot, left.data(), nullptr, 100, 48000);

    std::vector<ScopePoint> room(16);
    const std::uint32_t before = readScope(slot, 0, room).generation;
    claimScopeSlot(slot);
    const ScopeRead fresh = readScope(slot, 0, room);
    EXPECT_NE(fresh.generation, before);
    EXPECT_EQ(fresh.points, 0U) << "another block's sound was still there";

    // The same writer keeps no half-filled point from the last time.
    writer.write(slot, left.data(), nullptr, 4, 48000);
    EXPECT_EQ(newest(slot, 0, 16).size(), 4U);
    EXPECT_TRUE(newest(slot, 1, 16).empty());
}

TEST(Scope, GivingOutASlotTouchesEveryPageWhereverTheSlotStarts)
{
    // A slot starts wherever the snapshot before it ended, so a ring's last
    // points can lie on a page of their own, and the audio thread reaches
    // them only after a quarter of an hour.
    const auto page = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
    const std::size_t pages = sizeof(ScopeSlot) / page + 3;
    for (const std::size_t offset : {std::size_t{0}, std::size_t{64}, std::size_t{320}, page / 2, page - 64}) {
        void *memory = ::mmap(nullptr, pages * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ASSERT_NE(memory, MAP_FAILED);
        auto *slot = reinterpret_cast<ScopeSlot *>(static_cast<char *>(memory) + offset);
        claimScopeSlot(*slot);

        std::vector<unsigned char> resident(pages);
        ASSERT_EQ(::mincore(memory, pages * page, resident.data()), 0);
        const std::size_t lastPage = (offset + sizeof(ScopeSlot) - 1) / page;
        for (std::size_t index = 0; index <= lastPage; ++index) {
            EXPECT_NE(resident[index] & 1U, 0U) << "page " << index << " at offset " << offset;
        }
        ::munmap(memory, pages * page);
    }
}

TEST(Scope, ABlockMadeAnewGoesOnFromWhereTheRingsHaveGot)
{
    // An edit can make a block again around a meter that still has its slot.
    ZeroedSlot memory;
    ScopeSlot &slot = *memory.slot;
    claimScopeSlot(slot);
    std::vector<float> left(1000, 0.5f);
    {
        ScopeWriter writer;
        writer.write(slot, left.data(), nullptr, 1000, 48000);
    }
    ScopeWriter next;
    next.write(slot, left.data(), nullptr, 1000, 48000);
    EXPECT_EQ(newest(slot, 0, kScopePoints).size(), 2000U) << "the head went backwards under a reader";
}

TEST(Scope, ACountThatWrapsLosesNothing)
{
    ZeroedSlot memory;
    ScopeSlot &slot = *memory.slot;
    claimScopeSlot(slot);
    // Six hours at 192 kHz, more or less.
    const std::uint32_t nearEnd = 0xffffffffU - 50U;
    slot.rings[0].head = nearEnd;
    ScopeWriter writer;
    std::vector<float> left(100);
    for (std::size_t frame = 0; frame < left.size(); ++frame) {
        left[frame] = static_cast<float>(frame);
    }
    writer.write(slot, left.data(), nullptr, 100, 48000);
    const std::vector<ScopePoint> points = newest(slot, 0, 20);
    ASSERT_FALSE(points.empty());
    EXPECT_EQ(points.back().low[ScopeLeft], 99.0f);
}

TEST(Scope, AWriterGoingFlatOutNeverHandsAReaderATornOrOutOfOrderPoint)
{
    // Two sides that say the same number, so that half of one point and half
    // of another shows, and a count, so that a point out of place shows. The
    // writer does nothing else, so the ring is overwritten under every copy.
    ZeroedSlot memory;
    ScopeSlot &slot = *memory.slot;
    claimScopeSlot(slot);

    constexpr std::uint32_t kCycle = 512;
    constexpr std::uint32_t kWrap = 1U << 20; // A float counts to here exactly.
    std::atomic<bool> stop{false};
    std::thread writing([&] {
        ScopeWriter writer;
        std::array<float, kCycle> left{};
        std::array<float, kCycle> right{};
        std::uint32_t count = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            for (std::uint32_t frame = 0; frame < kCycle; ++frame) {
                count = (count + 1) % kWrap;
                left[frame] = static_cast<float>(count);
                right[frame] = -static_cast<float>(count);
            }
            writer.write(slot, left.data(), right.data(), kCycle, 48000);
        }
    });

    constexpr int kReaders = 2;
    std::atomic<std::uint64_t> pointsRead{0};
    std::atomic<std::uint64_t> shortened{0};
    std::atomic<std::uint64_t> torn{0};
    std::atomic<std::uint64_t> outOfOrder{0};
    std::array<std::thread, kReaders> reading;
    for (std::thread &reader : reading) {
        reader = std::thread([&] {
            std::vector<ScopePoint> room(kScopePoints);
            for (int round = 0; round < 4000; ++round) {
                const ScopeRead read = readScope(slot, 0, room);
                pointsRead.fetch_add(read.points, std::memory_order_relaxed);
                if (read.points < kScopePoints - 1) {
                    shortened.fetch_add(1, std::memory_order_relaxed);
                }
                for (std::size_t index = 0; index < read.points; ++index) {
                    const ScopePoint &point = room[index];
                    if (point.low[ScopeLeft] != -point.low[ScopeRight] || point.low[ScopeMid] != 0.0f
                        || point.high[ScopeLeft] != point.low[ScopeLeft]
                        || point.low[ScopeSide] != point.low[ScopeLeft]) {
                        torn.fetch_add(1, std::memory_order_relaxed);
                    }
                    if (index > 0) {
                        const auto step = static_cast<std::uint32_t>(point.low[ScopeLeft])
                            - static_cast<std::uint32_t>(room[index - 1].low[ScopeLeft]);
                        if ((step & (kWrap - 1U)) != 1U) {
                            outOfOrder.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                }
            }
        });
    }
    for (std::thread &reader : reading) {
        reader.join();
    }
    stop.store(true, std::memory_order_relaxed);
    writing.join();

    EXPECT_EQ(torn.load(), 0U);
    EXPECT_EQ(outOfOrder.load(), 0U);
    EXPECT_GT(pointsRead.load(), 0U) << "the readers have to have read something";
}

TEST(Scope, AMeterWritesOnlyWhileItHasASlot)
{
    ZeroedSlot memory;
    ScopeSlot &slot = *memory.slot;
    claimScopeSlot(slot);
    BlockMeter meter;
    ScopeWriter writer;
    const std::array<float, 8> left{};

    meter.feedScope(writer, left.data(), nullptr, 8, 48000);
    EXPECT_TRUE(newest(slot, 0, 16).empty());

    meter.setScope(&slot);
    meter.feedScope(writer, left.data(), nullptr, 8, 48000);
    EXPECT_EQ(newest(slot, 0, 16).size(), 8U);

    meter.setScope(nullptr);
    meter.feedScope(writer, left.data(), nullptr, 8, 48000);
    EXPECT_EQ(newest(slot, 0, 16).size(), 8U);
}

TEST(Scope, WhenASlotIsTakenAwayNothingMoreIsWrittenToIt)
{
    // setScope(null) is what lets a slot go to another block. A write that
    // came in after it returned would land in somebody else's sound.
    ZeroedSlot memory;
    ScopeSlot &slot = *memory.slot;
    claimScopeSlot(slot);
    BlockMeter meter;
    std::atomic<bool> stop{false};
    std::thread audio([&] {
        ScopeWriter writer;
        const std::array<float, 64> left{};
        while (!stop.load(std::memory_order_relaxed)) {
            meter.feedScope(writer, left.data(), nullptr, 64, 48000);
        }
    });

    std::vector<ScopePoint> room(1);
    for (int round = 0; round < 2000; ++round) {
        meter.setScope(&slot);
        std::this_thread::yield();
        meter.setScope(nullptr);
        const std::uint32_t head = readScope(slot, 0, room).head;
        std::this_thread::yield();
        EXPECT_EQ(readScope(slot, 0, room).head, head) << "written to after it was taken away, round " << round;
    }
    stop.store(true, std::memory_order_relaxed);
    audio.join();
}
