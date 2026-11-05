// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Speech Now: the sound passes bit for bit, the states keep their timing to
// the frame, and they come out the same at every quantum.

#include "allocationtrap.h"
#include "blocks/speechnow/speechnow.h"
#include "blocksizeinvariance.h"
#include "dsp/paramvalue.h"
#include "offlinerunner.h"
#include "rt/meter.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numbers>
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

void set(SpeechNow &block, std::string_view id, float value)
{
    ASSERT_TRUE(block.setParam(id, ParamValue::ofFloat(value)).has_value()) << id;
}

void pick(SpeechNow &block, const char *detector)
{
    ASSERT_TRUE(block.setParam("detector", ParamValue::ofEnum(detector)).has_value());
}

void frames(SpeechNow &block, std::uint32_t count, bool voiced)
{
    for (std::uint32_t frame = 0; frame < count; ++frame) {
        block.step(voiced);
    }
}

/// A tone that swells five times a second for \a talk seconds, then digital
/// silence for \a pause, again and again: enough of a voice's shape for the
/// detector that has no model to open on it.
[[nodiscard]] Signal bursts(std::size_t length, std::size_t talk, std::size_t pause)
{
    Signal signal(length);
    for (std::size_t n = 0; n < length; ++n) {
        if (n % (talk + pause) >= talk) {
            continue;
        }
        const double t = static_cast<double>(n) / static_cast<double>(kSecond);
        const double swell = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * 5.0 * t);
        signal[n] = static_cast<float>(0.2 * swell * std::sin(2.0 * std::numbers::pi * 800.0 * t));
    }
    return signal;
}

/// The state and the likelihood after each \a chunk samples, run at a
/// quantum of \a quantum.
[[nodiscard]] std::vector<std::pair<float, float>>
readings(const char *detector, const Signal &signal, std::size_t chunk, std::uint32_t quantum)
{
    SpeechNow block;
    pick(block, detector);
    BlockRunner runner(block, 2, 2, kTestSampleRate, 8192);
    EXPECT_TRUE(runner.prepare().has_value());
    std::vector<std::pair<float, float>> read;
    for (std::size_t from = 0; from + chunk <= signal.size(); from += chunk) {
        const Signal part(signal.begin() + static_cast<std::ptrdiff_t>(from),
                          signal.begin() + static_cast<std::ptrdiff_t>(from + chunk));
        (void)runner.run({part, part}, CycleShape::fixed(quantum));
        read.emplace_back(block.state(), block.voice());
    }
    return read;
}

} // namespace

TEST(SpeechNow, PassesTheSoundBitForBitWithNoDelay)
{
    for (const char *detector : {"model", "levelAndRhythm"}) {
        SpeechNow block;
        pick(block, detector);
        EXPECT_EQ(block.info().latencySamples, 0U);
        BlockRunner runner(block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        const Signal left = whiteNoise(kSecond, 0.5f, 1);
        const Signal right = bursts(kSecond, 4800, 2400);
        const MultiSignal out = runner.run({left, right}, CycleShape::random(1, 2048, 7));
        ASSERT_EQ(out.size(), 2U);
        EXPECT_EQ(out[0], left) << detector;
        EXPECT_EQ(out[1], right) << detector;
    }
}

TEST(SpeechNow, OneStrayFrameShowsNothing)
{
    SpeechNow block;
    frames(block, 2, true);
    block.step(false);
    frames(block, 2, true);
    EXPECT_EQ(block.state(), SpeechNow::kQuiet);
    block.step(true);
    EXPECT_EQ(block.state(), SpeechNow::kSpeaking) << "three frames in a row open it";
}

TEST(SpeechNow, HoldIsHowLongAVoiceMayPause)
{
    SpeechNow block;
    set(block, "holdMs", 200.0f);
    frames(block, 100, true);
    frames(block, 19, false);
    EXPECT_EQ(block.state(), SpeechNow::kSpeaking);
    block.step(true);
    frames(block, 19, false);
    EXPECT_EQ(block.state(), SpeechNow::kSpeaking) << "a voice in the gap starts the gap again";
    block.step(false);
    EXPECT_EQ(block.state(), SpeechNow::kQuietAgain) << "the twentieth frame of a gap is 200 ms";
}

TEST(SpeechNow, QuietAgainStaysForItsTimeAndThenIsQuiet)
{
    SpeechNow block;
    set(block, "quietAgainSeconds", 2.0f);
    frames(block, 100, true);
    frames(block, 20, false);
    ASSERT_EQ(block.state(), SpeechNow::kQuietAgain);
    frames(block, 199, false);
    EXPECT_EQ(block.state(), SpeechNow::kQuietAgain);
    block.step(false);
    EXPECT_EQ(block.state(), SpeechNow::kQuiet);
}

TEST(SpeechNow, QuietAgainOfNoughtTurnsTheMarkOff)
{
    SpeechNow block;
    set(block, "quietAgainSeconds", 0.0f);
    frames(block, 100, true);
    frames(block, 20, false);
    EXPECT_EQ(block.state(), SpeechNow::kQuiet);
}

TEST(SpeechNow, ACoughEarnsNoMark)
{
    SpeechNow block;
    set(block, "shortestTalkMs", 300.0f);
    // 290 ms of voice: the three frames that opened it count.
    frames(block, 29, true);
    frames(block, 20, false);
    EXPECT_EQ(block.state(), SpeechNow::kQuiet);

    // 300 ms, and the gap does not count as talk.
    frames(block, 30, true);
    frames(block, 20, false);
    EXPECT_EQ(block.state(), SpeechNow::kQuietAgain);
}

TEST(SpeechNow, AVoiceDuringTheMarkIsSpeakingAgain)
{
    SpeechNow block;
    frames(block, 100, true);
    frames(block, 20, false);
    ASSERT_EQ(block.state(), SpeechNow::kQuietAgain);
    frames(block, 3, true);
    EXPECT_EQ(block.state(), SpeechNow::kSpeaking);
}

TEST(SpeechNow, ResetAndSwitchingOnForgetWhatItWasSaying)
{
    SpeechNow block;
    frames(block, 10, true);
    ASSERT_EQ(block.state(), SpeechNow::kSpeaking);
    block.reset();
    EXPECT_EQ(block.state(), SpeechNow::kQuiet);
    frames(block, 10, true);
    block.switchedOn();
    EXPECT_EQ(block.state(), SpeechNow::kQuiet);
    frames(block, 2, true);
    EXPECT_EQ(block.state(), SpeechNow::kQuiet) << "the run before was forgotten too";
}

TEST(SpeechNow, TheDetectorHearsTheBurstsAndTheStatesFollow)
{
    SpeechNow block;
    pick(block, "levelAndRhythm");
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal talk = bursts(kSecond * 2, kSecond * 2, 0);
    (void)runner.run({talk, talk}, CycleShape::fixed(256));
    EXPECT_EQ(block.state(), SpeechNow::kSpeaking);
    const Signal silence(kSecond / 2, 0.0f);
    (void)runner.run({silence, silence}, CycleShape::fixed(256));
    EXPECT_EQ(block.state(), SpeechNow::kQuietAgain);
    const Signal longer(kSecond * 2, 0.0f);
    (void)runner.run({longer, longer}, CycleShape::fixed(256));
    EXPECT_EQ(block.state(), SpeechNow::kQuiet);
}

TEST(SpeechNow, PublishesItsStateItsVoiceAndItsDetector)
{
    SpeechNow block;
    pick(block, "levelAndRhythm");
    rt::BlockMeter meter;
    BlockRunner runner(block, 2, 2);
    runner.setMeter(&meter);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal talk = bursts(kSecond, kSecond, 0);
    (void)runner.run({talk, talk}, CycleShape::fixed(256));
    EXPECT_EQ(meter.value(SpeechNow::kStateValue), SpeechNow::kSpeaking);
    EXPECT_EQ(meter.value(SpeechNow::kVoiceValue), block.voice());
    EXPECT_EQ(meter.value(SpeechNow::kDetectorValue), block.detectorReading());
    EXPECT_NE(meter.value(SpeechNow::kDetectorValue), SpeechNow::kModelListening);
}

TEST(SpeechNow, SaysWhyTheModelIsNotListening)
{
    SpeechNow chosen;
    pick(chosen, "levelAndRhythm");
    BlockRunner chosenRunner(chosen, 2, 2);
    ASSERT_TRUE(chosenRunner.prepare().has_value());

    SpeechNow elsewhere;
    BlockRunner elsewhereRunner(elsewhere, 2, 2, 44100.0);
    ASSERT_TRUE(elsewhereRunner.prepare().has_value());

#ifdef RB_HAVE_RNNOISE
    EXPECT_EQ(chosen.detectorReading(), SpeechNow::kLevelAndRhythmChosen);
    EXPECT_EQ(elsewhere.detectorReading(), SpeechNow::kModelNeedsItsRate);
    SpeechNow listening;
    BlockRunner listeningRunner(listening, 2, 2);
    ASSERT_TRUE(listeningRunner.prepare().has_value());
    EXPECT_EQ(listening.detectorReading(), SpeechNow::kModelListening);
#else
    EXPECT_EQ(chosen.detectorReading(), SpeechNow::kNoModelInThisBuild);
    EXPECT_EQ(elsewhere.detectorReading(), SpeechNow::kNoModelInThisBuild);
#endif
}

TEST(SpeechNow, AChoiceOfDetectorMadeWhileItRunsTakesEffect)
{
    SpeechNow block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    block.applyParam(SpeechNow::Detector, 1.0f);
    const Signal talk = bursts(kSecond / 2, kSecond, 0);
    (void)runner.run({talk, talk}, CycleShape::fixed(256));
#ifdef RB_HAVE_RNNOISE
    EXPECT_EQ(block.detectorReading(), SpeechNow::kLevelAndRhythmChosen);
#else
    EXPECT_EQ(block.detectorReading(), SpeechNow::kNoModelInThisBuild);
#endif
}

TEST(SpeechNow, ThePlacesItShowsAreNoPartOfAPreset)
{
    const SpeechNow block;
    for (const ParamDescriptor &descriptor : block.params()) {
        if (descriptor.id == "showFloating" || descriptor.id == "showInTray") {
            EXPECT_FALSE(descriptor.flags.inPresets) << descriptor.id;
            EXPECT_EQ(block.param(descriptor.id).asBool(), false) << "a window must not appear unasked";
        }
    }
}

TEST(SpeechNow, TheStatesAreTheSameAtEveryQuantum)
{
    // A chunk every quantum divides, so that each run reads at the same
    // samples: 8192 × 3.
    const std::size_t chunk = 24576;
    const Signal signal = bursts(kSecond * 12, kSecond, kSecond / 2);
    for (const char *detector : {"model", "levelAndRhythm"}) {
        const auto reference = readings(detector, signal, chunk, 8192);
        for (const std::uint32_t quantum : {1U, 2U, 64U, 256U, 480U, 1024U, 4096U}) {
            EXPECT_EQ(readings(detector, signal, chunk, quantum), reference) << detector << " at " << quantum;
        }
    }
    bool spoke = false;
    for (const auto &[state, voice] : readings("levelAndRhythm", signal, chunk, 256)) {
        spoke = spoke || state == SpeechNow::kSpeaking;
    }
    EXPECT_TRUE(spoke) << "nothing to compare: it never opened";
}

TEST(SpeechNow, TheSoundIsTheSameAtEveryQuantum)
{
    const MultiSignal input{whiteNoise(kSecond, 0.5f, 1), whiteNoise(kSecond, 0.5f, 2)};
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    options.maxBlockFrames = 8192;
    for (const std::uint32_t fixed : {1U, 8192U}) {
        options.fixedCycleFrames = fixed;
        const InvarianceReport report =
            checkBlockSizeInvariance([] { return std::make_unique<SpeechNow>(); }, input, options);
        EXPECT_TRUE(report.passed) << fixed << ": " << report.summary;
    }
}

TEST(SpeechNow, AllocatesNothing)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "no allocation trap in this build";
    }
    for (const char *detector : {"model", "levelAndRhythm"}) {
        SpeechNow block;
        pick(block, detector);
        BlockRunner runner(block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        const Signal talk = bursts(kSecond * 2, 24000, 12000);
        std::size_t allocations = 0;
        (void)runner.runWatchingForAllocations({talk, talk}, CycleShape::random(1, 2048, 3), allocations);
        EXPECT_EQ(allocations, 0U) << detector;
    }
}

TEST(SpeechNow, CostsLittleWithEitherDetector)
{
    for (const auto &[detector, budget] : {std::pair{"model", 0.012}, std::pair{"levelAndRhythm", 0.001}}) {
        SpeechNow block;
        pick(block, detector);
        BlockRunner runner(block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        const MultiSignal input{whiteNoise(kSecond * 30, 0.1f, 1), whiteNoise(kSecond * 30, 0.1f, 2)};
        const auto started = std::chrono::steady_clock::now();
        (void)runner.run(input, CycleShape::fixed(256));
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        const double share = took / 30.0;
        std::cerr << "speech now, " << detector << ": " << share * 100.0 << " % of a core\n";
#ifdef NDEBUG
        EXPECT_LT(share, budget) << detector;
#else
        (void)budget;
#endif
    }
}
