// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Voice Gauge: the sound passes bit for bit, the reading holds through a pause
// and clears after Forget after, a position takes 0.5 LU to change, the marks
// bind in the block, and all of it is the same at every quantum.

#include "allocationtrap.h"
#include "blocks/speechnow.h"
#include "blocks/voicegauge.h"
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
#include <tuple>
#include <utility>
#include <vector>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr std::size_t kSecond = 48000;
constexpr float kNotSet = VoiceGauge::kNotSet;

void set(VoiceGauge &block, std::string_view id, float value)
{
    ASSERT_TRUE(block.setParam(id, ParamValue::ofFloat(value)).has_value()) << id;
}

void pick(VoiceGauge &block, const char *detector)
{
    ASSERT_TRUE(block.setParam("detector", ParamValue::ofEnum(detector)).has_value());
}

/// A tone that swells five times a second: what Level and rhythm opens on.
[[nodiscard]] Signal talk(std::size_t length, double amplitude = 0.2)
{
    Signal signal(length);
    for (std::size_t n = 0; n < length; ++n) {
        const double t = static_cast<double>(n) / static_cast<double>(kSecond);
        const double swell = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * 5.0 * t);
        signal[n] = static_cast<float>(amplitude * swell * std::sin(2.0 * std::numbers::pi * 800.0 * t));
    }
    return signal;
}

/// A steady noise far under anything Level and rhythm calls a voice, after
/// some sound: the room of a person who has stopped talking.
[[nodiscard]] Signal room(std::size_t length)
{
    return whiteNoise(length, 0.001f, 3);
}

/// A block on Level and rhythm, with a runner, for a signal to be pushed
/// through in pieces.
struct Rig {
    VoiceGauge block;
    rt::BlockMeter meter;
    std::unique_ptr<BlockRunner> runner;

    explicit Rig(std::uint32_t maxFrames = 8192)
    {
        pick(block, "levelAndRhythm");
        runner = std::make_unique<BlockRunner>(block, 2, 2, kTestSampleRate, maxFrames);
        runner->setMeter(&meter);
        EXPECT_TRUE(runner->prepare().has_value());
    }

    void feed(const Signal &signal, std::uint32_t quantum = 256)
    {
        (void)runner->run({signal, signal}, CycleShape::fixed(quantum));
    }
};

[[nodiscard]] std::vector<std::tuple<float, float, float, float>>
readings(const char *detector, const Signal &signal, std::size_t chunk, std::uint32_t quantum)
{
    VoiceGauge block;
    pick(block, detector);
    set(block, "upperMark", -20.0f);
    set(block, "lowerMark", -30.0f);
    BlockRunner runner(block, 2, 2, kTestSampleRate, 8192);
    EXPECT_TRUE(runner.prepare().has_value());
    std::vector<std::tuple<float, float, float, float>> read;
    for (std::size_t from = 0; from + chunk <= signal.size(); from += chunk) {
        const Signal part(signal.begin() + static_cast<std::ptrdiff_t>(from),
                          signal.begin() + static_cast<std::ptrdiff_t>(from + chunk));
        (void)runner.run({part, part}, CycleShape::fixed(quantum));
        read.emplace_back(block.reading(), block.wholeLevel(), block.stateWord(), block.marksWord());
    }
    return read;
}

} // namespace

TEST(VoiceGauge, PassesTheSoundBitForBitWithNoDelay)
{
    for (const char *detector : {"model", "levelAndRhythm"}) {
        VoiceGauge block;
        pick(block, detector);
        EXPECT_EQ(block.info().latencySamples, 0U);
        EXPECT_EQ(block.info().gainReach, GainReach::Unity);
        BlockRunner runner(block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        const Signal left = whiteNoise(kSecond, 0.5f, 1);
        const Signal right = talk(kSecond);
        const MultiSignal out = runner.run({left, right}, CycleShape::random(1, 2048, 7));
        ASSERT_EQ(out.size(), 2U);
        EXPECT_EQ(out[0], left) << detector;
        EXPECT_EQ(out[1], right) << detector;
    }
}

TEST(VoiceGauge, AFreshBlockHasNoMarkAndNoWord)
{
    Rig rig;
    EXPECT_EQ(rig.block.marksInUse().upper, kNotSet);
    EXPECT_EQ(rig.block.marksInUse().lower, kNotSet);
    rig.feed(talk(kSecond * 2));
    ASSERT_GT(rig.block.reading(), kSilentLufs);
    EXPECT_EQ(rig.block.position(), MarkPosition::NoMarks) << "no position word until a mark is set";
}

TEST(VoiceGauge, ANumberComesAfterASecondOfVoice)
{
    Rig rig;
    rig.feed(talk(kSecond / 2));
    EXPECT_EQ(rig.block.reading(), kSilentLufs);
    rig.feed(talk(kSecond));
    EXPECT_GT(rig.block.reading(), kSilentLufs);
}

TEST(VoiceGauge, TheReadingHoldsThroughAPauseAndClearsAfterForgetAfter)
{
    Rig rig;
    set(rig.block, "forgetSeconds", 40.0f);
    rig.feed(talk(kSecond * 3));
    const float spoken = rig.block.reading();
    ASSERT_GT(spoken, kSilentLufs);
    // The blocks that straddle the end of the voice still count.
    rig.feed(room(kSecond / 2));
    const float held = rig.block.reading();
    EXPECT_NEAR(held, spoken, 0.5f);
    for (int second = 0; second < 30; ++second) {
        rig.feed(room(kSecond));
        ASSERT_EQ(rig.block.reading(), held) << "a pause moves nothing: second " << second;
    }
    EXPECT_FALSE(rig.block.voiceNow());
    EXPECT_EQ(rig.block.sound(), VoiceGauge::RoomOnly);
    rig.feed(room(kSecond * 10));
    EXPECT_EQ(rig.block.reading(), kSilentLufs) << "after Forget after it clears";
    EXPECT_EQ(rig.block.secondsCounted(), 0U);
}

TEST(VoiceGauge, SecondsCountOnlyVoiceAndTheWholeLevelComesAtTwenty)
{
    Rig rig;
    rig.feed(talk(kSecond * 10));
    const std::uint32_t counted = rig.block.secondsCounted();
    EXPECT_GE(counted, 8U);
    EXPECT_LE(counted, 10U);
    rig.feed(room(kSecond * 5));
    EXPECT_LE(rig.block.secondsCounted(), counted + 1) << "a pause counts for nothing";
    EXPECT_EQ(rig.block.wholeLevel(), kSilentLufs);
    rig.feed(talk(kSecond * 13));
    EXPECT_EQ(rig.block.secondsCounted(), 20U);
    EXPECT_GT(rig.block.wholeLevel(), kSilentLufs);
}

TEST(VoiceGauge, NoSoundTakesTwoSecondsOfDigitalSilenceAfterEvenLoudSound)
{
    Rig rig;
    rig.feed(talk(kSecond * 2, 0.9));
    rig.feed(silence(kSecond + kSecond / 2));
    EXPECT_NE(rig.block.sound(), VoiceGauge::NoSound);
    rig.feed(silence(kSecond));
    EXPECT_EQ(rig.block.sound(), VoiceGauge::NoSound);
    EXPECT_GT(rig.block.reading(), kSilentLufs) << "the number is kept, and the surfaces say what they say";
    rig.feed(talk(kSecond / 10));
    EXPECT_NE(rig.block.sound(), VoiceGauge::NoSound);
}

TEST(VoiceGauge, SoundOnlyInAntiphaseIsStillSound)
{
    VoiceGauge block;
    pick(block, "levelAndRhythm");
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal left = whiteNoise(kSecond * 3, 0.1f, 1);
    Signal right = left;
    for (float &sample : right) {
        sample = -sample;
    }
    (void)runner.run({left, right}, CycleShape::fixed(256));
    EXPECT_NE(block.sound(), VoiceGauge::NoSound);
}

TEST(VoiceGauge, RoomOnlyIsSoundWithNoVoice)
{
    Rig rig;
    rig.feed(room(kSecond * 3));
    EXPECT_EQ(rig.block.sound(), VoiceGauge::RoomOnly);
    EXPECT_EQ(rig.block.reading(), kSilentLufs);
}

TEST(MarkPosition, ReadsAgainstBothMarksOneOrNone)
{
    const auto at = [](float reading, float upper, float lower) {
        MarkPosition position;
        return position.update(reading, upper, lower);
    };
    EXPECT_EQ(at(-24.0f, -20.0f, -30.0f), MarkPosition::Between);
    EXPECT_EQ(at(-17.0f, -20.0f, -30.0f), MarkPosition::AboveUpper);
    EXPECT_EQ(at(-35.0f, -20.0f, -30.0f), MarkPosition::BelowLower);
    EXPECT_EQ(at(-24.0f, -20.0f, kNotSet), MarkPosition::BelowUpper);
    EXPECT_EQ(at(-17.0f, -20.0f, kNotSet), MarkPosition::AboveUpper);
    EXPECT_EQ(at(-24.0f, kNotSet, -30.0f), MarkPosition::AboveLower);
    EXPECT_EQ(at(-35.0f, kNotSet, -30.0f), MarkPosition::BelowLower);
    EXPECT_EQ(at(-24.0f, kNotSet, kNotSet), MarkPosition::NoMarks);
    EXPECT_EQ(at(kSilentLufs, -20.0f, -30.0f), MarkPosition::NoMarks);
}

TEST(MarkPosition, AWordChangesOnlyHalfAUnitPastAMark)
{
    MarkPosition position;
    ASSERT_EQ(position.update(-21.0f, -20.0f, -30.0f), MarkPosition::Between);
    EXPECT_EQ(position.update(-19.6f, -20.0f, -30.0f), MarkPosition::Between);
    EXPECT_EQ(position.update(-19.5f, -20.0f, -30.0f), MarkPosition::AboveUpper);
    EXPECT_EQ(position.update(-20.4f, -20.0f, -30.0f), MarkPosition::AboveUpper);
    EXPECT_EQ(position.update(-20.5f, -20.0f, -30.0f), MarkPosition::Between);
    EXPECT_EQ(position.update(-30.4f, -20.0f, -30.0f), MarkPosition::Between);
    EXPECT_EQ(position.update(-30.5f, -20.0f, -30.0f), MarkPosition::BelowLower);
    EXPECT_EQ(position.update(-29.6f, -20.0f, -30.0f), MarkPosition::BelowLower);
    EXPECT_EQ(position.update(-29.5f, -20.0f, -30.0f), MarkPosition::Between);
}

TEST(MarkPosition, AMovedMarkReadsAtOnce)
{
    MarkPosition position;
    ASSERT_EQ(position.update(-19.8f, -20.0f, -30.0f), MarkPosition::AboveUpper);
    EXPECT_EQ(position.update(-19.8f, -19.0f, -30.0f), MarkPosition::Between);
}

TEST(VoiceGauge, TheBlockPublishesThePositionItWorkedOut)
{
    Rig rig;
    rig.feed(talk(kSecond * 2));
    const float level = rig.block.reading();
    set(rig.block, "upperMark", std::round(level) - 5.0f);
    set(rig.block, "lowerMark", std::round(level) - 10.0f);
    rig.feed(talk(kSecond / 10));
    EXPECT_EQ(rig.block.position(), MarkPosition::AboveUpper);
    set(rig.block, "upperMark", std::round(level) + 5.0f);
    set(rig.block, "lowerMark", std::round(level) - 5.0f);
    rig.feed(talk(kSecond / 10));
    EXPECT_EQ(rig.block.position(), MarkPosition::Between);
}

TEST(VoiceGauge, MovingOneMarkPastTheOtherPushesItAlong)
{
    VoiceGauge block;
    set(block, "upperMark", -20.0f);
    set(block, "lowerMark", -30.0f);
    set(block, "upperMark", -35.0f);
    EXPECT_EQ(block.marksInUse().upper, -35.0f);
    EXPECT_EQ(block.marksInUse().lower, -36.0f) << "the upper mark pushed the lower one down";

    set(block, "upperMark", -20.0f);
    set(block, "lowerMark", -30.0f);
    set(block, "lowerMark", -15.0f);
    EXPECT_EQ(block.marksInUse().lower, -15.0f);
    EXPECT_EQ(block.marksInUse().upper, -14.0f) << "the lower mark pushed the upper one up";

    block.applyParam(VoiceGauge::UpperMark, -40.0f);
    EXPECT_EQ(block.marksInUse().upper, -40.0f) << "the same through the queue";
    EXPECT_EQ(block.marksInUse().lower, -41.0f);
}

TEST(VoiceGauge, APushStopsAtTheEndsAndNeverLandsOnNotSet)
{
    const VoiceGauge::Marks low = VoiceGauge::pushed(-60.0f, -60.0f, false);
    EXPECT_EQ(low.upper, -59.0f);
    EXPECT_EQ(low.lower, -60.0f);
    const VoiceGauge::Marks high = VoiceGauge::pushed(-6.0f, -6.0f, true);
    EXPECT_EQ(high.upper, -6.0f);
    EXPECT_EQ(high.lower, -7.0f);
}

TEST(VoiceGauge, AMarkNotSetPushesNothing)
{
    const VoiceGauge::Marks marks = VoiceGauge::pushed(-40.0f, kNotSet, false);
    EXPECT_EQ(marks.upper, -40.0f);
    EXPECT_EQ(marks.lower, kNotSet);
    const VoiceGauge::Marks only = VoiceGauge::pushed(kNotSet, -40.0f, true);
    EXPECT_EQ(only.upper, kNotSet);
    EXPECT_EQ(only.lower, -40.0f);
}

TEST(VoiceGauge, MarksOutOfRangeAreRefused)
{
    VoiceGauge block;
    EXPECT_FALSE(block.setParam("upperMark", ParamValue::ofFloat(-5.0f)).has_value());
    EXPECT_FALSE(block.setParam("lowerMark", ParamValue::ofFloat(-62.0f)).has_value());
}

TEST(VoiceGauge, ALoadedSessionKeepsTheGapToo)
{
    VoiceGauge block;
    ASSERT_TRUE(
        block.loadState({{"upperMark", ParamValue::ofFloat(-30.0f)}, {"lowerMark", ParamValue::ofFloat(-20.0f)}})
            .has_value());
    EXPECT_EQ(block.marksInUse().upper, -30.0f);
    EXPECT_EQ(block.marksInUse().lower, -31.0f);
}

TEST(VoiceGauge, PublishesWhatTheSurfacesRead)
{
    Rig rig;
    set(rig.block, "upperMark", -6.0f);
    set(rig.block, "lowerMark", -60.0f);
    rig.feed(talk(kSecond * 2));
    EXPECT_EQ(rig.meter.value(VoiceGauge::kReadingValue), rig.block.reading());
    EXPECT_EQ(rig.meter.value(VoiceGauge::kWholeLevelValue), kSilentLufs);
    const auto state = static_cast<std::uint32_t>(rig.meter.value(VoiceGauge::kStateValue));
    EXPECT_EQ(state % VoiceGauge::kPerVoiceNow, static_cast<std::uint32_t>(VoiceGauge::Voice));
    EXPECT_EQ(state / VoiceGauge::kPerVoiceNow % 10, 1U);
    EXPECT_EQ(state / VoiceGauge::kPerPosition % 10, static_cast<std::uint32_t>(MarkPosition::Between));
    EXPECT_EQ(state / VoiceGauge::kPerSecond % 100, rig.block.secondsCounted());
    EXPECT_EQ(static_cast<float>(state / VoiceGauge::kPerDetector), rig.block.detectorReading());
    EXPECT_NE(rig.block.detectorReading(), SpeechNow::kModelListening);
    // -6 and -60, each counted from Not set.
    EXPECT_EQ(rig.meter.value(VoiceGauge::kMarksValue), 55.0f * 100.0f + 1.0f);
}

TEST(VoiceGauge, TheLargestStateIsExactInAFloat)
{
    const std::uint32_t largest = VoiceGauge::Voice + VoiceGauge::kPerVoiceNow
        + MarkPosition::BelowLower * VoiceGauge::kPerPosition + VoiceGauge::kWholeSeconds * VoiceGauge::kPerSecond
        + 3U * VoiceGauge::kPerDetector;
    EXPECT_EQ(static_cast<std::uint32_t>(static_cast<float>(largest)), largest);
}

TEST(VoiceGauge, ResetAndSwitchingOnForgetTheReadingAndKeepTheMarks)
{
    Rig rig;
    set(rig.block, "upperMark", -20.0f);
    rig.feed(talk(kSecond * 2));
    ASSERT_GT(rig.block.reading(), kSilentLufs);
    rig.block.switchedOn();
    EXPECT_EQ(rig.block.reading(), kSilentLufs);
    EXPECT_EQ(rig.block.position(), MarkPosition::NoMarks);
    EXPECT_EQ(rig.block.marksInUse().upper, -20.0f);
}

TEST(VoiceGauge, SaysWhyTheModelIsNotListening)
{
    VoiceGauge elsewhere;
    BlockRunner runner(elsewhere, 2, 2, 44100.0);
    ASSERT_TRUE(runner.prepare().has_value());
#ifdef RB_HAVE_RNNOISE
    EXPECT_EQ(elsewhere.detectorReading(), SpeechNow::kModelNeedsItsRate);
#else
    EXPECT_EQ(elsewhere.detectorReading(), SpeechNow::kNoModelInThisBuild);
#endif
}

TEST(VoiceGauge, ASettingChangedWhileItRunsTakesEffect)
{
    Rig rig;
    rig.feed(talk(kSecond * 2));
    rig.feed(room(kSecond));
    const float three = rig.block.reading();
    rig.block.applyParam(VoiceGauge::ReadingSeconds, 1.0f);
    rig.feed(room(kSecond / 10));
    EXPECT_NE(rig.block.reading(), three) << "a shorter mean answers in a pause too";
}

TEST(VoiceGauge, MarksAndPlacesAreNoPartOfAPreset)
{
    const VoiceGauge block;
    for (const ParamDescriptor &descriptor : block.params()) {
        if (descriptor.id == "upperMark" || descriptor.id == "lowerMark" || descriptor.id == "marksPlacedOn"
            || descriptor.id == "showFloating" || descriptor.id == "showInTray") {
            EXPECT_FALSE(descriptor.flags.inPresets) << descriptor.id;
        }
    }
    EXPECT_EQ(block.param("showFloating").asBool(), false) << "a window must not appear unasked";
    EXPECT_EQ(block.param("showInTray").asBool(), false);
}

TEST(VoiceGauge, EverythingIsTheSameAtEveryQuantum)
{
    const std::size_t chunk = 24576;
    Signal signal = talk(kSecond * 8);
    const Signal after = room(kSecond * 4);
    signal.insert(signal.end(), after.begin(), after.end());
    for (const char *detector : {"model", "levelAndRhythm"}) {
        const auto reference = readings(detector, signal, chunk, 8192);
        for (const std::uint32_t quantum : {1U, 2U, 64U, 256U, 480U, 1024U, 4096U}) {
            EXPECT_EQ(readings(detector, signal, chunk, quantum), reference) << detector << " at " << quantum;
        }
    }
    bool read = false;
    for (const auto &[reading, whole, state, marks] : readings("levelAndRhythm", signal, chunk, 256)) {
        read = read || reading > kSilentLufs;
    }
    EXPECT_TRUE(read) << "nothing to compare: it never read";
}

TEST(VoiceGauge, TheSoundIsTheSameAtEveryQuantum)
{
    const MultiSignal input{whiteNoise(kSecond, 0.5f, 1), whiteNoise(kSecond, 0.5f, 2)};
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    options.maxBlockFrames = 8192;
    for (const std::uint32_t fixed : {1U, 8192U}) {
        options.fixedCycleFrames = fixed;
        const InvarianceReport report =
            checkBlockSizeInvariance([] { return std::make_unique<VoiceGauge>(); }, input, options);
        EXPECT_TRUE(report.passed) << fixed << ": " << report.summary;
    }
}

TEST(VoiceGauge, OnSteadyNoiseItReadsAPlainMean)
{
    // Every slice voiced: VoicedLoudness alone, in the block's own wiring.
    VoicedLoudness loudness;
    loudness.prepare(kTestSampleRate);
    GatedLoudness gated;
    gated.prepare(kTestSampleRate);
    const Signal noise = whiteNoise(kSecond * 10, 0.1f, 5);
    for (const float sample : noise) {
        loudness.push(sample, sample, true);
        (void)gated.push(sample, sample);
    }
    EXPECT_NEAR(loudness.reading(), gated.loudness(), 0.1f);
}

TEST(VoiceGauge, AllocatesNothing)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "no allocation trap in this build";
    }
    for (const char *detector : {"model", "levelAndRhythm"}) {
        VoiceGauge block;
        pick(block, detector);
        BlockRunner runner(block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        const Signal voice = talk(kSecond * 2);
        std::size_t allocations = 0;
        (void)runner.runWatchingForAllocations({voice, voice}, CycleShape::random(1, 2048, 3), allocations);
        EXPECT_EQ(allocations, 0U) << detector;
    }
}

TEST(VoiceGauge, CostsLittleWithEitherDetector)
{
    for (const auto &[detector, budget] : {std::pair{"model", 0.012}, std::pair{"levelAndRhythm", 0.0015}}) {
        VoiceGauge block;
        pick(block, detector);
        BlockRunner runner(block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        const MultiSignal input{whiteNoise(kSecond * 30, 0.1f, 1), whiteNoise(kSecond * 30, 0.1f, 2)};
        const auto started = std::chrono::steady_clock::now();
        (void)runner.run(input, CycleShape::fixed(256));
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        const double share = took / 30.0;
        std::cerr << "voice gauge, " << detector << ": " << share * 100.0 << " % of a core\n";
#ifdef NDEBUG
        EXPECT_LT(share, budget) << detector;
#else
        (void)budget;
#endif
    }
}
