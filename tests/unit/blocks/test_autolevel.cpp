// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Auto Level: quiet speech lifted to the target, speech at the target
// left alone, noise not lifted, and nothing ever past −1 dBFS.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/autolevel.h"
#include "blocksizeinvariance.h"
#include "dsp/decibel.h"
#include "dsp/loudness.h"
#include "offlinerunner.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <random>
#include <vector>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr std::size_t kSecond = 48000;

/// Something with speech's shape: band-limited noise in syllables of about
/// a fifth of a second, each at its own level within ±6 dB, with a breath
/// between sentences — half a second of nothing every three seconds.
[[nodiscard]] Signal speechLike(std::size_t frames, std::uint32_t seed)
{
    const Signal noise = pinkNoise(frames, 1.0f, seed);
    Biquad low;
    Biquad high;
    low.setCoefficients(designBiquad({FilterShape::HighPass, 150.0, kButterworthQ, 0.0}, kTestSampleRate));
    high.setCoefficients(designBiquad({FilterShape::LowPass, 4000.0, kButterworthQ, 0.0}, kTestSampleRate));
    std::mt19937 random(seed);
    std::uniform_real_distribution<float> level(-6.0f, 6.0f);

    Signal out(frames);
    const std::size_t syllable = kSecond / 5;
    float syllableGain = 1.0f;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        if (frame % syllable == 0) {
            syllableGain = dbToGain(level(random));
        }
        const bool breath = frame % (3 * kSecond) >= 3 * kSecond - kSecond / 2;
        const double phase = static_cast<double>(frame % syllable) / static_cast<double>(syllable);
        const float envelope = breath ? 0.0f : static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * phase));
        out[frame] = high.process(low.process(noise[frame])) * envelope * syllableGain;
    }
    return out;
}

/// Integrated loudness as BS.1770 defines it: 400 ms blocks overlapping by
/// three quarters, those under −70 LUFS left out, then those more than
/// 10 LU under the rest left out. **The test's own**, from the
/// recommendation, so the block is not measured by the meter it uses.
[[nodiscard]] double integratedLufs(const Signal &left, const Signal &right, std::size_t from = 0)
{
    const auto stages = kWeighting(kTestSampleRate);
    Biquad shelfL;
    Biquad shelfR;
    Biquad passL;
    Biquad passR;
    shelfL.setCoefficients(stages[0]);
    shelfR.setCoefficients(stages[0]);
    passL.setCoefficients(stages[1]);
    passR.setCoefficients(stages[1]);
    std::vector<double> power(left.size());
    for (std::size_t frame = 0; frame < left.size(); ++frame) {
        const double l = passL.process(shelfL.process(left[frame]));
        const double r = passR.process(shelfR.process(right[frame]));
        power[frame] = l * l + r * r;
    }
    const std::size_t block = kSecond * 4 / 10;
    const std::size_t hop = block / 4;
    std::vector<double> blocks;
    for (std::size_t start = from; start + block <= power.size(); start += hop) {
        double sum = 0.0;
        for (std::size_t frame = start; frame < start + block; ++frame) {
            sum += power[frame];
        }
        blocks.push_back(sum / static_cast<double>(block));
    }
    const auto lufs = [](double meanSquare) { return -0.691 + 10.0 * std::log10(meanSquare); };
    const auto gatedMean = [&](double gate) {
        double sum = 0.0;
        std::size_t count = 0;
        for (const double each : blocks) {
            if (each > 0.0 && lufs(each) > gate) {
                sum += each;
                ++count;
            }
        }
        return count == 0 ? 0.0 : sum / static_cast<double>(count);
    };
    const double absolute = gatedMean(-70.0);
    return lufs(gatedMean(lufs(absolute) - 10.0));
}

/// \a signal scaled so that on both channels it reads \a lufs.
[[nodiscard]] Signal atLoudness(const Signal &signal, double lufs)
{
    const double now = integratedLufs(signal, signal);
    const float gain = static_cast<float>(std::pow(10.0, (lufs - now) / 20.0));
    Signal out(signal.size());
    std::ranges::transform(signal, out.begin(), [gain](float sample) { return sample * gain; });
    return out;
}

[[nodiscard]] MultiSignal through(AutoLevel &block, const Signal &mono)
{
    BlockRunner runner(block, 2, 2);
    EXPECT_TRUE(runner.prepare().has_value());
    return runner.run(MultiSignal{mono, mono}, CycleShape::fixed(256));
}

} // namespace

TEST(AutoLevelTest, QuietSpeechIsLiftedToTheTarget)
{
    // The criterion: speech at −35 LUFS ends within ±2 LU of the target,
    // measured once the boost has had time to rise (the last 15 s of 20).
    const Signal speech = atLoudness(speechLike(20 * kSecond, 70), -35.0);
    AutoLevel block;
    const MultiSignal out = through(block, speech);
    const double lifted = integratedLufs(out[0], out[1], 5 * kSecond);
    EXPECT_NEAR(lifted, AutoLevel::kDefaultTargetLufs, 2.0) << "speech at -35 LUFS came out at " << lifted;
}

TEST(AutoLevelTest, AnotherTargetIsReachedToo)
{
    const Signal speech = atLoudness(speechLike(20 * kSecond, 71), -38.0);
    AutoLevel block;
    ASSERT_TRUE(block.setParam("target", ParamValue::ofFloat(-24.0f)).has_value());
    const MultiSignal out = through(block, speech);
    EXPECT_NEAR(integratedLufs(out[0], out[1], 5 * kSecond), -24.0, 2.0);
}

TEST(AutoLevelTest, SpeechAlreadyAtTheTargetIsLeftAlone)
{
    // The criterion: material at the target changes by no more than 0.5 dB.
    const Signal speech = atLoudness(speechLike(20 * kSecond, 72), AutoLevel::kDefaultTargetLufs);
    AutoLevel block;
    const MultiSignal out = through(block, speech);
    const double before = integratedLufs(speech, speech, 5 * kSecond);
    const double after = integratedLufs(out[0], out[1], 5 * kSecond);
    EXPECT_NEAR(after - before, 0.0, 0.5) << "speech at the target was changed by " << after - before << " dB";
}

TEST(AutoLevelTest, LoudMaterialIsNotTurnedDown)
{
    // Upward only: speech above the target is neither boosted nor cut. It
    // is kept under the limiter's ceiling, so what comes out is what went
    // in, delayed by the lookahead.
    const Signal speech = atLoudness(speechLike(8 * kSecond, 73), -17.0);
    ASSERT_LT(peakDb(speech), -1.0f) << "the test signal would be limited, which is not what is being tested";
    AutoLevel block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    float highest = 0.0f;
    Signal left;
    for (std::size_t at = 0; at + 480 <= speech.size(); at += 480) {
        const Signal piece(speech.begin() + static_cast<Signal::difference_type>(at),
                           speech.begin() + static_cast<Signal::difference_type>(at + 480));
        const MultiSignal out = runner.run(MultiSignal{piece, piece}, CycleShape::fixed(480));
        left.insert(left.end(), out[0].begin(), out[0].end());
        highest = std::max(highest, block.boostDb());
    }
    EXPECT_LT(highest, 0.01f) << "speech 3 LU over the target was boosted";
    const std::uint32_t latency = block.info().latencySamples;
    Signal delayed(left.size(), 0.0f);
    std::copy(speech.begin(),
              speech.begin() + static_cast<Signal::difference_type>(left.size() - latency),
              delayed.begin() + latency);
    EXPECT_LE(nullTestDb(left, delayed), -90.0f);
}

TEST(AutoLevelTest, NoiseUnderTheGateIsNotLifted)
{
    const Signal hiss = atLoudness(pinkNoise(6 * kSecond, 1.0f, 74), -60.0);
    AutoLevel block;
    const MultiSignal out = through(block, hiss);
    EXPECT_LT(block.boostDb(), 0.01f);
    EXPECT_NEAR(integratedLufs(out[0], out[1]), -60.0, 0.1);
}

TEST(AutoLevelTest, NothingEverGoesPastMinusOneDecibel)
{
    // The criterion: the output never exceeds −1 dBFS. The hard case is a
    // shout straight after a long whisper, when the boost is at its highest
    // and has not had time to come down.
    Signal signal = atLoudness(speechLike(10 * kSecond, 75), -45.0);
    const Signal shout = atLoudness(speechLike(3 * kSecond, 76), -8.0);
    signal.insert(signal.end(), shout.begin(), shout.end());
    AutoLevel block;
    const MultiSignal out = through(block, signal);
    const float ceiling = dbToGain(-1.0f);
    EXPECT_LE(peak(out[0]), ceiling * 1.0001f) << "reached " << peakDb(out[0]) << " dBFS";
    EXPECT_LE(peak(out[1]), ceiling * 1.0001f);
}

TEST(AutoLevelTest, SwitchedOffItPassesTheSoundThrough)
{
    const Signal speech = atLoudness(speechLike(6 * kSecond, 77), -35.0);
    AutoLevel block;
    ASSERT_TRUE(block.setParam("boost", ParamValue::ofBool(false)).has_value());
    const MultiSignal out = through(block, speech);
    const std::uint32_t latency = block.info().latencySamples;
    ASSERT_GT(latency, 0U) << "the limiter's lookahead is a latency the block has to report";
    Signal delayed(speech.size(), 0.0f);
    std::copy(speech.begin(), speech.end() - latency, delayed.begin() + latency);
    EXPECT_LE(nullTestDb(out[0], delayed), kSilenceDb);
}

TEST(AutoLevelTest, TheBoostNeverGoesPastTheMaximum)
{
    const Signal speech = atLoudness(speechLike(12 * kSecond, 78), -45.0);
    AutoLevel block;
    ASSERT_TRUE(block.setParam("maximumBoost", ParamValue::ofFloat(6.0f)).has_value());
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    float highest = 0.0f;
    for (std::size_t at = 0; at + 256 <= speech.size(); at += 256) {
        const Signal piece(speech.begin() + static_cast<Signal::difference_type>(at),
                           speech.begin() + static_cast<Signal::difference_type>(at + 256));
        (void)runner.run(MultiSignal{piece, piece}, CycleShape::fixed(256));
        highest = std::max(highest, block.boostDb());
    }
    EXPECT_LE(highest, 6.0f + 1e-3f);
    EXPECT_GT(highest, 5.5f) << "a source 25 LU under the target was not boosted to the maximum";
}

TEST(AutoLevelTest, TheBoostRisesSlowlyAndFallsQuickly)
{
    // A whisper, then a shout: the boost takes over a second to rise, and
    // well under half a second to come off once the shout starts.
    Signal signal = atLoudness(speechLike(8 * kSecond, 79), -38.0);
    const Signal shout = atLoudness(speechLike(2 * kSecond, 80), -14.0);
    signal.insert(signal.end(), shout.begin(), shout.end());
    AutoLevel block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    std::vector<float> boost;
    for (std::size_t at = 0; at + 480 <= signal.size(); at += 480) {
        const Signal piece(signal.begin() + static_cast<Signal::difference_type>(at),
                           signal.begin() + static_cast<Signal::difference_type>(at + 480));
        (void)runner.run(MultiSignal{piece, piece}, CycleShape::fixed(480));
        boost.push_back(block.boostDb()); // every 10 ms
    }
    EXPECT_LT(boost[50], 12.0f) << "half a second in, the boost had already risen most of the way";
    EXPECT_GT(boost[790], 12.0f) << "the whisper was never lifted";
    // 0.6 s into the shout: the 400 ms window is full of it and the fall has
    // had 200 ms.
    EXPECT_LT(boost[860], 3.0f) << "the boost was still up well into the shout";
}

TEST(AutoLevelTest, ItSoundsTheSameWhateverTheQuantum)
{
    const Signal speech = atLoudness(speechLike(kSecond / 2, 81), -35.0);
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] { return std::unique_ptr<Block>(std::make_unique<AutoLevel>()); }, MultiSignal{speech, speech}, options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(AutoLevelTest, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    AutoLevel block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal speech = atLoudness(speechLike(kSecond, 82), -35.0);
    std::size_t allocations = 1;
    const MultiSignal out =
        runner.runWatchingForAllocations(MultiSignal{speech, speech}, CycleShape::random(1, 512, 3), allocations);
    EXPECT_EQ(allocations, 0U);
    EXPECT_EQ(out[0].size(), speech.size());
}

TEST(AutoLevelTest, WhatWasSetIsWhatComesBack)
{
    AutoLevel block;
    ASSERT_TRUE(block.setParam("boost", ParamValue::ofBool(false)).has_value());
    ASSERT_TRUE(block.setParam("target", ParamValue::ofFloat(-16.0f)).has_value());
    ASSERT_TRUE(block.setParam("maximumBoost", ParamValue::ofFloat(9.0f)).has_value());
    AutoLevel opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    EXPECT_FALSE(*opened.param("boost").asBool());
    EXPECT_FLOAT_EQ(*opened.param("target").asFloat(), -16.0f);
    EXPECT_FLOAT_EQ(*opened.param("maximumBoost").asFloat(), 9.0f);
    EXPECT_FALSE(block.setParam("target", ParamValue::ofFloat(-10.0f)).has_value());
    EXPECT_FALSE(block.setParam("maximumBoost", ParamValue::ofFloat(30.0f)).has_value());
}
