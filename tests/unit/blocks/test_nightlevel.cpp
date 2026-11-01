// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Night Level: at nothing it changes nothing but the delay, with no speech it
// changes nothing, the law is its table, the level is found and followed as
// told and let go, and whatever the settings do nothing rises faster than the
// limit or passes the ceiling, at every quantum.

#include "allocationtrap.h"
#include "blocks/nightlevel.h"
#include "blocksizeinvariance.h"
#include "dsp/decibel.h"
#include "dsp/loudness.h"
#include "dsp/paramvalue.h"
#include "dsp/voicelikelihood.h"
#include "offlinerunner.h"
#include "rt/meter.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr std::size_t kSecond = 48000;
constexpr std::size_t kDelay = 84;
constexpr std::array<std::uint32_t, 5> kQuanta{1, 64, 256, 1024, 8192};

void set(NightLevel &block, std::string_view id, float value)
{
    ASSERT_TRUE(block.setParam(id, ParamValue::ofFloat(value)).has_value()) << id;
}

void typed(NightLevel &block, float lufs)
{
    ASSERT_TRUE(block.setParam("pivotFrom", ParamValue::ofEnum("typed")).has_value());
    set(block, "typedPivot", lufs);
}

struct Rig {
    NightLevel block;
    rt::BlockMeter meter;
    std::unique_ptr<BlockRunner> runner;

    explicit Rig(float cut = 0.0f, float boost = 0.0f, double rate = kTestSampleRate)
    {
        set(block, "cut", cut);
        set(block, "boost", boost);
        runner = std::make_unique<BlockRunner>(block, 2, 2, rate, 8192);
        runner->setMeter(&meter);
        EXPECT_TRUE(runner->prepare().has_value());
        // The cheap detector, where a test is not about the voice.
        block.preferDetector(VoiceDetector::LevelAndRhythm);
    }

    MultiSignal feed(const Signal &signal, std::uint32_t quantum = 256)
    {
        return runner->run({signal, signal}, CycleShape::fixed(quantum));
    }

    /// A level found: three seconds of nothing but speech at \a lufs.
    void findLevel(float lufs)
    {
        for (std::uint32_t n = 0; n < NightLevel::kFirstReadingBlocks; ++n) {
            block.hearBlock(lufs, 1.0f);
        }
    }
};

/// A 1 kHz tone on both sides at \a lufs, as the block's own meter reads it.
[[nodiscard]] Signal toneAt(std::size_t frames, float lufs)
{
    const Signal tone = sine(frames, 1000.0, kTestSampleRate, 0.1f);
    GatedLoudness meter;
    meter.prepare(kTestSampleRate);
    for (std::size_t n = 0; n < kSecond; ++n) {
        (void)meter.push(tone[n], tone[n]);
    }
    const float gain = dbToGain(lufs - meter.loudness());
    Signal out = tone;
    for (float &sample : out) {
        sample *= gain;
    }
    return out;
}

void expectDelayedExactly(const Signal &out, const Signal &in)
{
    ASSERT_EQ(out.size(), in.size());
    for (std::size_t n = kDelay; n < out.size(); ++n) {
        ASSERT_EQ(out[n], in[n - kDelay]) << "at " << n;
    }
}

} // namespace

TEST(NightLevel, DelaysByTheLimiterAndReportsIt)
{
    Rig rig;
    EXPECT_EQ(rig.block.info().latencySamples, kDelay);
    EXPECT_EQ(rig.block.info().gainReach, GainReach::Adds);
}

TEST(NightLevel, NothingChangesAtZeroPercent)
{
    // Over full scale on purpose: a block set to nothing limits nothing either.
    const Signal noise = whiteNoise(kSecond / 2, 1.4f, 3);
    for (const std::uint32_t quantum : kQuanta) {
        for (const bool byHand : {false, true}) {
            Rig rig;
            if (byHand) {
                typed(rig.block, -45.0f);
            }
            rig.findLevel(-40.0f);
            const MultiSignal out = rig.feed(noise, quantum);
            expectDelayedExactly(out[0], noise);
            expectDelayedExactly(out[1], noise);
            EXPECT_EQ(rig.block.gainDb(), 0.0f);
            EXPECT_EQ(rig.meter.value(NightLevel::kGainValue), 0.0f);
        }
    }
}

TEST(NightLevel, NoSpeechNoChange)
{
    const Signal noise = pinkNoise(2 * kSecond, 0.3f, 4);
    for (const std::uint32_t quantum : kQuanta) {
        Rig rig(100.0f, 100.0f);
        const MultiSignal out = rig.feed(noise, quantum);
        expectDelayedExactly(out[0], noise);
        EXPECT_EQ(rig.block.foundLufs(), NightLevel::kNoLevel);
        EXPECT_EQ(rig.block.ceilingNowDb(), NightLevel::kPlainCeilingDb);
        EXPECT_EQ(rig.meter.value(NightLevel::kFoundValue), NightLevel::kNoLevel);
        EXPECT_EQ(std::fmod(rig.block.state(), NightLevel::kPerActing), static_cast<float>(NightLevel::NoSpeechYet));
    }
}

TEST(NightLevel, TheLawMatchesTheTable)
{
    struct Row {
        float cut;
        float boost;
        std::array<float, 7> out;
    };
    const std::array<float, 7> in{30.0f, 20.0f, 10.0f, 0.0f, -10.0f, -20.0f, -30.0f};
    const std::array<Row, 3> table{
        Row{50.0f, 0.0f, {21.9f, 14.9f, 7.9f, 0.0f, -10.0f, -20.0f, -30.0f}},
        Row{100.0f, 50.0f, {13.8f, 9.8f, 5.8f, 0.0f, -9.0f, -17.0f, -25.0f}},
        Row{100.0f, 100.0f, {13.8f, 9.8f, 5.8f, 0.0f, -8.0f, -14.0f, -20.0f}},
    };
    for (const Row &row : table) {
        for (std::size_t column = 0; column < in.size(); ++column) {
            EXPECT_NEAR(in[column] + NightLevel::lawDb(in[column], 0.0f, row.cut / 100.0f, row.boost / 100.0f),
                        row.out[column],
                        1.0e-3f);
        }
        // Left alone across the band, and under the floor.
        for (const float lu : {-5.0f, -2.0f, 3.0f, -30.5f, -60.0f}) {
            EXPECT_EQ(NightLevel::lawDb(lu, 0.0f, row.cut / 100.0f, row.boost / 100.0f), 0.0f) << lu;
        }
    }
}

TEST(NightLevel, TheBlockGoesWhereTheTableSaysAtEveryQuantum)
{
    struct Row {
        float inLu;
        float changeDb;
    };
    // Night. Typed at the far ends, so that the tone stays under the ceiling.
    for (const Row &row : {Row{30.0f, -16.2f},
                           Row{10.0f, -4.2f},
                           Row{0.0f, 0.0f},
                           Row{-20.0f, 3.0f},
                           Row{-30.0f, 5.0f},
                           Row{-33.0f, 0.0f}}) {
        const float pivot = row.inLu >= 0.0f ? -45.0f : -10.0f;
        const Signal tone = toneAt(6 * kSecond, pivot + row.inLu);
        for (const std::uint32_t quantum : {1U, 256U, 8192U}) {
            Rig rig(100.0f, 50.0f);
            typed(rig.block, pivot);
            (void)rig.feed(tone, quantum);
            EXPECT_NEAR(rig.block.gainDb(), row.changeDb, 0.15f) << row.inLu << " at " << quantum;
            EXPECT_LT(rig.block.limitedDb(), NightLevel::kActingFromDb) << row.inLu << " at " << quantum;
        }
    }
}

TEST(NightLevel, ASlowReadingNeverCuts)
{
    // A bang gone: the 3 s reading still holds it, and only the quick
    // readings may turn sound down, or the quiet after it stays pressed.
    Rig rig(100.0f, 100.0f);
    typed(rig.block, -30.0f);
    (void)rig.feed(toneAt(2 * kSecond, -10.0f));
    ASSERT_LT(rig.block.gainDb(), -9.0f);
    (void)rig.feed(toneAt(kSecond, -45.0f));
    EXPECT_GT(rig.block.gainDb(), -4.0f);
}

TEST(NightLevel, TheCeilingStandsWhereItIsToldTo)
{
    EXPECT_FLOAT_EQ(NightLevel::ceilingDb(-27.0f, 1.0f, 0.5f), -7.0f);
    EXPECT_FLOAT_EQ(NightLevel::ceilingDb(-27.0f, 0.5f, 0.0f), -1.0f);
    EXPECT_FLOAT_EQ(NightLevel::ceilingDb(-45.0f, 1.0f, 0.0f), -25.0f);
    EXPECT_FLOAT_EQ(NightLevel::ceilingDb(-45.0f, 0.5f, 0.0f), -15.0f);
    EXPECT_FLOAT_EQ(NightLevel::ceilingDb(-10.0f, 1.0f, 1.0f), -1.0f);
    // Asked for no cut, or with nothing to tie it to.
    EXPECT_FLOAT_EQ(NightLevel::ceilingDb(-45.0f, 0.0f, 1.0f), -1.0f);
    EXPECT_FLOAT_EQ(NightLevel::ceilingDb(NightLevel::kNoLevel, 1.0f, 1.0f), -1.0f);
    // Set to nothing.
    EXPECT_FLOAT_EQ(NightLevel::ceilingDb(-27.0f, 0.0f, 0.0f), NightLevel::kOutOfReachDb);
}

TEST(NightLevel, TheFirstLevelIsTheMeanOfThreeSecondsOfSpeech)
{
    Rig rig(100.0f);
    for (std::uint32_t n = 0; n + 1 < NightLevel::kFirstReadingBlocks; ++n) {
        rig.block.hearBlock(n % 2 == 0 ? -20.0f : -30.0f, 0.7f);
        ASSERT_EQ(rig.block.foundLufs(), NightLevel::kNoLevel);
    }
    rig.block.hearBlock(-30.0f, 1.0f);
    // A mean of energies, so nearer the louder.
    EXPECT_NEAR(rig.block.foundLufs(), -22.6f, 0.1f);
}

TEST(NightLevel, OnlyVoicedSoundCounts)
{
    Rig rig(100.0f);
    for (std::uint32_t n = 0; n < 2 * NightLevel::kFirstReadingBlocks; ++n) {
        rig.block.hearBlock(-20.0f, 0.69f);
        rig.block.hearBlock(-51.0f, 1.0f);
    }
    EXPECT_EQ(rig.block.foundLufs(), NightLevel::kNoLevel);
}

TEST(NightLevel, TheLevelIsClampedToTheSpeechRange)
{
    Rig low(100.0f);
    low.findLevel(-49.0f);
    EXPECT_EQ(low.block.foundLufs(), NightLevel::kLowestPivotLufs);
    Rig high(100.0f);
    high.findLevel(-3.0f);
    EXPECT_EQ(high.block.foundLufs(), NightLevel::kHighestPivotLufs);
    for (int n = 0; n < 500; ++n) {
        high.block.hearBlock(0.0f, 1.0f);
    }
    EXPECT_EQ(high.block.foundLufs(), NightLevel::kHighestPivotLufs);
}

TEST(NightLevel, AShoutedLineMovesTheLevelLittle)
{
    Rig rig(100.0f);
    rig.findLevel(-30.0f);
    for (int n = 0; n < 30; ++n) {
        rig.block.hearBlock(-18.0f, 1.0f);
    }
    // ±6 LU a step, 10 s of speech: 3 s at the step limit.
    EXPECT_NEAR(rig.block.foundLufs(), -28.2f, 0.05f);
}

TEST(NightLevel, WithNoVoiceTheLevelIsHeldAndThenLetGo)
{
    Rig rig(100.0f);
    rig.findLevel(-27.0f);
    const auto seconds = [&rig] { return std::floor(rig.block.state() / NightLevel::kPerSecond); };
    constexpr int kBlocksPerMinute = 600;
    for (int n = 0; n < 14 * kBlocksPerMinute; ++n) {
        rig.block.hearBlock(-20.0f, 0.0f);
    }
    EXPECT_EQ(rig.block.foundLufs(), -27.0f);
    EXPECT_NEAR(seconds(), 840.0f, 2.0f);
    // Silence does not count, and a voice starts the count again.
    for (int n = 0; n < 2 * kBlocksPerMinute; ++n) {
        rig.block.hearBlock(-60.0f, 0.0f);
    }
    EXPECT_EQ(rig.block.foundLufs(), -27.0f);
    rig.block.hearBlock(-27.0f, 1.0f);
    EXPECT_EQ(seconds(), 0.0f);
    for (int n = 0; n + 1 < 15 * kBlocksPerMinute - 10; ++n) {
        rig.block.hearBlock(-20.0f, 0.0f);
    }
    EXPECT_NE(rig.block.foundLufs(), NightLevel::kNoLevel);
    for (int n = 0; n < 20; ++n) {
        rig.block.hearBlock(-20.0f, 0.0f);
    }
    EXPECT_EQ(rig.block.foundLufs(), NightLevel::kNoLevel);
    EXPECT_EQ(std::fmod(rig.block.state(), NightLevel::kPerActing), static_cast<float>(NightLevel::NoSpeechYet));
}

TEST(NightLevel, ATypedLevelNeverLapsesAndTheTrackerRunsOn)
{
    Rig rig(100.0f);
    typed(rig.block, -24.0f);
    rig.findLevel(-30.0f);
    EXPECT_EQ(rig.block.pivotLufs(), -24.0f);
    EXPECT_EQ(rig.block.foundLufs(), -30.0f);
    for (int n = 0; n < 9100; ++n) {
        rig.block.hearBlock(-20.0f, 0.0f);
    }
    EXPECT_EQ(rig.block.pivotLufs(), -24.0f);
    EXPECT_EQ(rig.block.foundLufs(), NightLevel::kNoLevel);
    EXPECT_EQ(std::fmod(rig.block.state(), NightLevel::kPerActing), static_cast<float>(NightLevel::Typed));
}

TEST(NightLevel, ALapseGlidesToZero)
{
    const Signal loud = toneAt(3 * kSecond, -15.0f);
    for (const std::uint32_t quantum : kQuanta) {
        Rig rig(100.0f, 50.0f);
        rig.findLevel(-35.0f);
        (void)rig.feed(loud, quantum);
        const float cut = rig.block.gainDb();
        ASSERT_LT(cut, -9.0f) << quantum;

        for (int n = 0; n < 9000; ++n) {
            rig.block.hearBlock(-15.0f, 0.0f);
        }
        ASSERT_EQ(rig.block.foundLufs(), NightLevel::kNoLevel);

        // Measured a cycle at a time, however short it is.
        const std::uint32_t step = std::max(quantum, 256U);
        float last = cut;
        float time = 0.0f;
        const float perCycle = static_cast<float>(step) / static_cast<float>(kSecond);
        Signal piece = loud;
        piece.resize(step);
        while (time < 7.0f) {
            (void)rig.feed(piece, quantum);
            const float now = rig.block.gainDb();
            EXPECT_LE(now - last, NightLevel::kMostRiseDbPerSecond * perCycle * 1.01f) << quantum << " at " << time;
            EXPECT_GE(now, last) << quantum << " at " << time;
            last = now;
            time += perCycle;
        }
        EXPECT_EQ(rig.block.gainDb(), 0.0f) << quantum;
        const MultiSignal out = rig.feed(loud, quantum);
        expectDelayedExactly(out[0], loud);
    }
}

namespace
{

struct Setting {
    float cut;
    float boost;
    float pivot;
};

using Route = std::function<void(NightLevel &, const Setting &)>;

const Route kByQueue = [](NightLevel &block, const Setting &to) {
    block.applyParam(NightLevel::Cut, to.cut);
    block.applyParam(NightLevel::Boost, to.boost);
    block.applyParam(NightLevel::TypedPivot, to.pivot);
};

const Route kByParam = [](NightLevel &block, const Setting &to) {
    set(block, "cut", to.cut);
    set(block, "boost", to.boost);
    set(block, "typedPivot", to.pivot);
};

const Route kByState = [](NightLevel &block, const Setting &to) {
    ParamValueMap state = block.saveState();
    state["cut"] = ParamValue::ofFloat(to.cut);
    state["boost"] = ParamValue::ofFloat(to.boost);
    state["typedPivot"] = ParamValue::ofFloat(to.pivot);
    ASSERT_TRUE(block.loadState(state).has_value());
};

/// Runs \a signal from \a from, jumps to \a to, and holds the two rules on
/// what comes out.
void holdsTheRules(const Signal &signal, const Setting &from, const Setting &to, const Route &route, bool steady)
{
    Rig rig(from.cut, from.boost);
    typed(rig.block, from.pivot);
    const MultiSignal settled = rig.feed(signal);
    constexpr std::size_t kWindow = 480;
    float last = peakDb(std::span<const float>(settled[0]).subspan(settled[0].size() - kWindow, kWindow));
    route(rig.block, to);

    constexpr float kFallTakesSeconds = 0.04f;
    constexpr float kRisePerWindowDb = NightLevel::kMostRiseDbPerSecond * 0.01f;
    float time = 0.0f;
    Signal piece(kWindow);
    for (std::size_t at = 0; at + kWindow <= signal.size(); at += kWindow) {
        std::copy_n(signal.begin() + static_cast<std::ptrdiff_t>(at), kWindow, piece.begin());
        const MultiSignal out = rig.feed(piece, 256);
        const float peakNow = std::max(peakDb(out[0]), peakDb(out[1]));
        if (time >= kFallTakesSeconds) {
            ASSERT_LE(peakNow, rig.block.ceilingNowDb() + 0.01f) << "at " << time;
        }
        if (steady) {
            ASSERT_LE(peakNow - last, kRisePerWindowDb * 1.1f + 0.01f) << "at " << time;
        }
        last = peakNow;
        time += 0.01f;
    }
}

} // namespace

TEST(NightLevel, WorstCaseNothingPassesTheCeilingOrRisesFast)
{
    const std::array<Setting, 5> settings{Setting{0.0f, 0.0f, -10.0f},
                                          Setting{100.0f, 100.0f, -45.0f},
                                          Setting{100.0f, 100.0f, -10.0f},
                                          Setting{50.0f, 0.0f, -45.0f},
                                          Setting{0.0f, 100.0f, -45.0f}};
    // A square of full scale: its level is steady, so its peak is the gain.
    Signal square = sine(3 * kSecond, 200.0, kTestSampleRate, 1.0f);
    for (float &sample : square) {
        sample = sample >= 0.0f ? 1.0f : -1.0f;
    }
    const Signal noise = whiteNoise(kSecond, 1.0f, 8);
    for (const Setting &from : settings) {
        for (const Setting &to : settings) {
            holdsTheRules(square, from, to, kByQueue, true);
            holdsTheRules(noise, from, to, kByQueue, false);
        }
    }
    // Every route to the settings is the same store; the rises are held on each.
    holdsTheRules(square, settings[1], settings[0], kByParam, true);
    holdsTheRules(square, settings[1], settings[2], kByParam, true);
    holdsTheRules(square, settings[1], settings[0], kByState, true);
    holdsTheRules(square, settings[1], settings[2], kByState, true);
}

TEST(NightLevel, ALoudStepIsHeldByTheCeiling)
{
    Rig rig(100.0f, 50.0f);
    rig.findLevel(-27.0f);
    (void)rig.feed(toneAt(kSecond, -27.0f));
    const float ceiling = dbToGain(rig.block.ceilingNowDb());
    EXPECT_NEAR(gainToDb(ceiling), -7.0f, 0.01f);
    // It has stood there since the level was found.
    const MultiSignal out = rig.feed(sine(kSecond, 1000.0, kTestSampleRate, 0.9f));
    EXPECT_LE(peakDb(out[0]), -7.0f + 0.01f);
}

TEST(NightLevel, WhatAShotLeavesBehindComesBackAtTheRate)
{
    // A steady tone, a shot of 5 ms that the ceiling holds, and the tone again.
    for (const std::uint32_t quantum : {64U, 8192U}) {
        Rig rig(100.0f, 50.0f);
        typed(rig.block, -27.0f);
        Signal signal = toneAt(3 * kSecond, -27.0f);
        for (std::size_t n = kSecond; n < kSecond + 240; ++n) {
            signal[n] = n % 2 == 0 ? 1.0f : -1.0f;
        }
        const MultiSignal out = rig.feed(signal, quantum);
        constexpr std::size_t kCycle = 480;
        float last = peakDb(std::span<const float>(out[0]).subspan(kSecond - kCycle, kCycle));
        float lowest = last;
        for (std::size_t at = kSecond + 480; at + kCycle <= out[0].size(); at += kCycle) {
            const float now = peakDb(std::span<const float>(out[0]).subspan(at, kCycle));
            lowest = std::min(lowest, now);
            EXPECT_LE(now - last, NightLevel::kMostRiseDbPerSecond * 0.01f * 1.1f + 0.01f) << at << " at " << quantum;
            last = now;
        }
        EXPECT_LT(lowest, -30.0f) << "the shot left no dip, so this tested nothing";
    }
}

TEST(NightLevel, ItSaysWhichDetectorListensAndThatTheCeilingActed)
{
    Rig rig(100.0f, 50.0f, 44100.0);
    typed(rig.block, -40.0f);
    (void)rig.feed(sine(kSecond / 4, 1000.0, kTestSampleRate, 0.9f));
    const float state = rig.meter.value(NightLevel::kStateValue);
    EXPECT_EQ(std::fmod(state, NightLevel::kPerActing), static_cast<float>(NightLevel::Typed));
    EXPECT_EQ(std::fmod(std::floor(state / NightLevel::kPerActing), 10.0f), 1.0f);
#ifdef RB_HAVE_RNNOISE
    EXPECT_EQ(std::fmod(std::floor(state / NightLevel::kPerDetector), 10.0f), 3.0f) << "the model needs 48 kHz";
#endif
    // The applied gain counts what the limiter took off.
    EXPECT_LT(rig.meter.value(NightLevel::kGainValue), rig.block.gainDb());
}

TEST(NightLevel, ItReadsBothSidesBeforeWritingEither)
{
    NightLevel block;
    set(block, "cut", 100.0f);
    PrepareSpec spec;
    ASSERT_TRUE(block.prepare(spec).has_value());

    NightLevel apart;
    set(apart, "cut", 100.0f);
    ASSERT_TRUE(apart.prepare(spec).has_value());
    typed(block, -40.0f);
    typed(apart, -40.0f);

    // Long enough for the gain to come down: at nought the two sides could
    // not show it.
    constexpr std::uint32_t kFrames = 4096;
    for (int cycle = 0; cycle < 12; ++cycle) {
        const Signal leftIn = sine(kFrames, 500.0, kTestSampleRate, 0.5f);
        const Signal rightIn = sine(kFrames, 700.0, kTestSampleRate, 0.2f);
        Signal left = leftIn;
        Signal right = rightIn;
        Signal apartLeft = leftIn;
        Signal apartRight = rightIn;
        std::vector<float> outLeft(kFrames);
        std::vector<float> outRight(kFrames);
        std::array<float *, 2> in{apartLeft.data(), apartRight.data()};
        std::array<float *, 2> out{outLeft.data(), outRight.data()};
        ProcessContext separate{AudioBlock{in.data(), 2, kFrames}, AudioBlock{out.data(), 2, kFrames}, kFrames};
        apart.process(separate);
        std::array<float *, 2> both{left.data(), right.data()};
        ProcessContext shared{AudioBlock{both.data(), 2, kFrames}, AudioBlock{both.data(), 2, kFrames}, kFrames};
        block.process(shared);
        for (std::size_t n = 0; n < kFrames; ++n) {
            ASSERT_EQ(left[n], outLeft[n]) << n;
            ASSERT_EQ(right[n], outRight[n]) << n;
        }
    }
    EXPECT_LT(block.gainDb(), -1.0f) << "the gain never moved, so this tested nothing";
}

TEST(NightLevel, TheSoundIsTheSameWhateverTheQuantum)
{
    // Moving: a quiet stretch, a loud one, the ceiling and both ways of the gain.
    Signal signal = whiteNoise(2 * kSecond, 0.01f, 5);
    const Signal loud = whiteNoise(kSecond, 0.9f, 6);
    std::copy(loud.begin(), loud.begin() + kSecond / 2, signal.begin() + static_cast<std::ptrdiff_t>(kSecond));
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    options.maxBlockFrames = 8192;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] {
            auto block = std::make_unique<NightLevel>();
            set(*block, "cut", 100.0f);
            set(*block, "boost", 100.0f);
            typed(*block, -35.0f);
            return std::unique_ptr<Block>(std::move(block));
        },
        MultiSignal{signal, signal},
        options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(NightLevel, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    NightLevel block;
    set(block, "cut", 100.0f);
    set(block, "boost", 100.0f);
    BlockRunner runner(block, 2, 2, kTestSampleRate, 2048);
    typed(block, -40.0f);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal noise = whiteNoise(kSecond, 0.5f, 7);
    std::size_t allocations = 1;
    (void)runner.runWatchingForAllocations(MultiSignal{noise, noise}, CycleShape::random(1, 2048, 3), allocations);
    EXPECT_EQ(allocations, 0U);
    EXPECT_LT(block.gainDb(), -1.0f) << "the gain never moved, so this tested little";
}

TEST(NightLevel, CostsLittle)
{
    for (const auto &[model, budget] : {std::pair{true, 0.016}, std::pair{false, 0.005}}) {
        NightLevel block;
        set(block, "cut", 100.0f);
        set(block, "boost", 100.0f);
        BlockRunner runner(block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        if (!model) {
            block.preferDetector(VoiceDetector::LevelAndRhythm);
        }
        const MultiSignal input{whiteNoise(kSecond * 30, 0.1f, 1), whiteNoise(kSecond * 30, 0.1f, 2)};
        const auto started = std::chrono::steady_clock::now();
        (void)runner.run(input, CycleShape::fixed(256));
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        const double share = took / 30.0;
        std::cerr << "night level, model " << model << ": " << share * 100.0 << " % of a core\n";
#ifdef NDEBUG
        EXPECT_LT(share, budget) << model;
#else
        (void)budget;
#endif
    }
}
