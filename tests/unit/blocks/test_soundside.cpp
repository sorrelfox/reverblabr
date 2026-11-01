// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Sound Side: the sound passes bit for bit, the pan and the width are the
// formula's at every quantum, a short click reaches a reader that comes every
// 16 ms, silence is never a centred mark, and a fold before it shows.

#include "allocationtrap.h"
#include "blocks/oneearmix.h"
#include "blocks/soundside.h"
#include "blocksizeinvariance.h"
#include "dsp/paramvalue.h"
#include "offlinerunner.h"
#include "rt/meter.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr std::size_t kSecond = 48000;

void set(SoundSide &block, std::string_view id, float value)
{
    ASSERT_TRUE(block.setParam(id, ParamValue::ofFloat(value)).has_value()) << id;
}

void range(SoundSide &block)
{
    ASSERT_TRUE(block.setParam("band", ParamValue::ofEnum("range")).has_value());
}

[[nodiscard]] Signal scaled(const Signal &signal, float gain)
{
    Signal out = signal;
    for (float &sample : out) {
        sample *= gain;
    }
    return out;
}

[[nodiscard]] Signal added(const Signal &one, const Signal &other)
{
    Signal out = one;
    for (std::size_t n = 0; n < out.size(); ++n) {
        out[n] += other[n];
    }
    return out;
}

struct Rig {
    SoundSide block;
    rt::BlockMeter meter;
    std::unique_ptr<BlockRunner> runner;

    Rig()
    {
        runner = std::make_unique<BlockRunner>(block, 2, 2, kTestSampleRate, 8192);
        runner->setMeter(&meter);
        EXPECT_TRUE(runner->prepare().has_value());
    }

    void feed(const Signal &left, const Signal &right, std::uint32_t quantum = 256)
    {
        (void)runner->run({left, right}, CycleShape::fixed(quantum));
    }

    [[nodiscard]] float published(std::size_t slot) const
    {
        return meter.value(slot);
    }
};

/// The pan of two sides that differ by \a leftGain and \a rightGain.
[[nodiscard]] float panOf(float leftGain, float rightGain)
{
    const float left = leftGain * leftGain;
    const float right = rightGain * rightGain;
    return (right - left) / (right + left);
}

/// What a click does: five milliseconds of a tone on one side.
[[nodiscard]] Signal click(std::size_t length, std::size_t at, float amplitude = 0.5f)
{
    Signal signal(length);
    for (std::size_t n = at; n < at + 240 && n < length; ++n) {
        signal[n] = amplitude
            * static_cast<float>(std::sin(2.0 * std::numbers::pi * 2000.0 * static_cast<double>(n) / 48000.0));
    }
    return signal;
}

} // namespace

TEST(SoundSide, PassesTheSoundBitForBitWithNoDelay)
{
    SoundSide block;
    EXPECT_EQ(block.info().latencySamples, 0U);
    EXPECT_EQ(block.info().gainReach, GainReach::Unity);
    for (const char *band : {"all", "range"}) {
        ASSERT_TRUE(block.setParam("band", ParamValue::ofEnum(band)).has_value());
        BlockRunner runner(block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        const Signal left = whiteNoise(kSecond, 0.5f, 1);
        const Signal right = whiteNoise(kSecond, 0.25f, 2);
        const MultiSignal out = runner.run({left, right}, CycleShape::random(1, 2048, 7));
        ASSERT_EQ(out.size(), 2U);
        EXPECT_EQ(out[0], left) << band;
        EXPECT_EQ(out[1], right) << band;
    }
}

TEST(SoundSide, TheSoundIsTheSameAtEveryQuantum)
{
    const MultiSignal input{whiteNoise(kSecond, 0.5f, 1), whiteNoise(kSecond, 0.5f, 2)};
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    options.maxBlockFrames = 8192;
    for (const std::uint32_t fixed : {1U, 8192U}) {
        options.fixedCycleFrames = fixed;
        const InvarianceReport report =
            checkBlockSizeInvariance([] { return std::make_unique<SoundSide>(); }, input, options);
        EXPECT_TRUE(report.passed) << fixed << ": " << report.summary;
    }
}

TEST(SoundSide, ThePanIsTheFormulaAtEveryQuantum)
{
    const Signal tone = sine(kSecond / 2, 1000.0, kTestSampleRate, 0.25f);
    const Signal noise = whiteNoise(kSecond / 2, 0.25f, 5);
    struct Row {
        float leftGain;
        float rightGain;
    };
    const double quarter = std::numbers::pi / 2.0;
    std::vector<Row> rows{{1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}};
    for (const double share : {0.1, 0.25, 0.4, 0.6, 0.75, 0.9}) {
        rows.push_back({static_cast<float>(std::cos(share * quarter)), static_cast<float>(std::sin(share * quarter))});
    }
    for (const Signal *signal : {&tone, &noise}) {
        for (const Row row : rows) {
            const float expected = panOf(row.leftGain, row.rightGain);
            for (const std::uint32_t quantum : {1U, 64U, 256U, 1024U, 8192U}) {
                Rig rig;
                rig.feed(scaled(*signal, row.leftGain), scaled(*signal, row.rightGain), quantum);
                EXPECT_NEAR(rig.published(SoundSide::kPanValue), expected, 0.02f) << quantum;
                EXPECT_EQ(rig.block.state(), SoundSide::Sounding) << quantum;
            }
        }
    }
}

TEST(SoundSide, TheBedTableHolds)
{
    // A steady bed, even on both sides, and a sound on the left alone.
    const Signal bed = whiteNoise(kSecond, 0.1f, 11);
    const Signal bedRight = whiteNoise(kSecond, 0.1f, 12);
    for (const float overBedDb : {6.0f, 0.0f, -2.0f, -6.0f, -12.0f}) {
        const float gain = std::pow(10.0f, overBedDb / 20.0f);
        const Signal step = scaled(whiteNoise(kSecond, 0.1f, 13), gain);
        const float energyRatio = gain * gain;
        const float expected = -energyRatio / (2.0f + energyRatio);
        Rig rig;
        double total = 0.0;
        const int reads = 40;
        for (int read = 0; read < reads; ++read) {
            const auto from = static_cast<std::ptrdiff_t>(read) * 1200;
            const Signal left = added(Signal(bed.begin() + from, bed.begin() + from + 1200),
                                      Signal(step.begin() + from, step.begin() + from + 1200));
            rig.feed(left, Signal(bedRight.begin() + from, bedRight.begin() + from + 1200), 1200);
            total += rig.block.pan();
        }
        EXPECT_NEAR(total / reads, expected, 0.02f) << overBedDb;
    }
}

TEST(SoundSide, WidthTellsOneSoundFromTwo)
{
    const Signal one = whiteNoise(kSecond / 2, 0.2f, 1);
    Rig same;
    same.feed(one, one);
    EXPECT_NEAR(same.block.width(), 1.0f, 0.001f);

    Rig bed;
    bed.feed(one, whiteNoise(kSecond / 2, 0.2f, 2));
    EXPECT_LT(std::abs(bed.block.width()), 0.3f) << "two unrelated sides";
    EXPECT_NEAR(bed.block.pan(), 0.0f, 0.2f);
}

TEST(SoundSide, TheLevelIsTheMeanPowerOfTheTwoSides)
{
    Rig rig;
    const Signal tone = sine(kSecond / 2, 1000.0, kTestSampleRate, 1.0f);
    rig.feed(tone, tone);
    EXPECT_NEAR(rig.published(SoundSide::kLevelValue), -3.01f, 0.05f);
    Rig oneSide;
    oneSide.feed(tone, silence(kSecond / 2));
    EXPECT_NEAR(oneSide.published(SoundSide::kLevelValue), -6.02f, 0.05f);
}

/// Whether a reader every 16 ms, whenever it starts, reads a pan past
/// \a wanted on the side of its sign at least once in the second of \a left
/// and \a right, cut into cycles of \a quantum. Answers the first start that
/// did not, or a negative number.
[[nodiscard]] double readerThatMissed(const Signal &left, const Signal &right, std::uint32_t quantum, float wanted)
{
    struct Published {
        double atMs;
        float pan;
        float state;
    };
    Rig rig;
    std::vector<Published> published;
    for (std::size_t from = 0; from + quantum <= left.size(); from += quantum) {
        const auto cycle = static_cast<std::ptrdiff_t>(quantum);
        const auto start = static_cast<std::ptrdiff_t>(from);
        rig.feed(Signal(left.begin() + start, left.begin() + start + cycle),
                 Signal(right.begin() + start, right.begin() + start + cycle),
                 quantum);
        published.push_back({static_cast<double>(from + quantum) / 48.0,
                             rig.published(SoundSide::kPanValue),
                             rig.published(SoundSide::kStateValue)});
    }
    for (double phase = 0.0; phase < 16.0; phase += 0.5) {
        bool seen = false;
        std::size_t last = 0;
        for (double when = phase; when < 1000.0; when += 16.0) {
            while (last + 1 < published.size() && published[last + 1].atMs <= when) {
                ++last;
            }
            const Published &read = published[last];
            seen = seen
                || (read.atMs <= when && read.state == static_cast<float>(SoundSide::Sounding)
                    && read.pan * wanted >= wanted * wanted);
        }
        if (!seen) {
            return phase;
        }
    }
    return -1.0;
}

constexpr std::array kQuanta{64U, 128U, 256U, 512U, 1024U, 2048U, 4096U, 8192U};

TEST(SoundSide, AFiveMillisecondClickReachesEveryReader)
{
    // Every place in a cycle, as how long a hold lasts turns on it.
    for (const std::uint32_t quantum : kQuanta) {
        for (std::size_t at = 12000; at < 12000 + 512; at += 13) {
            const Signal sound = click(kSecond, at);
            const Signal none = silence(kSecond);
            EXPECT_LT(readerThatMissed(sound, none, quantum, -0.9f), 0.0) << "left, " << quantum << ", at " << at;
            EXPECT_LT(readerThatMissed(none, sound, quantum, 0.9f), 0.0) << "right, " << quantum << ", at " << at;
        }
    }
}

TEST(SoundSide, AClickOverAQuieterSoundOnTheOtherSideReachesEveryReader)
{
    // What follows the click does not fade to Quiet: it says something else,
    // and only the hold keeps the click up for long enough.
    const Signal bed = whiteNoise(kSecond, 0.05f, 9);
    for (const std::uint32_t quantum : kQuanta) {
        for (std::size_t at = 12000; at < 12000 + 512; at += 13) {
            EXPECT_LT(readerThatMissed(click(kSecond, at, 0.8f), bed, quantum, -0.5f), 0.0) << quantum << ", at " << at;
        }
    }
}

TEST(SoundSide, TheHoldKeepsTheLoudestWindowOfACycle)
{
    // A big quantum, a loud click at its start and quieter sound after: what
    // the cycle publishes is the click.
    const std::size_t length = 8192;
    Signal left = click(length, 240, 0.8f);
    Signal right(length, 0.0f);
    const Signal after = whiteNoise(length, 0.01f, 4);
    for (std::size_t n = 2000; n < length; ++n) {
        right[n] = after[n];
    }
    Rig rig;
    rig.feed(left, right, 8192);
    EXPECT_LT(rig.published(SoundSide::kPanValue), -0.9f);
}

TEST(SoundSide, TheHoldEndsAndALaterSoundTakesOver)
{
    Rig rig;
    const Signal tone = sine(kSecond / 4, 1000.0, kTestSampleRate, 0.5f);
    rig.feed(tone, silence(kSecond / 4));
    ASSERT_LT(rig.block.pan(), -0.9f);
    const Signal quieter = scaled(tone, 0.1f);
    rig.feed(silence(kSecond / 4), quieter);
    EXPECT_GT(rig.block.pan(), 0.9f) << "a quieter sound is not locked out";
}

TEST(SoundSide, SilenceIsQuietAndNeverACentredMark)
{
    Rig rig;
    rig.feed(silence(kSecond / 4), silence(kSecond / 4));
    EXPECT_EQ(rig.block.state(), SoundSide::Quiet);
    EXPECT_EQ(rig.published(SoundSide::kStateValue), static_cast<float>(SoundSide::Quiet));
    EXPECT_LE(rig.published(SoundSide::kLevelValue), SoundSide::kNoLevel + 0.1f);
    EXPECT_TRUE(std::isfinite(rig.published(SoundSide::kPanValue)));
    EXPECT_TRUE(std::isfinite(rig.published(SoundSide::kWidthValue)));

    // A sound on the left, then silence: Quiet, and not a pan drifting to nought.
    const Signal tone = sine(kSecond / 4, 1000.0, kTestSampleRate, 0.5f);
    rig.feed(tone, silence(kSecond / 4));
    ASSERT_EQ(rig.block.state(), SoundSide::Sounding);
    rig.feed(silence(kSecond / 4), silence(kSecond / 4));
    EXPECT_EQ(rig.block.state(), SoundSide::Quiet);
}

TEST(SoundSide, UnderTheGateIsQuiet)
{
    Rig rig;
    const Signal under = whiteNoise(kSecond / 2, 0.001f, 1); // About -65 dBFS.
    rig.feed(under, whiteNoise(kSecond / 2, 0.001f, 2));
    EXPECT_EQ(rig.block.state(), SoundSide::Quiet);
    EXPECT_LT(rig.published(SoundSide::kLevelValue), -50.0f);
    EXPECT_GT(rig.published(SoundSide::kLevelValue), -80.0f) << "the level still reads";

    set(rig.block, "gateDb", -70.0f);
    rig.feed(under, under);
    EXPECT_EQ(rig.block.state(), SoundSide::Sounding) << "the gate moved under it";
}

TEST(SoundSide, TheGateIsClampedToItsRange)
{
    SoundSide block;
    EXPECT_FALSE(block.setParam("gateDb", ParamValue::ofFloat(-10.0f)).has_value());
    EXPECT_FALSE(block.setParam("gateDb", ParamValue::ofFloat(-90.0f)).has_value());
    EXPECT_FALSE(block.setParam("fromHz", ParamValue::ofFloat(50.0f)).has_value());
    EXPECT_FALSE(block.setParam("toHz", ParamValue::ofFloat(20000.0f)).has_value());
}

TEST(SoundSide, IdenticalSidesReadAsAlikeAfterFiveSeconds)
{
    Rig rig;
    const Signal noise = whiteNoise(kSecond * 3, 0.1f, 1);
    rig.feed(noise, noise);
    EXPECT_EQ(rig.block.state(), SoundSide::Sounding) << "a centred sound for a moment is no fold";
    rig.feed(noise, noise);
    EXPECT_EQ(rig.block.state(), SoundSide::SidesAlike);
    EXPECT_EQ(rig.published(SoundSide::kStateValue), static_cast<float>(SoundSide::SidesAlike));
}

TEST(SoundSide, ItClearsAtTheNextStereoSound)
{
    Rig rig;
    const Signal noise = whiteNoise(kSecond * 6, 0.1f, 1);
    rig.feed(noise, noise);
    ASSERT_EQ(rig.block.state(), SoundSide::SidesAlike);
    const Signal tone = sine(kSecond / 4, 1000.0, kTestSampleRate, 0.3f);
    rig.feed(tone, silence(kSecond / 4));
    EXPECT_EQ(rig.block.state(), SoundSide::Sounding);
    EXPECT_LT(rig.block.pan(), -0.9f);
}

TEST(SoundSide, APauseDoesNotStartTheCountAgain)
{
    Rig rig;
    const Signal noise = whiteNoise(kSecond * 3, 0.1f, 1);
    rig.feed(noise, noise);
    rig.feed(silence(kSecond), silence(kSecond));
    EXPECT_EQ(rig.block.state(), SoundSide::Quiet);
    rig.feed(noise, noise);
    EXPECT_EQ(rig.block.state(), SoundSide::SidesAlike);
}

/// One Ear Mix on \a choice, feeding a Sound Side.
void runAfterOneEarMix(const char *choice, BlockRunner &sideRunner)
{
    OneEarMix mix;
    ASSERT_TRUE(mix.setParam("sendTo", ParamValue::ofEnum(choice)).has_value());
    BlockRunner mixRunner(mix, 2, 2, kTestSampleRate, 8192);
    ASSERT_TRUE(mixRunner.prepare().has_value());
    // The far side arrives over 4.75 s, and the 5 s count from when it is in.
    const std::size_t length = kSecond * 10;
    const MultiSignal game{whiteNoise(length, 0.1f, 1), whiteNoise(length, 0.1f, 2)};
    const MultiSignal folded = mixRunner.run(game, CycleShape::fixed(256));
    (void)sideRunner.run(folded, CycleShape::fixed(256));
}

TEST(SoundSide, OneEarMixBeforeTheBlockShows)
{
    {
        SoundSide side;
        BlockRunner runner(side, 2, 2, kTestSampleRate, 8192);
        ASSERT_TRUE(runner.prepare().has_value());
        runAfterOneEarMix("both", runner);
        EXPECT_EQ(side.state(), SoundSide::SidesAlike) << "both ears: the sides are identical";
    }
    for (const auto &[choice, pan] : {std::pair{"left", -1.0f}, std::pair{"right", 1.0f}}) {
        SoundSide side;
        BlockRunner runner(side, 2, 2, kTestSampleRate, 8192);
        ASSERT_TRUE(runner.prepare().has_value());
        runAfterOneEarMix(choice, runner);
        EXPECT_EQ(side.state(), SoundSide::Sounding) << choice;
        EXPECT_NEAR(side.pan(), pan, 0.001f) << choice;
    }
    {
        SoundSide side;
        BlockRunner runner(side, 2, 2, kTestSampleRate, 8192);
        ASSERT_TRUE(runner.prepare().has_value());
        runAfterOneEarMix("none", runner);
        EXPECT_EQ(side.state(), SoundSide::Sounding) << "No change leaves two sides";
        EXPECT_LT(side.width(), 0.5f);
    }
}

TEST(SoundSide, OnRangeOnlyThatRangeCounts)
{
    // A loud low note on the left and a quieter high one on the right.
    const Signal low = sine(kSecond / 2, 100.0, kTestSampleRate, 0.5f);
    const Signal high = sine(kSecond / 2, 3000.0, kTestSampleRate, 0.1f);
    Rig all;
    all.feed(low, high);
    EXPECT_LT(all.block.pan(), -0.8f);

    Rig band;
    range(band.block);
    band.feed(low, high);
    EXPECT_GT(band.block.pan(), 0.5f);
}

TEST(SoundSide, ToStaysAnOctaveAboveFrom)
{
    SoundSide block;
    set(block, "fromHz", 4000.0f);
    EXPECT_EQ(block.bandInUse().to, 8000.0f) << "From pushed To up";
    set(block, "toHz", 1000.0f);
    EXPECT_EQ(block.bandInUse().from, 500.0f) << "To pushed From down";
    EXPECT_EQ(block.bandInUse().to, 1000.0f);

    const SoundSide::BandEdges top = SoundSide::octaveApart(SoundSide::kHighestFrom, SoundSide::kHighestTo, false);
    EXPECT_EQ(top.to, SoundSide::kHighestTo);
    const SoundSide::BandEdges bottom = SoundSide::octaveApart(SoundSide::kLowestFrom, SoundSide::kLowestTo, true);
    EXPECT_EQ(bottom.from, SoundSide::kLowestFrom);
}

TEST(SoundSide, TheEdgeMovedLastStandsThroughTheQueueToo)
{
    SoundSide block;
    block.applyParam(SoundSide::ToHz, 1000.0f);
    EXPECT_EQ(block.bandInUse().from, 500.0f);
    block.applyParam(SoundSide::FromHz, 800.0f);
    EXPECT_EQ(block.bandInUse().to, 1600.0f);
}

TEST(SoundSide, ALoadedSessionKeepsTheOctaveToo)
{
    SoundSide block;
    ParamValueMap state = block.saveState();
    state["fromHz"] = ParamValue::ofFloat(4000.0f);
    state["toHz"] = ParamValue::ofFloat(5000.0f);
    ASSERT_TRUE(block.loadState(state).has_value());
    EXPECT_EQ(block.bandInUse().from, 4000.0f);
    EXPECT_EQ(block.bandInUse().to, 8000.0f);
}

TEST(SoundSide, AtEveryRateTheHopIsFiveMilliseconds)
{
    for (const double rate : {44100.0, 48000.0, 96000.0, 192000.0}) {
        SoundSide block;
        BlockRunner runner(block, 2, 2, rate, 8192);
        ASSERT_TRUE(runner.prepare().has_value());
        const auto length = static_cast<std::size_t>(rate / 4.0);
        (void)runner.run({sine(length, 1000.0, rate, 0.5f), silence(length)}, CycleShape::fixed(512));
        EXPECT_NEAR(block.pan(), -1.0f, 0.001f) << rate;
        EXPECT_NEAR(block.level(), -12.04f, 0.05f) << rate;
    }
}

TEST(SoundSide, ResetAndSwitchingOnForgetWhatItHeld)
{
    Rig rig;
    const Signal tone = sine(kSecond / 4, 1000.0, kTestSampleRate, 0.5f);
    rig.feed(tone, silence(kSecond / 4));
    ASSERT_EQ(rig.block.state(), SoundSide::Sounding);
    rig.block.switchedOn();
    EXPECT_EQ(rig.block.state(), SoundSide::Quiet);
    EXPECT_EQ(rig.block.level(), SoundSide::kNoLevel);
}

TEST(SoundSide, WhereItShowsIsNoPartOfAPreset)
{
    SoundSide block;
    for (const ParamDescriptor &descriptor : block.params()) {
        if (descriptor.id == "showStrip") {
            EXPECT_FALSE(descriptor.flags.inPresets);
        }
    }
    EXPECT_EQ(block.param("showStrip").asBool(), false) << "a strip must not appear unasked";
}

TEST(SoundSide, AllocatesNothing)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "no allocation trap in this build";
    }
    for (const char *band : {"all", "range"}) {
        SoundSide block;
        ASSERT_TRUE(block.setParam("band", ParamValue::ofEnum(band)).has_value());
        BlockRunner runner(block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        const Signal noise = whiteNoise(kSecond, 0.2f, 3);
        std::size_t allocations = 0;
        (void)runner.runWatchingForAllocations({noise, noise}, CycleShape::random(1, 2048, 3), allocations);
        EXPECT_EQ(allocations, 0U) << band;
    }
}

TEST(SoundSide, CostsLittle)
{
    for (const auto &[band, budget] : {std::pair{"all", 0.0006}, std::pair{"range", 0.0015}}) {
        SoundSide block;
        ASSERT_TRUE(block.setParam("band", ParamValue::ofEnum(band)).has_value());
        BlockRunner runner(block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        const MultiSignal input{whiteNoise(kSecond * 30, 0.1f, 1), whiteNoise(kSecond * 30, 0.1f, 2)};
        const auto started = std::chrono::steady_clock::now();
        (void)runner.run(input, CycleShape::fixed(256));
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        const double share = took / 30.0;
        std::cerr << "sound side, " << band << ": " << share * 100.0 << " % of a core\n";
#ifdef NDEBUG
        EXPECT_LT(share, budget) << band;
#else
        (void)budget;
#endif
    }
}
