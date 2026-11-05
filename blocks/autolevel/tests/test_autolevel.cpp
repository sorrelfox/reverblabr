// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Auto Level: quiet speech lifted to the target, speech at the target
// left alone, noise not lifted, and nothing ever past −1 dBFS. With the cut
// on, loud speech brought down too, and the guard holding a jump.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/autolevel/autolevel.h"
#include "blocksizeinvariance.h"
#include "dsp/biquad.h"
#include "dsp/decibel.h"
#include "dsp/limiter.h"
#include "dsp/loudness.h"
#include "offlinerunner.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
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

/// A voice of even syllables: speechLike() varies them by ±6 dB, and that
/// alone moves the gain 3 dB.
[[nodiscard]] Signal evenSpeech(std::size_t frames, std::uint32_t seed, std::size_t gapEvery, std::size_t gap)
{
    const Signal noise = pinkNoise(frames, 1.0f, seed);
    Biquad low;
    Biquad high;
    low.setCoefficients(designBiquad({FilterShape::HighPass, 150.0, kButterworthQ, 0.0}, kTestSampleRate));
    high.setCoefficients(designBiquad({FilterShape::LowPass, 4000.0, kButterworthQ, 0.0}, kTestSampleRate));
    Signal out(frames);
    const std::size_t syllable = kSecond / 5;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const bool pause = gapEvery != 0 && frame % gapEvery >= gapEvery - gap;
        const double phase = static_cast<double>(frame % syllable) / static_cast<double>(syllable);
        const float envelope = pause ? 0.0f : static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * phase));
        out[frame] = high.process(low.process(noise[frame])) * envelope;
    }
    return out;
}

void set(AutoLevel &block, std::string_view id, float value)
{
    ASSERT_TRUE(block.setParam(id, ParamValue::ofFloat(value)).has_value());
}

void setOn(AutoLevel &block, std::string_view id, bool on)
{
    ASSERT_TRUE(block.setParam(id, ParamValue::ofBool(on)).has_value());
}

/// Runs \a signal a piece at a time, handing the block to \a watch after each.
template <typename Watch>
void runInPieces(BlockRunner &runner, AutoLevel &block, const Signal &signal, std::size_t piece, Watch watch)
{
    for (std::size_t at = 0; at + piece <= signal.size(); at += piece) {
        const Signal part(signal.begin() + static_cast<Signal::difference_type>(at),
                          signal.begin() + static_cast<Signal::difference_type>(at + piece));
        (void)runner.run(MultiSignal{part, part}, CycleShape::fixed(static_cast<std::uint32_t>(piece)));
        watch(block);
    }
}

/// The loudest 400 ms in the \a seconds after \a from, in LUFS, stepping 100 ms.
[[nodiscard]] double loudestIn(const Signal &signal, std::size_t from, double seconds)
{
    const std::size_t block = kSecond * 4 / 10;
    double loudest = -200.0;
    const auto last = from + static_cast<std::size_t>(seconds * static_cast<double>(kSecond)) - block;
    for (std::size_t start = from; start <= last; start += kSecond / 10) {
        const Signal part(signal.begin() + static_cast<Signal::difference_type>(start),
                          signal.begin() + static_cast<Signal::difference_type>(start + block));
        GatedLoudness reading;
        reading.prepare(kTestSampleRate);
        double sum = 0.0;
        const auto stages = kWeighting(kTestSampleRate);
        Biquad shelf;
        Biquad pass;
        shelf.setCoefficients(stages[0]);
        pass.setCoefficients(stages[1]);
        for (const float sample : part) {
            const double weighted = pass.process(shelf.process(sample));
            sum += 2.0 * weighted * weighted;
        }
        loudest = std::max(loudest, -0.691 + 10.0 * std::log10(sum / static_cast<double>(block)));
    }
    return loudest;
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

TEST(AutoLevelTest, ALoudTalkerComesDownToTheTarget)
{
    const Signal speech = atLoudness(speechLike(20 * kSecond, 90), -12.0);
    AutoLevel block;
    setOn(block, "cut", true);
    const MultiSignal out = through(block, speech);
    EXPECT_NEAR(integratedLufs(out[0], out[1], 3 * kSecond), AutoLevel::kDefaultTargetLufs, 1.0);
}

TEST(AutoLevelTest, TheCutStopsAtItsMaximum)
{
    for (const float most : {6.0f, 24.0f}) {
        const Signal speech = atLoudness(speechLike(12 * kSecond, 91), -6.0);
        AutoLevel block;
        setOn(block, "cut", true);
        set(block, "maximumCut", most);
        set(block, "target", -30.0f);
        BlockRunner runner(block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        float lowest = 0.0f;
        runInPieces(runner, block, speech, 256, [&lowest](const AutoLevel &level) {
            lowest = std::min(lowest, level.boostDb());
        });
        EXPECT_GE(lowest, -most - 1e-3f);
        EXPECT_LT(lowest, -most + 0.5f) << "speech far over the target never reached the maximum";
    }
}

TEST(AutoLevelTest, AJumpAfterAQuietTalkerIsPulledBack)
{
    // Two talkers 20 dB apart. The loud one's first 1.2 s is the 3 s reading's
    // worst moment: the boost from the quiet one still stands.
    Signal signal = atLoudness(speechLike(10 * kSecond, 92), -33.0);
    const std::size_t jump = signal.size();
    const Signal loud = atLoudness(speechLike(4 * kSecond, 93), -13.0);
    signal.insert(signal.end(), loud.begin(), loud.end());
    AutoLevel block;
    set(block, "target", -23.0f);
    setOn(block, "cut", true);
    const MultiSignal out = through(block, signal);
    EXPECT_LT(loudestIn(out[0], jump, 1.2), -23.0 + 12.0);
}

TEST(AutoLevelTest, TheGuardNeverGoesBelowTheFloor)
{
    // With the cut off the guard may take back a boost and no more, over many
    // jumps, and the glide of the pull must not pass the floor.
    Signal signal;
    for (std::uint32_t turn = 0; turn < 20; ++turn) {
        const Signal quiet = atLoudness(speechLike(4 * kSecond, 100 + turn), -40.0);
        const Signal loud = atLoudness(speechLike(kSecond, 140 + turn), -8.0);
        signal.insert(signal.end(), quiet.begin(), quiet.end());
        signal.insert(signal.end(), loud.begin(), loud.end());
    }
    AutoLevel block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    float lowest = 0.0f;
    float mostPulled = 0.0f;
    runInPieces(runner, block, signal, 64, [&](const AutoLevel &level) {
        lowest = std::min(lowest, level.boostDb());
        mostPulled = std::max(mostPulled, level.pullDb());
    });
    EXPECT_GE(lowest, 0.0f);
    EXPECT_GT(mostPulled, 1.0f) << "the guard never acted, so this tested nothing";
}

TEST(AutoLevelTest, TurningTheCutOffGlides)
{
    const Signal speech = atLoudness(speechLike(16 * kSecond, 160), -10.0);
    AutoLevel block;
    setOn(block, "cut", true);
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal first(speech.begin(), speech.begin() + static_cast<Signal::difference_type>(8 * kSecond));
    const Signal second(speech.begin() + static_cast<Signal::difference_type>(8 * kSecond), speech.end());
    runInPieces(runner, block, first, 480, [](const AutoLevel &) { });
    ASSERT_LT(block.boostDb(), -5.0f) << "the cut has to be standing for this to mean anything";

    block.applyParam(AutoLevel::Cut, 0.0f);
    float before = block.boostDb();
    float fastest = 0.0f;
    runInPieces(runner, block, second, 480, [&](const AutoLevel &level) {
        fastest = std::max(fastest, (level.boostDb() - before) * 100.0f); // dB/s
        before = level.boostDb();
    });
    // From -10 dB with a time constant of 1.5 s the slow rate starts near 7 dB/s.
    EXPECT_LT(fastest, 10.0f);
    EXPECT_NEAR(block.boostDb(), 0.0f, 0.5f);
}

TEST(AutoLevelTest, UnderTheGateTheGainDriftsBackSlowly)
{
    // A source that stops is no source that got loud: a fall at the quick
    // rate would take the lift away in every long pause.
    Signal signal = atLoudness(evenSpeech(8 * kSecond, 187, 0, 0), -35.0);
    const std::size_t stopped = signal.size();
    signal.resize(stopped + 6 * kSecond, 0.0f);
    AutoLevel block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    std::vector<float> gain;
    runInPieces(runner, block, signal, 480, [&gain](const AutoLevel &level) { gain.push_back(level.boostDb()); });
    const std::size_t atStop = stopped / 480;
    ASSERT_GT(gain[atStop], 10.0f);
    // The reading takes 3 s to empty, then the slow rate: half a second on,
    // well over half of it is left.
    const std::size_t underGate = atStop + 310;
    EXPECT_GT(gain[underGate + 50], gain[underGate] * 0.6f);
}

TEST(AutoLevelTest, APauseDoesNotMoveTheGain)
{
    const Signal speech = atLoudness(evenSpeech(16 * kSecond, 161, 2 * kSecond, kSecond / 2), -32.0);
    AutoLevel block;
    setOn(block, "cut", true);
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    std::vector<float> gain;
    runInPieces(runner, block, speech, 480, [&gain](const AutoLevel &level) { gain.push_back(level.boostDb()); });
    const auto [low, high] = std::minmax_element(gain.begin() + 800, gain.end());
    EXPECT_LT(*high - *low, 1.0f);
}

TEST(AutoLevelTest, TheGuardMakesNoClick)
{
    // A 1 kHz tone rising smoothly while the boost stands, so that any energy
    // far above the tone is the pull's own. A step in the test tone would be
    // the click.
    const std::size_t quiet = 8 * kSecond;
    const std::size_t rise = kSecond / 5;
    const std::size_t total = quiet + rise + kSecond;
    Signal tone(total);
    for (std::size_t frame = 0; frame < total; ++frame) {
        const double along =
            std::clamp(static_cast<double>(frame - std::min(frame, quiet)) / static_cast<double>(rise), 0.0, 1.0);
        const double smooth = 0.5 - 0.5 * std::cos(std::numbers::pi * along);
        const double level = std::pow(10.0, (-38.0 + 26.0 * smooth) / 20.0);
        tone[frame] = static_cast<float>(
            level * std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(frame) / 48000.0));
    }
    AutoLevel block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    Signal out;
    float mostPulled = 0.0f;
    for (std::size_t at = 0; at + 64 <= total; at += 64) {
        const Signal part(tone.begin() + static_cast<Signal::difference_type>(at),
                          tone.begin() + static_cast<Signal::difference_type>(at + 64));
        const MultiSignal piece = runner.run(MultiSignal{part, part}, CycleShape::fixed(64));
        out.insert(out.end(), piece[0].begin(), piece[0].end());
        if (at > quiet) {
            mostPulled = std::max(mostPulled, block.pullDb());
        }
    }
    ASSERT_GT(mostPulled, 3.0f) << "the tone did not make the guard pull";

    std::array<Biquad, 4> highPass;
    for (Biquad &stage : highPass) {
        stage.setCoefficients(designBiquad({FilterShape::HighPass, 10000.0, kButterworthQ, 0.0}, kTestSampleRate));
    }
    float highest = 0.0f;
    float whole = 0.0f;
    for (std::size_t frame = 0; frame < out.size(); ++frame) {
        float sample = out[frame];
        for (Biquad &stage : highPass) {
            sample = stage.process(sample);
        }
        if (frame >= quiet) {
            highest = std::max(highest, std::fabs(sample));
            whole = std::max(whole, std::fabs(out[frame]));
        }
    }
    EXPECT_LT(gainToDb(highest / whole), -80.0f);
}

TEST(AutoLevelTest, TheBandLeavesItAlone)
{
    // 1.5 LU either side of the target, inside a band of ±2.
    for (const double offset : {-1.5, 1.5}) {
        const Signal speech = atLoudness(evenSpeech(12 * kSecond, 170, 0, 0), -20.0 + offset);
        AutoLevel block;
        setOn(block, "cut", true);
        set(block, "band", 2.0f);
        BlockRunner runner(block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        float most = 0.0f;
        runInPieces(runner, block, speech, 480, [&most](const AutoLevel &level) {
            most = std::max(most, std::fabs(level.boostDb()));
        });
        EXPECT_LT(most, 0.2f) << "offset " << offset;
    }
    // Outside it the band comes off the distance: no step at its edge.
    const Signal farther = atLoudness(evenSpeech(14 * kSecond, 171, 0, 0), -26.0);
    AutoLevel block;
    set(block, "band", 2.0f);
    (void)through(block, farther);
    EXPECT_NEAR(block.boostDb(), 4.0f, 0.5f);
}

namespace
{

/// The rule written out on its own, from the block's own constants, for a
/// comparison sample for sample.
class WrittenOutRule
{
public:
    float low = 0.0f;
    float high = 0.0f;
    float target = 0.0f;

    void prepare()
    {
        m_loudness.prepare(kTestSampleRate);
        m_limiter.prepare(kTestSampleRate, 2);
        m_rise = std::exp(-1.0f / (1.5f * 48000.0f));
        m_fall = std::exp(-1.0f / (0.1f * 48000.0f));
        m_glide = std::exp(-1.0f / (0.005f * 48000.0f));
        m_decay = std::exp(-10.0f / 20.0f);
    }

    [[nodiscard]] MultiSignal run(const Signal &mono)
    {
        Signal left(mono.size());
        Signal right(mono.size());
        for (std::size_t frame = 0; frame < mono.size(); ++frame) {
            if (m_loudness.push(mono[frame], mono[frame])) {
                const float reading = m_loudness.loudness();
                m_wanted = reading < AutoLevel::kGateLufs ? 0.0f : std::clamp(target - reading, low, high);
            }
            const bool down = m_wanted < m_gain && m_loudness.loudness() >= AutoLevel::kGateLufs;
            m_gain = m_wanted + (m_gain - m_wanted) * (down ? m_fall : m_rise);
            if (m_loudness.sliceEnded()) {
                const float over = std::max(0.0f, m_loudness.fast() + m_gain - (target + 9.0f));
                m_pull = std::min(std::max(over, m_pull * m_decay), std::max(0.0f, m_gain - low));
            }
            m_smooth = m_pull + (m_smooth - m_pull) * m_glide;
            const float applied = std::max(m_gain - m_smooth, std::min(m_gain, low));
            left[frame] = mono[frame] * std::exp2(applied * (0.05f * 3.321928f));
            right[frame] = left[frame];
        }
        std::array<float *, 2> channels{left.data(), right.data()};
        AudioBlock block(channels.data(), 2, static_cast<std::uint32_t>(mono.size()));
        constexpr std::uint32_t kPiece = 4096;
        for (std::uint32_t at = 0; at < mono.size(); at += kPiece) {
            std::array<float *, 2> part{left.data() + at, right.data() + at};
            AudioBlock piece(
                part.data(), 2, std::min<std::uint32_t>(kPiece, static_cast<std::uint32_t>(mono.size()) - at));
            m_limiter.process(piece, piece.frames());
        }
        return MultiSignal{left, right};
    }

private:
    GatedLoudness m_loudness;
    LookaheadLimiter m_limiter;
    float m_rise = 0.0f;
    float m_fall = 0.0f;
    float m_glide = 0.0f;
    float m_decay = 0.0f;
    float m_gain = 0.0f;
    float m_wanted = 0.0f;
    float m_pull = 0.0f;
    float m_smooth = 0.0f;
};

} // namespace

TEST(AutoLevelTest, ABandOfZeroIsTheRuleWithNoBand)
{
    Signal signal = atLoudness(speechLike(8 * kSecond, 180), -38.0);
    const Signal loud = atLoudness(speechLike(4 * kSecond, 181), -10.0);
    signal.insert(signal.end(), loud.begin(), loud.end());

    AutoLevel block;
    setOn(block, "cut", true);
    set(block, "target", -23.0f);
    const MultiSignal out = through(block, signal);

    WrittenOutRule rule;
    rule.low = -18.0f;
    rule.high = 18.0f;
    rule.target = -23.0f;
    rule.prepare();
    const MultiSignal expected = rule.run(signal);
    EXPECT_LE(nullTestDb(out[0], expected[0]), -90.0f);
}

TEST(AutoLevelTest, ASessionSavedBeforeTheseSettingsKeepsTheCutOffAndNoBand)
{
    ParamValueMap saved;
    saved.emplace("boost", ParamValue::ofBool(true));
    saved.emplace("target", ParamValue::ofFloat(-18.0f));
    saved.emplace("maximumBoost", ParamValue::ofFloat(12.0f));
    AutoLevel block;
    ASSERT_TRUE(block.loadState(saved).has_value());
    EXPECT_FALSE(*block.param("cut").asBool());
    EXPECT_FLOAT_EQ(*block.param("band").asFloat(), 0.0f);
    EXPECT_FLOAT_EQ(*block.param("maximumCut").asFloat(), AutoLevel::kDefaultMaximumCutDb);
    EXPECT_FLOAT_EQ(*block.param("target").asFloat(), -18.0f);
}

TEST(AutoLevelTest, TheNewLimitsBindInTheBlock)
{
    AutoLevel block;
    EXPECT_FALSE(block.setParam("maximumCut", ParamValue::ofFloat(25.0f)).has_value());
    EXPECT_FALSE(block.setParam("maximumCut", ParamValue::ofFloat(-1.0f)).has_value());
    EXPECT_FALSE(block.setParam("band", ParamValue::ofFloat(7.0f)).has_value());
    EXPECT_FALSE(block.setParam("band", ParamValue::ofFloat(-1.0f)).has_value());
}

TEST(AutoLevelTest, WithEverythingOnItSoundsTheSameWhateverTheQuantum)
{
    Signal signal = atLoudness(speechLike(kSecond, 182), -40.0);
    const Signal loud = atLoudness(speechLike(kSecond / 2, 183), -6.0);
    signal.insert(signal.end(), loud.begin(), loud.end());
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] {
            auto made = std::make_unique<AutoLevel>();
            (void)made->setParam("cut", ParamValue::ofBool(true));
            (void)made->setParam("band", ParamValue::ofFloat(1.0f));
            return std::unique_ptr<Block>(std::move(made));
        },
        MultiSignal{signal, signal},
        options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(AutoLevelTest, WithFullScaleNoiseAndEverythingUpNothingPassesTheLimiter)
{
    AutoLevel block;
    setOn(block, "cut", true);
    set(block, "maximumBoost", 24.0f);
    set(block, "target", -14.0f);
    Signal signal = atLoudness(speechLike(6 * kSecond, 184), -49.0);
    const Signal full = whiteNoise(3 * kSecond, 1.0f, 185);
    signal.insert(signal.end(), full.begin(), full.end());
    const MultiSignal out = through(block, signal);
    EXPECT_LE(peak(out[0]), dbToGain(-1.0f) * 1.0001f);
}

TEST(AutoLevelTest, CostsLessThanAQuarterOfAPerCentOfACore)
{
    AutoLevel block;
    setOn(block, "cut", true);
    set(block, "band", 1.0f);
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal noise = whiteNoise(kSecond * 60, 0.1f, 186);
    const auto started = std::chrono::steady_clock::now();
    (void)runner.run(MultiSignal{noise, noise}, CycleShape::fixed(256));
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const double share = took / 60.0;
    std::cerr << "auto level: a minute of stereo in " << took << " s, " << share * 100.0 << " % of a core\n";
#ifdef NDEBUG
    EXPECT_LT(share, 0.0025);
#endif
}
