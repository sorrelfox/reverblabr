// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Sudden Sounds: Off is the input to the bit, only later; the other ear is
// untouched; the count and the held reading do not depend on how the cycles
// fall; and at every rate it costs little and allocates nothing.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/suddensounds.h"
#include "blocksizeinvariance.h"
#include "dsp/paramvalue.h"
#include "offlinerunner.h"
#include "rt/meter.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <vector>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr std::size_t kSecond = 48000;
constexpr std::size_t kDelay = 35;

void setStrength(SuddenSounds &block, std::string_view strength)
{
    ASSERT_TRUE(block.setParam("strength", ParamValue::ofEnum(std::string{strength})).has_value());
}

void setEar(SuddenSounds &block, std::string_view ear)
{
    ASSERT_TRUE(block.setParam("ear", ParamValue::ofEnum(std::string{ear})).has_value());
}

/// Noise with short bangs in it, \a gap apart, starting after a second.
[[nodiscard]] Signal bangs(std::size_t frames, std::size_t gap, std::uint32_t seed = 1, double rate = kSecond)
{
    Signal signal = whiteNoise(frames, 0.005f, seed);
    const Signal bang = whiteNoise(frames, 0.5f, seed + 1);
    const auto length = static_cast<std::size_t>(rate * 0.002);
    for (auto at = static_cast<std::size_t>(rate); at + length < frames; at += gap) {
        for (std::size_t n = 0; n < length; ++n) {
            signal[at + n] += bang[at + n];
        }
    }
    return signal;
}

[[nodiscard]] std::string choiceOf(const SuddenSounds &block, std::string_view id)
{
    const ParamValue value = block.param(id);
    return std::string{value.asEnum().value_or("")};
}

struct Rig {
    SuddenSounds block;
    rt::BlockMeter meter;
    std::unique_ptr<BlockRunner> runner;

    explicit Rig(std::string_view strength, double rate = kTestSampleRate)
    {
        setStrength(block, strength);
        runner = std::make_unique<BlockRunner>(block, 2, 2, rate, 8192);
        runner->setMeter(&meter);
        EXPECT_TRUE(runner->prepare().has_value());
    }

    MultiSignal feed(const MultiSignal &signal, std::uint32_t quantum = 256)
    {
        return runner->run(signal, CycleShape::fixed(quantum));
    }

    [[nodiscard]] float softened() const
    {
        return meter.value(SuddenSounds::kSoftenedValue);
    }

    [[nodiscard]] float count() const
    {
        return meter.value(SuddenSounds::kCountValue);
    }
};

TEST(SuddenSoundsTest, IsAHearingBlockThatStartsOff)
{
    SuddenSounds block;
    EXPECT_EQ(block.info().typeId, "hearing.suddensounds");
    EXPECT_EQ(block.info().category, BlockCategory::Hearing);
    EXPECT_EQ(block.info().face, FaceKind::Value);
    EXPECT_EQ(block.info().gainReach, GainReach::Adds);
    EXPECT_EQ(choiceOf(block, "strength"), "off");
    EXPECT_EQ(choiceOf(block, "ear"), "left");
    const auto descriptors = block.params();
    ASSERT_EQ(descriptors.size(), 2U);
    EXPECT_FALSE(descriptors[SuddenSounds::Ear].flags.inPresets);
    EXPECT_TRUE(descriptors[SuddenSounds::Strength].flags.inPresets);
}

TEST(SuddenSoundsTest, TheDelayIsTheSameAtEveryRateAndStrength)
{
    for (const auto &[rate, delay] : std::array<std::pair<double, std::uint32_t>, 3>{
             std::pair{44100.0, 32U}, std::pair{48000.0, 35U}, std::pair{96000.0, 70U}}) {
        for (const std::string_view strength : SuddenSounds::kStrengthIds) {
            Rig rig(strength, rate);
            EXPECT_EQ(rig.block.info().latencySamples, delay) << rate << " " << strength;
        }
    }
}

TEST(SuddenSoundsTest, OffIsTheInputToTheBitOnlyLater)
{
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        Rig rig("off", rate);
        const auto delay = rig.block.info().latencySamples;
        const auto frames = static_cast<std::size_t>(rate * 2);
        const Signal left = bangs(frames, frames / 8, 1, rate);
        const Signal right = bangs(frames, frames / 7, 3, rate);
        const MultiSignal out = rig.feed({left, right});
        for (std::size_t n = delay; n < frames; ++n) {
            ASSERT_EQ(out[0][n], left[n - delay]) << rate << " at " << n;
            ASSERT_EQ(out[1][n], right[n - delay]) << rate << " at " << n;
        }
        EXPECT_EQ(rig.softened(), 0.0f);
        EXPECT_EQ(rig.count(), 0.0f);
    }
}

TEST(SuddenSoundsTest, TheLatencyIsWhatTheSoundShows)
{
    Rig rig("medium");
    const Signal click = impulse(4096, 1000, 0.5f);
    const MultiSignal out = rig.feed({click, click});
    EXPECT_EQ(latencySamples(click, out[0], 200), static_cast<int>(kDelay));
    EXPECT_EQ(latencySamples(click, out[1], 200), static_cast<int>(kDelay));
}

TEST(SuddenSoundsTest, TheOtherEarIsUntouched)
{
    for (const std::string_view ear : {"left", "right"}) {
        SuddenSounds block;
        setStrength(block, "strong");
        setEar(block, ear);
        BlockRunner runner(block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        const Signal both = bangs(kSecond * 2, kSecond / 4);
        const MultiSignal out = runner.run({both, both}, CycleShape::fixed(256));
        const std::size_t cut = ear == "left" ? 0 : 1;
        bool softened = false;
        for (std::size_t n = kDelay; n < both.size(); ++n) {
            ASSERT_EQ(out[1 - cut][n], both[n - kDelay]) << ear << " at " << n;
            softened = softened || out[cut][n] != both[n - kDelay];
        }
        EXPECT_TRUE(softened) << ear;
    }
}

TEST(SuddenSoundsTest, ItSoftensABangAtEveryRate)
{
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        Rig rig("medium", rate);
        const auto delay = rig.block.info().latencySamples;
        const auto frames = static_cast<std::size_t>(rate * 2);
        const Signal both = bangs(frames, frames, 1, rate);
        const MultiSignal out = rig.feed({both, both});
        const auto at = static_cast<std::size_t>(rate);
        const auto length = static_cast<std::size_t>(rate * 0.002);
        const float in = rmsDb(std::span<const float>(both).subspan(at, length));
        const float heard = rmsDb(std::span<const float>(out[0]).subspan(at + delay, length));
        EXPECT_LT(heard, in - 3.0f) << rate;
        EXPECT_GT(heard, in - 15.5f) << rate;
        EXPECT_EQ(rig.count(), 1.0f) << rate;
    }
}

TEST(SuddenSoundsTest, TheSoundDoesNotDependOnTheQuantum)
{
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    options.maxBlockFrames = 8192;
    const Signal left = bangs(kSecond * 3, kSecond / 3, 1);
    const Signal right = bangs(kSecond * 3, kSecond / 5, 3);
    const InvarianceReport report = checkBlockSizeInvariance(
        [] {
            auto block = std::make_unique<SuddenSounds>();
            setStrength(*block, "medium");
            return block;
        },
        {left, right},
        options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(SuddenSoundsTest, WhatItPublishesDoesNotDependOnTheQuantum)
{
    const Signal both = bangs(kSecond * 3, kSecond / 3);
    Rig first("medium");
    const MultiSignal one = first.feed({both, both}, 1);
    EXPECT_GT(first.count(), 3.0f);
    EXPECT_GT(first.softened(), 0.0f);
    for (const std::uint32_t quantum : {64U, 256U, 1024U, 8192U}) {
        Rig other("medium");
        const MultiSignal out = other.feed({both, both}, quantum);
        ASSERT_EQ(out[0], one[0]) << quantum;
        EXPECT_EQ(other.count(), first.count()) << quantum;
        EXPECT_EQ(other.softened(), first.softened()) << quantum;
    }
}

TEST(SuddenSoundsTest, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    SuddenSounds block;
    setStrength(block, "strong");
    BlockRunner runner(block, 2, 2, kTestSampleRate, 2048);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal both = bangs(kSecond * 2, kSecond / 4);
    std::size_t allocations = 1;
    (void)runner.runWatchingForAllocations({both, both}, CycleShape::random(1, 2048, 3), allocations);
    EXPECT_EQ(allocations, 0U);
}

TEST(SuddenSoundsTest, TheDeepestCutIsHeldForASecond)
{
    Rig rig("strong");
    const Signal both = bangs(kSecond + kSecond / 8, kSecond * 2);
    (void)rig.feed({both, both});
    const float held = rig.softened();
    EXPECT_GT(held, 5.0f);
    EXPECT_LE(held, 15.0f);

    // Sound that needs no cut, up to just short of the second.
    (void)rig.feed(spread(whiteNoise(kSecond * 3 / 4, 0.005f, 9), 2));
    EXPECT_EQ(rig.softened(), held);
    (void)rig.feed(spread(whiteNoise(kSecond / 4, 0.005f, 10), 2));
    EXPECT_EQ(rig.softened(), 0.0f);
}

TEST(SuddenSoundsTest, TheCountJoinsWhatIsCloseAndForgetsAfterAMinute)
{
    Rig rig("medium");
    const auto bangAt = [](Signal &signal, std::size_t at) {
        const Signal bang = whiteNoise(48, 0.5f, static_cast<std::uint32_t>(at));
        for (std::size_t n = 0; n < bang.size(); ++n) {
            signal[at + n] += bang[n];
        }
    };
    // Two bangs 2 ms apart are one; another half a second on is a second.
    Signal first = whiteNoise(kSecond * 3, 0.005f, 20);
    bangAt(first, kSecond);
    bangAt(first, kSecond + 144);
    bangAt(first, kSecond + kSecond / 2);
    (void)rig.feed(spread(first, 2));
    EXPECT_EQ(rig.count(), 2.0f);

    // Fifty-six seconds on it is still there, and past the minute it is gone.
    (void)rig.feed(spread(whiteNoise(kSecond * 56, 0.005f, 21), 2), 8192);
    EXPECT_EQ(rig.count(), 2.0f);
    (void)rig.feed(spread(whiteNoise(kSecond * 3, 0.005f, 22), 2), 8192);
    EXPECT_EQ(rig.count(), 0.0f);
}

TEST(SuddenSoundsTest, StrengthGoingOffDoesNotClick)
{
    // A step keeps a cut in place. Off lets it go, and no step in the sound
    // follows from the setting.
    SuddenSounds block;
    setStrength(block, "strong");
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    Signal low = whiteNoise(kSecond, 0.008f, 30);
    (void)runner.run(spread(low, 2), CycleShape::fixed(256));
    const Signal loud = whiteNoise(kSecond / 10, 0.8f, 31);
    const MultiSignal cutting = runner.run(spread(loud, 2), CycleShape::fixed(256));
    setStrength(block, "off");
    const Signal goes = whiteNoise(kSecond / 5, 0.8f, 32);
    const MultiSignal after = runner.run(spread(goes, 2), CycleShape::fixed(256));

    // Over 10 ms the sound rises less than the whole cut would in one step.
    constexpr std::size_t kTen = 480;
    const float before =
        rmsDb(std::span<const float>(cutting[1]).last(kTen)) - rmsDb(std::span<const float>(cutting[0]).last(kTen));
    const float then =
        rmsDb(std::span<const float>(after[1]).first(kTen)) - rmsDb(std::span<const float>(after[0]).first(kTen));
    EXPECT_GT(before, 8.0f) << "the step was not being softened, so this tested nothing";
    EXPECT_GT(then, before - 6.0f);
}

TEST(SuddenSoundsTest, AnEarChangeMovesTheWorkToTheOtherSide)
{
    Rig rig("strong");
    (void)rig.feed(spread(whiteNoise(kSecond, 0.005f, 40), 2));
    setEar(rig.block, "right");
    (void)rig.feed(spread(whiteNoise(kSecond, 0.005f, 41), 2));
    const Signal both = bangs(kSecond * 2, kSecond * 2, 42);
    const MultiSignal out = rig.feed({both, both});
    bool softened = false;
    for (std::size_t n = kDelay; n < both.size(); ++n) {
        softened = softened || out[1][n] != both[n - kDelay];
        ASSERT_EQ(out[0][n], both[n - kDelay]) << n;
    }
    EXPECT_TRUE(softened);
}

TEST(SuddenSoundsTest, TheSettingsComeBack)
{
    SuddenSounds block;
    setStrength(block, "gentle");
    setEar(block, "right");
    const ParamValueMap saved = block.saveState();
    SuddenSounds other;
    ASSERT_TRUE(other.loadState(saved).has_value());
    EXPECT_EQ(choiceOf(other, "strength"), "gentle");
    EXPECT_EQ(choiceOf(other, "ear"), "right");
}

TEST(SuddenSoundsTest, CostsLittle)
{
    SuddenSounds block;
    setStrength(block, "medium");
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal both = bangs(kSecond * 30, kSecond / 4);
    const MultiSignal input{both, both};
    const auto started = std::chrono::steady_clock::now();
    (void)runner.run(input, CycleShape::fixed(256));
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const double share = took / 30.0;
    std::cerr << "sudden sounds: " << share * 100.0 << " % of a core\n";
#ifdef NDEBUG
    EXPECT_LT(share, 0.003);
#else
    (void)share;
#endif
}

} // namespace
