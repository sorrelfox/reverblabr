// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The One Ear Mix block: what reaches each ear, that every change of route
// keeps to the glide whatever brought it about, and that Match Levels reads
// the two mixes and sets nothing. What the fold itself does across the band
// is in dsp.QuadratureFold.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/oneearmix/oneearmix.h"
#include "blocks/volume/volume.h"
#include "blocksizeinvariance.h"
#include "dsp/audiobuffer.h"
#include "dsp/decibel.h"
#include "dsp/limiter.h"
#include "dsp/loudness.h"
#include "dsp/quadraturefold.h"
#include "offlinerunner.h"
#include "rt/meter.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <numbers>
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
constexpr std::uint32_t kFallSamples = 480;
/// More than the slowest arrival, 57 dB at 12 dB/s.
constexpr std::size_t kSettled = kSecond * 5;

void set(OneEarMix &block, const char *id, const char *choice)
{
    ASSERT_TRUE(block.setParam(id, ParamValue::ofEnum(choice)).has_value());
}

void lower(OneEarMix &block, float db)
{
    ASSERT_TRUE(block.setParam("lowerDb", ParamValue::ofFloat(db)).has_value());
}

/// \a frames of silence through \a block with no runner, one cycle at a
/// time, so that a test can look between them.
void runSilence(OneEarMix &block, std::size_t frames, std::uint32_t cycle = 64)
{
    AudioBuffer buffer;
    buffer.resize(2, cycle);
    buffer.clear();
    for (std::size_t done = 0; done < frames; done += cycle) {
        ProcessContext context{buffer.block(), buffer.block(), cycle};
        block.process(context);
    }
}

[[nodiscard]] PrepareSpec spec(rt::BlockMeter *meter = nullptr)
{
    PrepareSpec prepared;
    prepared.maxBlockFrames = 2048;
    prepared.meter = meter;
    return prepared;
}

[[nodiscard]] float tail(const Signal &signal, std::size_t frames = kSecond / 2)
{
    const std::size_t from = signal.size() - frames;
    return 20.0f * std::log10(rms(std::span<const float>{signal}.subspan(from)) * std::numbers::sqrt2_v<float>);
}

} // namespace

TEST(OneEarMix, IsANewBlockOfTheHearingGroupThatCanAddToAPeak)
{
    const OneEarMix block;
    EXPECT_EQ(block.info().typeId, "hearing.oneearmix");
    EXPECT_EQ(block.info().category, BlockCategory::Hearing);
    EXPECT_EQ(block.info().gainReach, GainReach::Adds);
    EXPECT_EQ(block.info().latencySamples, 0U);
    EXPECT_EQ(block.info().face, FaceKind::Text);
    EXPECT_EQ(block.param("sendTo").text(), "none");
    EXPECT_EQ(block.param("hear").text(), "fold");
    EXPECT_EQ(*block.param("lowerDb").asNumber(), 0.0);
}

TEST(OneEarMix, NoChangeIsBitExactAndRunsNoFilter)
{
    OneEarMix block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal input{whiteNoise(kSecond, 0.7f, 1), whiteNoise(kSecond, 0.7f, 2)};
    const MultiSignal output = runner.run(input, CycleShape::random(16, 1024, 3));
    EXPECT_EQ(output[0], input[0]);
    EXPECT_EQ(output[1], input[1]);
    EXPECT_FALSE(block.filtersRunning());
}

TEST(OneEarMix, EachChoiceSendsTheSoundWhereItsNameSays)
{
    struct Row {
        const char *choice;
        bool left;
        bool right;
    };
    for (const Row row : {Row{"left", true, false}, Row{"right", false, true}, Row{"both", true, true}}) {
        for (const char *hear : {"fold", "mono"}) {
            OneEarMix block;
            set(block, "sendTo", row.choice);
            set(block, "hear", hear);
            BlockRunner runner(block, 2, 2);
            ASSERT_TRUE(runner.prepare().has_value());
            const MultiSignal output = runner.run(
                {sine(kSettled, 1000.0, kTestSampleRate, 0.5f), sine(kSettled, 1000.0, kTestSampleRate, 0.5f)});
            EXPECT_EQ(rms(output[0]) > 0.1f, row.left) << row.choice << ' ' << hear;
            EXPECT_EQ(rms(output[1]) > 0.1f, row.right) << row.choice << ' ' << hear;
            if (!row.left) {
                EXPECT_EQ(peak(output[0]), 0.0f) << "the silent side is silent";
            }
            if (!row.right) {
                EXPECT_EQ(peak(output[1]), 0.0f) << "the silent side is silent";
            }
        }
    }
}

TEST(OneEarMix, BothEarsIsTheSameMixOnBothSides)
{
    OneEarMix block;
    set(block, "sendTo", "both");
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal output =
        runner.run({whiteNoise(kSettled + kSecond, 0.5f, 1), whiteNoise(kSettled + kSecond, 0.5f, 2)});
    const std::span<const float> left{output[0]};
    const std::span<const float> right{output[1]};
    EXPECT_TRUE(nulls(left.subspan(kSettled), right.subspan(kSettled)));
}

TEST(OneEarMix, ReachesTheLevelsOfItsTable)
{
    struct Row {
        float leftGain;
        float rightGain;
        const char *hear;
        float expected;
        float within;
    };
    const float gone = -60.0f;
    for (const Row row : {Row{1, 1, "fold", 0.2f, 0.6f},
                          Row{1, 0, "fold", -3.0f, 0.1f},
                          Row{0, 1, "fold", -3.0f, 0.1f},
                          Row{1, -1, "fold", -0.3f, 0.7f},
                          Row{1, 1, "mono", 0.0f, 0.02f},
                          Row{1, 0, "mono", -6.0f, 0.05f},
                          Row{1, -1, "mono", gone, 0.0f}}) {
        OneEarMix block;
        set(block, "sendTo", "left");
        set(block, "hear", row.hear);
        BlockRunner runner(block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        const Signal tone = sine(kSettled + kSecond, 1000.0, kTestSampleRate, 1.0f);
        Signal left = tone;
        Signal right = tone;
        for (float &sample : left) {
            sample *= row.leftGain;
        }
        for (float &sample : right) {
            sample *= row.rightGain;
        }
        const float level = tail(runner.run({left, right})[0]);
        if (row.expected == gone) {
            EXPECT_LT(level, gone) << row.hear;
        } else {
            EXPECT_NEAR(level, row.expected, row.within) << row.hear << ' ' << row.leftGain << ' ' << row.rightGain;
        }
    }
}

TEST(OneEarMix, PlainMonoIsTheAverageOfTheSides)
{
    OneEarMix block;
    set(block, "sendTo", "both");
    set(block, "hear", "mono");
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal input{whiteNoise(kSettled, 0.5f, 1), whiteNoise(kSettled, 0.5f, 2)};
    const MultiSignal output = runner.run(input);
    Signal average(kSettled);
    for (std::size_t frame = 0; frame < kSettled; ++frame) {
        average[frame] = 0.5f * (input[0][frame] + input[1][frame]);
    }
    const std::size_t from = kSettled - kSecond / 2;
    EXPECT_TRUE(nulls(std::span<const float>{output[0]}.subspan(from), std::span<const float>{average}.subspan(from)));
    EXPECT_FALSE(block.filtersRunning());
}

TEST(OneEarMix, LowerTurnsDownTheFoldAndNeverPlainMono)
{
    OneEarMix folded;
    set(folded, "sendTo", "left");
    ASSERT_TRUE(folded.prepare(spec()).has_value());
    lower(folded, 2.0f);
    runSilence(folded, kFallSamples * 2);
    EXPECT_NEAR(folded.weights()[0], QuadratureFold::kFoldGain * dbToGain(-2.0f), 1e-5f);

    OneEarMix plain;
    set(plain, "sendTo", "left");
    set(plain, "hear", "mono");
    ASSERT_TRUE(plain.prepare(spec()).has_value());
    lower(plain, 2.0f);
    runSilence(plain, kFallSamples * 2);
    EXPECT_EQ(plain.weights()[0], 0.5f);
}

TEST(OneEarMix, TheFoldedWeightIsTheOneThatKeepsACentredSoundsLevel)
{
    OneEarMix block;
    set(block, "sendTo", "left");
    ASSERT_TRUE(block.prepare(spec()).has_value());
    EXPECT_FLOAT_EQ(block.weights()[0], 1.0f / std::numbers::sqrt2_v<float>);
}

TEST(OneEarMix, SoundsTheSameWhateverTheQuantum)
{
    for (const char *choice : {"none", "left", "right", "both"}) {
        for (const char *hear : {"fold", "mono"}) {
            const MultiSignal input{whiteNoise(kSecond * 2, 0.5f, 1), whiteNoise(kSecond * 2, 0.5f, 2)};
            InvarianceOptions options;
            options.inputChannels = 2;
            options.outputChannels = 2;
            options.maxBlockFrames = 8192;
            for (const std::uint32_t fixed : {1U, 8192U}) {
                options.fixedCycleFrames = fixed;
                const InvarianceReport report = checkBlockSizeInvariance(
                    [&] {
                        auto block = std::make_unique<OneEarMix>();
                        set(*block, "sendTo", choice);
                        set(*block, "hear", hear);
                        return block;
                    },
                    input,
                    options);
                EXPECT_TRUE(report.passed) << choice << ' ' << hear << ' ' << fixed << ": " << report.summary;
            }
        }
    }
}

TEST(OneEarMix, AllocatesNothing)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the sanitizers own the allocator in this build";
    }
    OneEarMix block;
    set(block, "sendTo", "left");
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    block.applyParam(OneEarMix::MatchLevels, 1.0f);
    std::size_t allocations = 0;
    (void)runner.runWatchingForAllocations({whiteNoise(kSecond * 4, 0.5f, 1), whiteNoise(kSecond * 4, 0.5f, 2)},
                                           CycleShape::random(1, 2048, 4),
                                           allocations);
    EXPECT_EQ(allocations, 0U);
}

TEST(OneEarMix, ASideNotInAnEarArrivesAtTheRiseAndAFallTakesTenMilliseconds)
{
    OneEarMix block;
    ASSERT_TRUE(block.prepare(spec()).has_value());
    set(block, "sendTo", "left");

    // Out of the first cycle the right side is falling, and the other side's
    // first step is the floor the rise starts from.
    runSilence(block, 1, 1);
    EXPECT_NEAR(block.weights()[1], dbToGain(OneEarMix::kRiseFromDb), 1e-4f);

    runSilence(block, kFallSamples, 1);
    const float k = QuadratureFold::kFoldGain;
    EXPECT_EQ(block.weights()[0], k);
    EXPECT_EQ(block.weights()[2], 0.0f);
    EXPECT_EQ(block.weights()[3], 0.0f);
    EXPECT_LT(block.weights()[1], dbToGain(-59.0f));

    // 57 dB at 12 dB/s.
    const std::size_t arrival = static_cast<std::size_t>(4.75 * kSecond);
    runSilence(block, arrival - 2 * kSecond / 100, 1);
    EXPECT_LT(block.weights()[1], k);
    runSilence(block, 3 * kSecond / 100, 1);
    EXPECT_EQ(block.weights()[1], k);
}

TEST(OneEarMix, EveryRouteKeepsToTheGlide)
{
    // Between every pair of settings, by the routes a setting can take, from
    // a settled start. A rise is measured in dB, from the moment a weight has
    // anything to rise from; a fall has to be over in its time.
    constexpr std::array sends{"none", "left", "right", "both"};
    constexpr std::array hears{"fold", "mono"};
    const std::uint32_t cycle = 16;
    const float mostRiseDb = OneEarMix::kRiseDbPerSecond * static_cast<float>(cycle) / kSecond * 1.01f;
    for (int route = 0; route < 3; ++route) {
        for (const char *fromSend : sends) {
            for (const char *fromHear : hears) {
                for (const char *toSend : sends) {
                    for (const char *toHear : hears) {
                        OneEarMix block;
                        set(block, "sendTo", fromSend);
                        set(block, "hear", fromHear);
                        ASSERT_TRUE(block.prepare(spec()).has_value());
                        runSilence(block, kSettled, 2048);
                        const std::array<float, 4> settled = block.weights();

                        OneEarMix target;
                        set(target, "sendTo", toSend);
                        set(target, "hear", toHear);
                        const auto choiceOf = [](const char *id, std::span<const char *const> list) {
                            return static_cast<float>(
                                std::find_if(
                                    list.begin(), list.end(), [&](const char *each) { return std::string{each} == id; })
                                - list.begin());
                        };
                        if (route == 0) {
                            set(block, "sendTo", toSend);
                            set(block, "hear", toHear);
                        } else if (route == 1) {
                            block.applyParam(OneEarMix::SendTo, choiceOf(toSend, sends));
                            block.applyParam(OneEarMix::Hear, choiceOf(toHear, hears));
                        } else {
                            ASSERT_TRUE(block.loadState(target.saveState()).has_value());
                        }

                        std::array<float, 4> before = settled;
                        for (std::uint32_t step = 0; step < 2 * kFallSamples / cycle; ++step) {
                            runSilence(block, cycle, cycle);
                            const std::array<float, 4> now = block.weights();
                            for (std::size_t weight = 0; weight < now.size(); ++weight) {
                                if (now[weight] > before[weight] && before[weight] > 0.0f) {
                                    EXPECT_LE(20.0f * std::log10(now[weight] / before[weight]), mostRiseDb)
                                        << fromSend << ' ' << fromHear << " to " << toSend << ' ' << toHear;
                                }
                                if (before[weight] == 0.0f) {
                                    EXPECT_LE(now[weight], dbToGain(OneEarMix::kRiseFromDb) * 1.01f)
                                        << "a weight from nothing starts at the floor";
                                }
                            }
                            before = now;
                        }
                        // Everything going down has gone down by now.
                        runSilence(block, kFallSamples, cycle);
                        const std::array<float, 4> after = block.weights();
                        runSilence(block, 2048, 2048);
                        for (std::size_t weight = 0; weight < after.size(); ++weight) {
                            EXPECT_GE(block.weights()[weight], after[weight]) << "still falling after 10 ms";
                        }
                    }
                }
            }
        }
    }
}

TEST(OneEarMix, AStartAndASwitchOnBringTheOtherSideInToo)
{
    OneEarMix block;
    set(block, "sendTo", "both");
    ASSERT_TRUE(block.prepare(spec()).has_value());
    const float k = QuadratureFold::kFoldGain;
    const float floor = dbToGain(OneEarMix::kRiseFromDb);
    EXPECT_EQ(block.weights()[0], k) << "a side already there stays";
    EXPECT_NEAR(block.weights()[1], floor, 1e-6f);
    EXPECT_NEAR(block.weights()[2], floor, 1e-6f);
    EXPECT_EQ(block.weights()[3], k);

    runSilence(block, kSettled);
    EXPECT_EQ(block.weights()[1], k);
    block.switchedOn();
    EXPECT_NEAR(block.weights()[1], floor, 1e-6f);
    EXPECT_NEAR(block.weights()[2], floor, 1e-6f);
    EXPECT_EQ(block.weights()[0], k);
    runSilence(block, kSettled);
    EXPECT_EQ(block.weights()[1], k);
    block.reset();
    EXPECT_NEAR(block.weights()[2], floor, 1e-6f);
}

TEST(OneEarMix, ANewOutputBringsTheFarSideInAgain)
{
    OneEarMix block;
    ASSERT_TRUE(block.prepare(spec()).has_value());
    set(block, "sendTo", "left");
    runSilence(block, kSettled);
    const float k = QuadratureFold::kFoldGain;
    ASSERT_EQ(block.weights()[1], k);

    block.applyParam(OneEarMix::NewOutput, 1.0f);
    const float floor = dbToGain(OneEarMix::kRiseFromDb);
    EXPECT_NEAR(block.weights()[1], floor, 1e-6f);
    EXPECT_EQ(block.weights()[0], k) << "the near side goes on playing";
    runSilence(block, kSecond);
    EXPECT_NEAR(gainToDb(block.weights()[1]), OneEarMix::kRiseFromDb + OneEarMix::kRiseDbPerSecond, 0.1f);
    runSilence(block, kSettled);
    EXPECT_EQ(block.weights()[1], k);

    OneEarMix plain;
    ASSERT_TRUE(plain.prepare(spec()).has_value());
    plain.applyParam(OneEarMix::NewOutput, 1.0f);
    EXPECT_EQ(plain.weights(), (std::array{1.0f, 0.0f, 0.0f, 1.0f})) << "nothing to bring in";
}

TEST(OneEarMix, SwitchingOnWithNothingToBringInChangesNothing)
{
    OneEarMix block;
    ASSERT_TRUE(block.prepare(spec()).has_value());
    block.switchedOn();
    EXPECT_EQ(block.weights(), (std::array{1.0f, 0.0f, 0.0f, 1.0f}));
}

TEST(OneEarMix, ReportsASideArrivingAndNotATrimRising)
{
    rt::BlockMeter meter;
    OneEarMix block;
    set(block, "sendTo", "left");
    ASSERT_TRUE(block.prepare(spec(&meter)).has_value());
    runSilence(block, 64);
    EXPECT_EQ(meter.value(OneEarMix::kStateValue), OneEarMix::kArriving);
    runSilence(block, kSettled);
    EXPECT_EQ(meter.value(OneEarMix::kStateValue), OneEarMix::kSettled);

    set(block, "hear", "mono");
    runSilence(block, kSecond);
    set(block, "hear", "fold");
    runSilence(block, 64);
    EXPECT_EQ(meter.value(OneEarMix::kStateValue), OneEarMix::kSettled);
}

TEST(OneEarMix, NoChangeMakesAClick)
{
    constexpr std::array sends{"none", "left", "right", "both"};
    constexpr std::array hears{"fold", "mono"};
    const float amplitude = 0.5f;
    // A 500 Hz tone's own step at this level, and room for the crossfade.
    const float allowed = amplitude * 2.0f * std::numbers::pi_v<float> * 500.0f / kSecond * 1.6f;
    for (const char *fromSend : sends) {
        for (const char *toSend : sends) {
            for (const char *hear : hears) {
                OneEarMix block;
                set(block, "sendTo", fromSend);
                set(block, "hear", hear);
                BlockRunner runner(block, 2, 2);
                ASSERT_TRUE(runner.prepare().has_value());
                const Signal tone = sine(kSettled, 500.0, kTestSampleRate, amplitude);
                (void)runner.run({tone, tone}, CycleShape::fixed(1024));
                set(block, "sendTo", toSend);
                const Signal more =
                    sine(kSecond / 4, 500.0, kTestSampleRate, amplitude, 2.0 * std::numbers::pi * 500.0 * 5.0);
                const MultiSignal output = runner.run({more, more}, CycleShape::fixed(1024));
                EXPECT_LT(maxSampleStep(output[0]), allowed) << fromSend << " to " << toSend << ' ' << hear;
                EXPECT_LT(maxSampleStep(output[1]), allowed) << fromSend << " to " << toSend << ' ' << hear;
            }
        }
    }
}

TEST(OneEarMix, WithVolumeAfterItNoSamplePassesTheCeilingOnAnyJump)
{
    constexpr std::array sends{"none", "left", "right", "both"};
    const float ceiling = dbToGain(LookaheadLimiter::kDefaultCeilingDb);
    Signal square(kSecond);
    for (std::size_t frame = 0; frame < square.size(); ++frame) {
        square[frame] = frame / 240 % 2 == 0 ? 1.0f : -1.0f;
    }
    const Signal noise = whiteNoise(kSecond, 1.0f, 8);
    for (const char *from : sends) {
        for (const char *to : sends) {
            for (const Signal *input : std::array<const Signal *, 2>{&square, &noise}) {
                OneEarMix block;
                set(block, "sendTo", from);
                BlockRunner runner(block, 2, 2);
                ASSERT_TRUE(runner.prepare().has_value());
                Volume volume;
                BlockRunner after(volume, 2, 2);
                ASSERT_TRUE(after.prepare().has_value());
                (void)after.run(runner.run({*input, *input}));
                set(block, "sendTo", to);
                const MultiSignal output = after.run(runner.run({*input, *input}));
                EXPECT_LE(peak(output[0]), ceiling * 1.0001f) << from << " to " << to;
                EXPECT_LE(peak(output[1]), ceiling * 1.0001f) << from << " to " << to;
            }
        }
    }
}

/// What Match Levels publishes for \a left and \a right, and the same
/// measured here without the block.
struct Measured {
    float fold;
    float mono;
    float offlineFold;
    float offlineMono;
};

[[nodiscard]] Measured measure(const Signal &left, const Signal &right, const char *hear = "fold", float lowered = 0.0f)
{
    rt::BlockMeter meter;
    OneEarMix block;
    EXPECT_TRUE(block.setParam("sendTo", ParamValue::ofEnum("left")).has_value());
    EXPECT_TRUE(block.setParam("hear", ParamValue::ofEnum(hear)).has_value());
    EXPECT_TRUE(block.setParam("lowerDb", ParamValue::ofFloat(lowered)).has_value());
    BlockRunner runner(block, 2, 2);
    runner.setMeter(&meter);
    EXPECT_TRUE(runner.prepare().has_value());
    block.applyParam(OneEarMix::MatchLevels, 1.0f);
    (void)runner.run({left, right});
    EXPECT_EQ(meter.value(OneEarMix::kReadingValue), OneEarMix::kRead);

    QuadratureFold fold;
    fold.prepare(kTestSampleRate);
    GatedLoudness foldArm;
    GatedLoudness monoArm;
    foldArm.prepare(kTestSampleRate);
    monoArm.prepare(kTestSampleRate);
    const auto window = static_cast<std::size_t>(OneEarMix::kReadingMs * 1.0e-3f * kTestSampleRate);
    for (std::size_t frame = 0; frame < window; ++frame) {
        const QuadratureFold::Frame out = fold.process(left[frame], right[frame]);
        (void)foldArm.push(QuadratureFold::kFoldGain * (out.a + out.b), 0.0f);
        (void)monoArm.push(0.5f * (left[frame] + right[frame]), 0.0f);
    }
    return {meter.value(OneEarMix::kFoldLoudnessValue),
            meter.value(OneEarMix::kMonoLoudnessValue),
            foldArm.loudness(),
            monoArm.loudness()};
}

TEST(OneEarMix, MatchLevelsReadsWhatTheTwoMixesMeasureOffline)
{
    const std::size_t frames = kSecond * 4;
    const Signal mid = whiteNoise(frames, 0.25f, 11);
    const Signal side = whiteNoise(frames, 0.25f, 12);
    struct Row {
        float sideDb;
        float gapDb;
        float within;
    };
    for (const Row row : {Row{-6.0f, 1.23f, 0.25f}, Row{-20.0f, 0.46f, 0.3f}, Row{0.0f, 3.01f, 0.25f}}) {
        Signal left(frames);
        Signal right(frames);
        const float gain = dbToGain(row.sideDb);
        for (std::size_t frame = 0; frame < frames; ++frame) {
            left[frame] = mid[frame] + gain * side[frame];
            right[frame] = mid[frame] - gain * side[frame];
        }
        const Measured read = measure(left, right);
        EXPECT_NEAR(read.fold, read.offlineFold, 0.2f);
        EXPECT_NEAR(read.mono, read.offlineMono, 0.2f);
        EXPECT_NEAR(read.fold - read.mono, row.gapDb, row.within) << row.sideDb;
    }
}

TEST(OneEarMix, ASoundOnOneSideReadsThreeDecibelsApart)
{
    const Signal noise = whiteNoise(kSecond * 4, 0.5f, 13);
    const Measured read = measure(noise, silence(noise.size()));
    EXPECT_NEAR(read.fold - read.mono, 3.01f, 0.25f);
}

TEST(OneEarMix, AReadingIsTheSameWhicheverMixIsPlayingAndHoweverLowered)
{
    const Signal left = whiteNoise(kSecond * 4, 0.5f, 14);
    const Signal right = whiteNoise(kSecond * 4, 0.5f, 15);
    const Measured fold = measure(left, right);
    const Measured mono = measure(left, right, "mono");
    const Measured lowered = measure(left, right, "fold", 3.0f);
    EXPECT_NEAR(mono.fold, fold.fold, 0.01f);
    EXPECT_NEAR(mono.mono, fold.mono, 0.01f);
    EXPECT_NEAR(lowered.fold, fold.fold, 0.01f);
}

TEST(OneEarMix, MatchLevelsOnSilenceReadsSilence)
{
    const Measured read = measure(silence(kSecond * 4), silence(kSecond * 4));
    EXPECT_EQ(read.fold, kSilentLufs);
    EXPECT_EQ(read.mono, kSilentLufs);
}

TEST(OneEarMix, AReadingSetsNoGainAndStopsByItself)
{
    rt::BlockMeter meter;
    OneEarMix block;
    set(block, "sendTo", "left");
    set(block, "hear", "mono");
    ASSERT_TRUE(block.prepare(spec(&meter)).has_value());
    runSilence(block, kSettled);
    const std::array<float, 4> before = block.weights();
    EXPECT_FALSE(block.filtersRunning());

    block.applyParam(OneEarMix::MatchLevels, 1.0f);
    runSilence(block, 64);
    EXPECT_TRUE(block.filtersRunning()) << "plain mono needs them for a reading";
    EXPECT_EQ(meter.value(OneEarMix::kReadingValue), OneEarMix::kReading);

    runSilence(block, kSecond * 3 + kSecond / 10);
    EXPECT_EQ(meter.value(OneEarMix::kReadingValue), OneEarMix::kRead);
    EXPECT_FALSE(block.filtersRunning()) << "no detector runs outside a reading";
    EXPECT_EQ(block.weights(), before);
}

TEST(OneEarMix, ALoadedOrSavedStateStartsNoReading)
{
    rt::BlockMeter meter;
    OneEarMix block;
    ASSERT_TRUE(block.prepare(spec(&meter)).has_value());
    ASSERT_TRUE(block.setParam("matchLevels", ParamValue::ofBool(true)).has_value());
    OneEarMix opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    ASSERT_TRUE(opened.prepare(spec(&meter)).has_value());
    runSilence(block, 64);
    runSilence(opened, 64);
    EXPECT_EQ(meter.value(OneEarMix::kReadingValue), OneEarMix::kNotRead);
}

TEST(OneEarMix, TheReadingIsAPressAndNoSetting)
{
    const OneEarMix block;
    const auto params = block.params();
    const ParamDescriptor &match = params[OneEarMix::MatchLevels];
    EXPECT_EQ(match.id, "matchLevels");
    EXPECT_TRUE(match.flags.momentary);
    EXPECT_TRUE(match.flags.hidden);
    EXPECT_FALSE(match.flags.inPresets);
    for (std::size_t index = 0; index < params.size(); ++index) {
        EXPECT_EQ(params[index].flags.momentary, index == OneEarMix::MatchLevels || index == OneEarMix::NewOutput)
            << params[index].id;
        EXPECT_EQ(params[index].flags.pressedByNewOutput, index == OneEarMix::NewOutput) << params[index].id;
    }
}

TEST(OneEarMix, ASavedStateComesBack)
{
    OneEarMix block;
    set(block, "sendTo", "right");
    set(block, "hear", "mono");
    lower(block, 1.5f);
    OneEarMix opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    EXPECT_EQ(opened.param("sendTo").text(), "right");
    EXPECT_EQ(opened.param("hear").text(), "mono");
    EXPECT_FLOAT_EQ(static_cast<float>(*opened.param("lowerDb").asNumber()), 1.5f);
}

TEST(OneEarMix, TheGlideTakesTheSameTimeAtOtherRates)
{
    for (const double rate : {44100.0, 96000.0, 192000.0}) {
        OneEarMix block;
        set(block, "sendTo", "left");
        BlockRunner runner(block, 2, 2, rate, 2048);
        ASSERT_TRUE(runner.prepare().has_value());
        const auto frames = static_cast<std::size_t>(rate * 4.5);
        (void)runner.run({silence(frames), silence(frames)}, CycleShape::fixed(2048));
        EXPECT_LT(block.weights()[1], QuadratureFold::kFoldGain * dbToGain(-2.0f)) << rate;
        (void)runner.run({silence(frames / 4), silence(frames / 4)}, CycleShape::fixed(2048));
        EXPECT_EQ(block.weights()[1], QuadratureFold::kFoldGain) << rate;
    }
}

TEST(OneEarMix, CostsLessThanAPointNoughtFivePerCentOfACore)
{
    OneEarMix block;
    set(block, "sendTo", "both");
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal input{whiteNoise(kSecond * 60, 0.5f, 1), whiteNoise(kSecond * 60, 0.5f, 2)};
    const auto started = std::chrono::steady_clock::now();
    (void)runner.run(input, CycleShape::fixed(256));
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const double share = took / 60.0;
    std::cerr << "one ear mix: a minute of stereo in " << took << " s, " << share * 100.0 << " % of a core\n";
#ifdef NDEBUG
    EXPECT_LT(share, 0.0005);
#endif
}
