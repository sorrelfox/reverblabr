// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Waveform: the sound passes bit for bit at every cycle length, whatever is
// drawn; the rings hold what went by; the settings are kept and touch nothing
// on the audio thread; and with nobody looking it costs next to nothing.

#include "allocationtrap.h"
#include "blocks/waveformmeter/waveformmeter.h"
#include "blocksizeinvariance.h"
#include "dsp/block.h"
#include "dsp/paramvalue.h"
#include "dsp/scopetrace.h"
#include "offlinerunner.h"
#include "rt/meter.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <numbers>
#include <vector>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr std::size_t kSecond = 48000;

/// A slot as a memfd gives it, and a meter that has it.
struct Drawn {
    Drawn()
        : slot(static_cast<rt::ScopeSlot *>(std::aligned_alloc(alignof(rt::ScopeSlot), sizeof(rt::ScopeSlot))))
    {
        std::memset(static_cast<void *>(slot), 0, sizeof(rt::ScopeSlot));
        rt::claimScopeSlot(*slot);
        meter.setScope(slot);
    }
    ~Drawn()
    {
        std::free(slot);
    }
    Drawn(const Drawn &) = delete;
    Drawn &operator=(const Drawn &) = delete;

    [[nodiscard]] std::vector<rt::ScopePoint> points(std::size_t level) const
    {
        std::vector<rt::ScopePoint> room(rt::kScopePoints);
        room.resize(rt::readScope(*slot, level, room).points);
        return room;
    }

    rt::ScopeSlot *slot;
    rt::BlockMeter meter;
};

[[nodiscard]] Signal tone(std::size_t length, double hz, double amplitude = 0.5)
{
    Signal signal(length);
    for (std::size_t n = 0; n < length; ++n) {
        signal[n] = static_cast<float>(
            amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(n) / static_cast<double>(kSecond)));
    }
    return signal;
}

} // namespace

TEST(WaveformMeter, PassesTheSoundBitForBitWithNoDelay)
{
    WaveformMeter block;
    EXPECT_EQ(block.info().latencySamples, 0U);
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal left = whiteNoise(kSecond, 0.7f, 901);
    const Signal right = whiteNoise(kSecond, 0.3f, 902);
    const MultiSignal out = runner.run(MultiSignal{left, right}, CycleShape::random(1, 2048, 903));
    EXPECT_EQ(out[0], left);
    EXPECT_EQ(out[1], right);
}

TEST(WaveformMeter, BeingDrawnChangesNothingAboutTheSound)
{
    Drawn drawn;
    WaveformMeter block;
    BlockRunner runner(block, 2, 2);
    runner.setMeter(&drawn.meter);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal left = whiteNoise(kSecond, 0.7f, 904);
    const Signal right = whiteNoise(kSecond, 0.3f, 905);
    const MultiSignal out = runner.run(MultiSignal{left, right}, CycleShape::fixed(256));
    EXPECT_EQ(out[0], left);
    EXPECT_EQ(out[1], right);
    EXPECT_FALSE(drawn.points(0).empty()) << "it was supposed to be drawn";
}

TEST(WaveformMeter, TheSoundIsTheSameAtEveryCycleLength)
{
    const MultiSignal input{whiteNoise(kSecond / 2, 0.5f, 906), whiteNoise(kSecond / 2, 0.5f, 912)};
    const InvarianceReport report = checkBlockSizeInvariance([] { return std::make_unique<WaveformMeter>(); }, input);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(WaveformMeter, WhatIsDrawnIsTheSameAtEveryCycleLength)
{
    const Signal left = whiteNoise(kSecond / 4, 0.5f, 907);
    const Signal right = whiteNoise(kSecond / 4, 0.5f, 908);
    const auto run = [&](const CycleShape &shape) {
        auto drawn = std::make_unique<Drawn>();
        WaveformMeter block;
        BlockRunner runner(block, 2, 2);
        runner.setMeter(&drawn->meter);
        EXPECT_TRUE(runner.prepare().has_value());
        (void)runner.run(MultiSignal{left, right}, shape);
        return drawn;
    };
    const auto steady = run(CycleShape::fixed(1024));
    const auto ragged = run(CycleShape::random(1, 2047, 909));
    for (std::size_t level = 0; level < rt::kScopeLevels; ++level) {
        const std::vector<rt::ScopePoint> one = steady->points(level);
        const std::vector<rt::ScopePoint> two = ragged->points(level);
        ASSERT_EQ(one.size(), two.size()) << level;
        for (std::size_t point = 0; point < one.size(); ++point) {
            ASSERT_EQ(std::memcmp(&one[point], &two[point], sizeof(rt::ScopePoint)), 0) << level << ":" << point;
        }
    }
}

TEST(WaveformMeter, EveryRingShowsTheSoundItsExtremesIncluded)
{
    Drawn drawn;
    WaveformMeter block;
    BlockRunner runner(block, 2, 2);
    runner.setMeter(&drawn.meter);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal left = tone(kSecond, 100.0, 0.8);
    const Signal right = tone(kSecond, 100.0, 0.4);
    (void)runner.run(MultiSignal{left, right}, CycleShape::fixed(480));

    for (std::size_t level = 0; level < rt::kScopeLevels; ++level) {
        float highest = -2.0f;
        float lowest = 2.0f;
        float highestRight = -2.0f;
        for (const rt::ScopePoint &point : drawn.points(level)) {
            highest = std::max(highest, point.high[rt::ScopeLeft]);
            lowest = std::min(lowest, point.low[rt::ScopeLeft]);
            highestRight = std::max(highestRight, point.high[rt::ScopeRight]);
        }
        EXPECT_NEAR(highest, 0.8f, 0.001f) << level;
        EXPECT_NEAR(lowest, -0.8f, 0.001f) << level;
        EXPECT_NEAR(highestRight, 0.4f, 0.001f) << level;
    }
}

TEST(WaveformMeter, ATraceStartedOnACrossingStandsStillOnATone)
{
    // Wherever the newest sample falls, the place chosen is the same point of
    // the wave: that is the whole of a trigger.
    const Signal signal = tone(kSecond, 441.0);
    for (std::size_t cut : {4000U, 4100U, 4133U, 4250U, 7777U}) {
        Drawn drawn;
        WaveformMeter block;
        BlockRunner runner(block, 2, 2);
        runner.setMeter(&drawn.meter);
        ASSERT_TRUE(runner.prepare().has_value());
        (void)runner.run(MultiSignal{Signal(signal.begin(), signal.begin() + static_cast<std::ptrdiff_t>(cut))},
                         CycleShape::fixed(256));

        const std::vector<rt::ScopePoint> points = drawn.points(0);
        const ScopeCrossing crossing = latestRisingCrossing(points, rt::ScopeLeft, points.size() - 1);
        ASSERT_TRUE(crossing.found) << cut;
        // Where the crossing is, in samples since the start of the tone.
        const double at = static_cast<double>(cut - points.size()) + crossing.point;
        const double period = static_cast<double>(kSecond) / 441.0;
        const double cycles = at / period;
        EXPECT_NEAR(cycles, std::round(cycles), 0.002) << cut;
    }
}

TEST(WaveformMeter, ItsSettingsAreKeptAndAPresetLeavesTheHoldAlone)
{
    WaveformMeter block;
    EXPECT_TRUE(block.setParam("style", ParamValue::ofEnum("bars")).has_value());
    EXPECT_TRUE(block.setParam("spanMs", ParamValue::ofFloat(1000.0f)).has_value());
    EXPECT_TRUE(block.setParam("hold", ParamValue::ofBool(true)).has_value());
    EXPECT_FALSE(block.setParam("style", ParamValue::ofEnum("smoke")).has_value());
    EXPECT_FALSE(block.setParam("spanMs", ParamValue::ofFloat(1.0f)).has_value());
    EXPECT_FALSE(block.setParam("refresh", ParamValue::ofEnum("hz61")).has_value());

    const ParamValueMap saved = block.saveState();
    WaveformMeter next;
    ASSERT_TRUE(next.loadState(saved).has_value());
    EXPECT_EQ(next.param("style"), ParamValue::ofEnum("bars"));
    EXPECT_EQ(next.param("spanMs"), ParamValue::ofFloat(1000.0f));
    EXPECT_EQ(next.param("hold"), ParamValue::ofBool(true));

    for (const ParamDescriptor &descriptor : block.params()) {
        EXPECT_EQ(descriptor.flags.inPresets, descriptor.id != "hold") << descriptor.id;
        EXPECT_TRUE(descriptor.flags.automatable) << descriptor.id << " would plan the session again";
    }
}

TEST(WaveformMeter, AllocatesNothingBeingDrawnOrNot)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "no allocation trap in this build";
    }
    const Signal noise = whiteNoise(kSecond, 0.5f, 910);
    {
        Drawn drawn;
        WaveformMeter block;
        BlockRunner runner(block, 2, 2);
        runner.setMeter(&drawn.meter);
        ASSERT_TRUE(runner.prepare().has_value());
        std::size_t allocations = 0;
        (void)runner.runWatchingForAllocations(MultiSignal{noise, noise}, CycleShape::fixed(256), allocations);
        EXPECT_EQ(allocations, 0U);
    }
    {
        rt::BlockMeter meter;
        WaveformMeter block;
        BlockRunner runner(block, 2, 2);
        runner.setMeter(&meter);
        ASSERT_TRUE(runner.prepare().has_value());
        std::size_t allocations = 0;
        (void)runner.runWatchingForAllocations(MultiSignal{noise, noise}, CycleShape::fixed(256), allocations);
        EXPECT_EQ(allocations, 0U);
    }
}

TEST(WaveformMeter, CostsAlmostNothingUnwatchedAndLittleWatched)
{
    const Signal noise = whiteNoise(kSecond * 60, 0.1f, 911);
    const auto shareOfACore = [&](rt::BlockMeter &meter) {
        WaveformMeter block;
        BlockRunner runner(block, 2, 2);
        runner.setMeter(&meter);
        EXPECT_TRUE(runner.prepare().has_value());
        const auto started = std::chrono::steady_clock::now();
        (void)runner.run(MultiSignal{noise, noise}, CycleShape::fixed(256));
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() / 60.0;
    };
    Drawn drawn;
    rt::BlockMeter nobody;
    const double unwatched = shareOfACore(nobody);
    const double watched = shareOfACore(drawn.meter);
    std::cerr << "waveform: a minute of stereo at " << unwatched * 100.0 << " % of a core unwatched, "
              << watched * 100.0 << " % watched\n";
#ifdef NDEBUG
    EXPECT_LT(unwatched, 0.002);
    EXPECT_LT(watched, 0.005);
#endif
}

TEST(ScopeTrace, TheFinestRingThatReachesFarEnoughIsChosen)
{
    EXPECT_EQ(scopeLevelFor(480.0), 0U);
    EXPECT_EQ(scopeLevelFor(rt::kScopePoints - 1.0), 0U);
    EXPECT_EQ(scopeLevelFor(rt::kScopePoints), 1U);
    EXPECT_EQ(scopeLevelFor(48000.0), 1U);
    EXPECT_EQ(scopeLevelFor(480000.0), 2U);
    EXPECT_EQ(scopeLevelFor(1920000.0), 3U);
    EXPECT_EQ(scopeLevelFor(1.0e9), rt::kScopeLevels - 1);
}

TEST(ScopeTrace, EvenTenSecondsAt192kHzFitsTheCoarsestRing)
{
    EXPECT_GE((rt::kScopePoints - 1.0) * rt::scopeFramesPerPoint(rt::kScopeLevels - 1), 2.0 * 192000.0 * 10.0)
        << "with room for a trigger's look back";
}

namespace
{

[[nodiscard]] std::vector<rt::ScopePoint> pointsOf(const std::vector<float> &values)
{
    std::vector<rt::ScopePoint> points(values.size());
    for (std::size_t index = 0; index < values.size(); ++index) {
        points[index].low[rt::ScopeLeft] = values[index];
        points[index].high[rt::ScopeLeft] = values[index];
    }
    return points;
}

} // namespace

TEST(ScopeTrace, ACrossingIsPlacedBetweenThePointsEitherSide)
{
    const auto points = pointsOf({-1.0f, -1.0f, 1.0f, 1.0f});
    const ScopeCrossing crossing = latestRisingCrossing(points, rt::ScopeLeft, 3);
    ASSERT_TRUE(crossing.found);
    EXPECT_DOUBLE_EQ(crossing.point, 1.5);
}

TEST(ScopeTrace, TheLatestCrossingNoLaterThanAskedIsTheOneFound)
{
    const auto points = pointsOf({-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f});
    EXPECT_NEAR(latestRisingCrossing(points, rt::ScopeLeft, 5).point, 4.5, 1.0e-9);
    EXPECT_NEAR(latestRisingCrossing(points, rt::ScopeLeft, 4).point, 2.5, 1.0e-9);
}

TEST(ScopeTrace, RippleNearNoughtIsNotACrossing)
{
    // A slow wave going up through nought with a small ripple on it: only
    // the first time counts, or the trace would jump between them.
    const auto points = pointsOf({-1.0f, -0.5f, -0.01f, 0.01f, -0.01f, 0.01f, 0.5f, 1.0f});
    const ScopeCrossing crossing = latestRisingCrossing(points, rt::ScopeLeft, 7);
    ASSERT_TRUE(crossing.found);
    EXPECT_NEAR(crossing.point, 2.5, 1.0e-9);
}

TEST(ScopeTrace, SilenceAndAConstantHaveNoCrossing)
{
    EXPECT_FALSE(latestRisingCrossing(pointsOf(std::vector<float>(64, 0.0f)), rt::ScopeLeft, 63).found);
    EXPECT_FALSE(latestRisingCrossing(pointsOf(std::vector<float>(64, 0.5f)), rt::ScopeLeft, 63).found);
    EXPECT_FALSE(latestRisingCrossing(pointsOf(std::vector<float>(64, -0.5f)), rt::ScopeLeft, 63).found);
}

TEST(ScopeTrace, AFallingEdgeIsNotARisingOne)
{
    EXPECT_FALSE(latestRisingCrossing(pointsOf({1.0f, 0.5f, -0.5f, -1.0f}), rt::ScopeLeft, 3).found);
}

TEST(ScopeTrace, ANanInTheSoundFindsNoFalseCrossing)
{
    const auto points = pointsOf({-1.0f, NAN, 1.0f, -1.0f, 1.0f});
    const ScopeCrossing crossing = latestRisingCrossing(points, rt::ScopeLeft, 4);
    ASSERT_TRUE(crossing.found);
    EXPECT_NEAR(crossing.point, 3.5, 1.0e-9);
}

TEST(ScopeTrace, MoreColumnsThanPointsFollowTheLineBetweenThem)
{
    const auto points = pointsOf({0.0f, 1.0f, 0.0f});
    std::array<ScopeColumn, 8> columns;
    scopeColumns(points, rt::ScopeLeft, 0.0, 0.25, columns);
    for (std::size_t column = 0; column < 4; ++column) {
        EXPECT_TRUE(columns[column].heard);
        EXPECT_NEAR(columns[column].low, 0.25f * static_cast<float>(column), 1.0e-6f);
        EXPECT_NEAR(columns[column].high, 0.25f * static_cast<float>(column + 1), 1.0e-6f);
    }
    EXPECT_NEAR(columns[4].high, 1.0f, 1.0e-6f);
    EXPECT_NEAR(columns[4].low, 0.75f, 1.0e-6f);
    EXPECT_NEAR(columns[7].low, 0.0f, 1.0e-6f);
}

TEST(ScopeTrace, NeighbouringColumnsMeetAtTheirEdge)
{
    // Or a steep stretch of a line would have gaps in it.
    std::vector<float> values(64);
    for (std::size_t index = 0; index < values.size(); ++index) {
        values[index] = std::sin(static_cast<float>(index) * 0.7f);
    }
    std::array<ScopeColumn, 20> columns;
    scopeColumns(pointsOf(values), rt::ScopeLeft, 3.3, 2.7, columns);
    for (std::size_t column = 1; column < columns.size(); ++column) {
        EXPECT_LE(columns[column].low, columns[column - 1].high + 1.0e-5f) << column;
        EXPECT_GE(columns[column].high, columns[column - 1].low - 1.0e-5f) << column;
    }
}

TEST(ScopeTrace, AFractionOfAPointMovesAColumnByAFraction)
{
    // What keeps a triggered tone from shaking: a crossing falls between two
    // points, differently each time.
    std::vector<float> values(256);
    for (std::size_t index = 0; index < values.size(); ++index) {
        values[index] = std::sin(static_cast<float>(index) * 0.2f);
    }
    const auto points = pointsOf(values);
    std::array<ScopeColumn, 40> one;
    std::array<ScopeColumn, 40> two;
    scopeColumns(points, rt::ScopeLeft, 10.0, 2.4, one);
    scopeColumns(points, rt::ScopeLeft, 10.01, 2.4, two);
    for (std::size_t column = 0; column < one.size(); ++column) {
        EXPECT_NEAR(one[column].high, two[column].high, 0.004f) << column;
        EXPECT_NEAR(one[column].low, two[column].low, 0.004f) << column;
    }
}

TEST(ScopeTrace, FewerColumnsThanPointsKeepEveryExtreme)
{
    std::vector<float> values(1000, 0.0f);
    values[333] = 1.0f;
    values[777] = -1.0f;
    const auto points = pointsOf(values);
    std::array<ScopeColumn, 10> columns;
    scopeColumns(points, rt::ScopeLeft, 0.0, 100.0, columns);
    float highest = -1.0f;
    float lowest = 1.0f;
    for (const ScopeColumn &column : columns) {
        EXPECT_TRUE(column.heard);
        highest = std::max(highest, column.high);
        lowest = std::min(lowest, column.low);
    }
    EXPECT_EQ(highest, 1.0f);
    EXPECT_EQ(lowest, -1.0f);
    EXPECT_EQ(columns[3].high, 1.0f);
    EXPECT_EQ(columns[2].high, 0.0f) << "one sample stays in its own column";
    EXPECT_EQ(columns[7].low, -1.0f);
}

TEST(ScopeTrace, WhatTheSoundHasNotReachedIsNotDrawn)
{
    const auto points = pointsOf({0.5f, 0.5f, 0.5f, 0.5f});
    std::array<ScopeColumn, 8> columns;
    scopeColumns(points, rt::ScopeLeft, -4.0, 1.0, columns);
    EXPECT_FALSE(columns[0].heard);
    EXPECT_FALSE(columns[3].heard);
    EXPECT_TRUE(columns[4].heard);
    EXPECT_TRUE(columns[6].heard);
    EXPECT_FALSE(columns[7].heard) << "past the newest point, no more than before the oldest";
}
