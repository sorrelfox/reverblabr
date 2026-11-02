// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Voice Compressor: the law is its table and the sound follows it at every
// quantum, nothing passes the ceiling, no change of a setting clicks, the attack
// and the release are what they say on tone bursts, and the gate eases, holds,
// stops at its range and is exactly off at the bottom.

#include "allocationtrap.h"
#include "blocks/voicecompressor.h"
#include "blocksizeinvariance.h"
#include "dsp/decibel.h"
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
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
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
constexpr std::size_t kMilli = 48;
constexpr std::size_t kDelay = 84;
constexpr float kOff = VoiceCompressor::kGateOffDb;
constexpr std::array<std::uint32_t, 5> kQuanta{1, 64, 256, 1024, 8192};

struct Setting {
    float compression;
    float ceiling;
    float gate;
};

void set(VoiceCompressor &block, const Setting &to)
{
    ASSERT_TRUE(block.setParam("compression", ParamValue::ofFloat(to.compression)).has_value());
    ASSERT_TRUE(block.setParam("ceiling", ParamValue::ofFloat(to.ceiling)).has_value());
    ASSERT_TRUE(block.setParam("gate", ParamValue::ofFloat(to.gate)).has_value());
}

struct Rig {
    VoiceCompressor block;
    rt::BlockMeter meter;
    std::unique_ptr<BlockRunner> runner;

    explicit Rig(const Setting &setting, double rate = kTestSampleRate)
    {
        set(block, setting);
        runner = std::make_unique<BlockRunner>(block, 2, 2, rate, 8192);
        runner->setMeter(&meter);
        EXPECT_TRUE(runner->prepare().has_value());
    }

    Signal feed(const Signal &signal, std::uint32_t quantum = 256)
    {
        return runner->run({signal, signal}, CycleShape::fixed(quantum))[0];
    }
};

[[nodiscard]] Signal tone(std::size_t frames, float levelDb, double hz = 1000.0, double rate = kTestSampleRate)
{
    return sine(frames, hz, rate, dbToGain(levelDb));
}

/// A tone at one level, then at another, whole cycles and no jump in phase.
[[nodiscard]] Signal
stepped(std::size_t before, float fromDb, std::size_t after, float toDb, double rate = kTestSampleRate)
{
    Signal signal = sine(before + after, 1000.0, rate, 1.0f);
    for (std::size_t n = 0; n < signal.size(); ++n) {
        signal[n] *= dbToGain(n < before ? fromDb : toDb);
    }
    return signal;
}

/// The peak of the 2 ms from \a at, in dB.
[[nodiscard]] float levelAt(const Signal &signal, std::size_t at, std::size_t window = 2 * kMilli)
{
    return peakDb(std::span<const float>(signal).subspan(at, window));
}

/// The first frame from \a from on where the 2 ms level is within \a nearDb of \a goalDb.
[[nodiscard]] std::size_t firstNear(const Signal &signal, std::size_t from, float goalDb, float nearDb)
{
    for (std::size_t at = from; at + 2 * kMilli < signal.size(); at += kMilli / 4) {
        if (std::fabs(levelAt(signal, at) - goalDb) < nearDb) {
            return at;
        }
    }
    return signal.size();
}

constexpr std::array kLevels{0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f, -60.0f};

struct Row {
    Setting setting;
    float ratio;
    float makeup;
    std::array<float, kLevels.size()> out;
};

/// Worked out apart from this code, from the design's formulas.
const std::array kTable{
    Row{{0.0f, 0.0f, kOff}, 2.0f, 0.9375f, {0.0f, -5.062f, -11.062f, -17.062f, -23.062f, -35.062f, -47.062f, -59.062f}},
    Row{{-12.0f, -6.0f, kOff}, 3.333f, 9.45f, {-6.0f, -6.75f, -9.6f, -14.55f, -20.55f, -32.55f, -44.55f, -56.55f}},
    Row{{-12.0f, 0.0f, kOff}, 3.333f, 10.5f, {0.0f, 0.0f, -2.55f, -7.5f, -13.5f, -25.5f, -37.5f, -49.5f}},
    Row{{-24.0f, -6.0f, kOff}, 4.667f, 21.214f, {-6.0f, -6.0f, -6.214f, -7.5f, -9.964f, -20.786f, -32.786f, -44.786f}},
    Row{{-36.0f, -12.0f, kOff}, 6.0f, 30.0f, {-12.0f, -13.0f, -14.0f, -15.0f, -16.0f, -19.25f, -30.0f, -42.0f}},
    Row{{-36.0f, 0.0f, kOff}, 6.0f, 37.5f, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -10.5f, -22.5f}},
    Row{{-18.0f, -6.0f, -50.0f},
        4.0f,
        15.1875f,
        {-6.0f, -6.0f, -7.312f, -9.938f, -14.812f, -26.812f, -39.479f, -60.812f}},
};

} // namespace

TEST(VoiceCompressor, DelaysByTheLimiterAddsAndSaysSo)
{
    Rig rig({-12.0f, -6.0f, kOff});
    EXPECT_EQ(rig.block.info().latencySamples, kDelay);
    // Its ceiling is a slider that goes up to full scale, and a limit that a
    // drag undoes is no limit a later block can lean on.
    EXPECT_EQ(rig.block.info().gainReach, GainReach::Adds);
    EXPECT_EQ(rig.block.info().category, BlockCategory::Effects);
}

TEST(VoiceCompressor, TheDelayIsMeasured)
{
    Rig rig({0.0f, -30.0f, kOff});
    const Signal in = tone(kSecond / 4, -20.0f, 997.0);
    const Signal out = rig.feed(in);
    const std::optional<int> lag = latencySamples(in, out, 200);
    ASSERT_TRUE(lag.has_value());
    EXPECT_EQ(*lag, static_cast<int>(kDelay));
}

TEST(VoiceCompressor, TheLawIsTheTable)
{
    for (const Row &row : kTable) {
        EXPECT_NEAR(VoiceCompressor::ratioAt(row.setting.compression), row.ratio, 0.001f);
        EXPECT_NEAR(VoiceCompressor::makeupDb(row.setting.compression, row.setting.ceiling), row.makeup, 0.001f);
        for (std::size_t at = 0; at < kLevels.size(); ++at) {
            EXPECT_NEAR(
                VoiceCompressor::outputDb(kLevels[at], row.setting.compression, row.setting.ceiling, row.setting.gate),
                row.out[at],
                0.001f)
                << row.setting.compression << " at " << kLevels[at];
        }
    }
}

namespace
{

void expectFollowsTheTable(const Row &row, std::uint32_t quantum)
{
    for (std::size_t at = 0; at < kLevels.size(); ++at) {
        Rig rig(row.setting);
        const Signal out = rig.feed(tone(kSecond, kLevels[at]), quantum);
        const float heard = levelAt(out, out.size() - 20 * kMilli, 10 * kMilli);
        // A sine's mean square ripples at twice its frequency, which a law
        // read on a steady tone cannot quite leave out.
        EXPECT_NEAR(heard, row.out[at], 0.25f) << row.setting.compression << " at " << kLevels[at] << ", " << quantum;
    }
}

} // namespace

TEST(VoiceCompressor, TheSoundFollowsTheTable)
{
    for (const Row &row : kTable) {
        expectFollowsTheTable(row, 256);
    }
    for (const std::uint32_t quantum : kQuanta) {
        expectFollowsTheTable(kTable[1], quantum);
        expectFollowsTheTable(kTable[6], quantum);
    }
}

TEST(VoiceCompressor, OnAddItLiftsAMiddlingSoundAndKeepsTheCeiling)
{
    VoiceCompressor block;
    EXPECT_EQ(*block.param("compression").asFloat(), VoiceCompressor::kDefaultCompressionDb);
    EXPECT_EQ(*block.param("gate").asFloat(), kOff);
    EXPECT_EQ(*block.param("ceiling").asFloat(), VoiceCompressor::kDefaultCeilingDb);

    Rig rig({VoiceCompressor::kDefaultCompressionDb, VoiceCompressor::kDefaultCeilingDb, kOff});
    const Signal out = rig.feed(tone(kSecond, -18.0f));
    EXPECT_NEAR(levelAt(out, out.size() - 20 * kMilli) + 18.0f, 3.45f, 0.25f);
}

TEST(VoiceCompressor, LoweringCompressionTakesMoreOffAndBringsTheSoundUp)
{
    float lastOut = -120.0f;
    float lastTaken = -1.0f;
    for (const float compression : {0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -30.0f, -36.0f}) {
        Rig rig({compression, -12.0f, kOff});
        const Signal out = rig.feed(tone(kSecond, -30.0f));
        const float heard = levelAt(out, out.size() - 20 * kMilli);
        const float taken = rig.meter.value(VoiceCompressor::kCompressedValue);
        EXPECT_GT(heard, lastOut) << compression;
        EXPECT_GE(taken, lastTaken) << compression;
        lastOut = heard;
        lastTaken = taken;
    }
}

TEST(VoiceCompressor, LoweringTheCeilingNearTheTopLimitsLess)
{
    // A steady tone comes out at the ceiling at each, so only the work differs.
    float last = 100.0f;
    for (const float ceiling : {0.0f, -3.0f, -6.0f, -9.0f, -12.0f}) {
        Rig rig({-24.0f, ceiling, kOff});
        (void)rig.feed(tone(kSecond, -10.0f));
        const float limited = rig.meter.value(VoiceCompressor::kLimitedValue);
        EXPECT_LT(limited, last) << ceiling;
        last = limited;
    }
    EXPECT_LT(last, 0.05f) << "below the push the limiter has nothing steady to do";
}

TEST(VoiceCompressor, TheCeilingMovesTheWholeSoundBelowThePush)
{
    Rig high({-12.0f, -12.0f, kOff});
    Rig low({-12.0f, -24.0f, kOff});
    const Signal in = tone(kSecond, -30.0f);
    const Signal a = high.feed(in);
    const Signal b = low.feed(in);
    EXPECT_NEAR(levelAt(a, a.size() - 20 * kMilli) - levelAt(b, b.size() - 20 * kMilli), 12.0f, 0.02f);
}

namespace
{

[[nodiscard]] Signal square(std::size_t frames, std::size_t period, float amplitude)
{
    Signal signal(frames);
    for (std::size_t n = 0; n < frames; ++n) {
        signal[n] = (n / period) % 2 == 0 ? amplitude : -amplitude;
    }
    return signal;
}

/// Past this the ceiling has finished its glide.
constexpr std::size_t kGlide = 25 * kMilli;

} // namespace

TEST(VoiceCompressor, WorstCaseNothingPassesTheCeiling)
{
    const std::array<Setting, 6> settings{Setting{0.0f, 0.0f, kOff},
                                          Setting{-36.0f, 0.0f, kOff},
                                          Setting{-36.0f, -36.0f, kOff},
                                          Setting{-36.0f, -6.0f, -20.0f},
                                          Setting{-12.0f, -6.0f, 0.0f},
                                          Setting{0.0f, -18.0f, kOff}};
    // Over full scale on purpose, and with quiet before it: the gain is at its
    // highest when the loud part arrives.
    std::array<Signal, 4> signals{whiteNoise(kSecond / 2, 1.0f, 8),
                                  square(kSecond / 2, 50, 2.0f),
                                  stepped(kSecond / 4, -70.0f, kSecond / 4, 6.0f),
                                  impulse(kSecond / 2, kSecond / 4, 4.0f)};
    for (const Setting &setting : settings) {
        for (const Signal &signal : signals) {
            for (const std::uint32_t quantum : {1U, 8192U}) {
                Rig rig(setting);
                const Signal out = rig.feed(signal, quantum);
                EXPECT_LE(peakDb(out), setting.ceiling + 0.01f) << setting.compression << ", " << setting.ceiling;
            }
        }
    }
}

TEST(VoiceCompressor, TheCeilingHoldsThroughEveryChangeOfSetting)
{
    const std::array<Setting, 5> settings{Setting{0.0f, 0.0f, kOff},
                                          Setting{-36.0f, 0.0f, kOff},
                                          Setting{-36.0f, -36.0f, kOff},
                                          Setting{-6.0f, -12.0f, -30.0f},
                                          Setting{-24.0f, -3.0f, 0.0f}};
    const Signal noise = whiteNoise(kSecond / 2, 1.0f, 9);
    for (const Setting &from : settings) {
        for (const Setting &to : settings) {
            Rig rig(from);
            (void)rig.feed(noise);
            set(rig.block, to);
            const Signal out = rig.feed(noise);
            const float highest = std::max(from.ceiling, to.ceiling);
            EXPECT_LE(peakDb(out), highest + 0.01f);
            EXPECT_LE(peakDb(std::span<const float>(out).subspan(kGlide)), to.ceiling + 0.01f)
                << from.ceiling << " to " << to.ceiling;
        }
    }
}

TEST(VoiceCompressor, NoChangeOfASettingClicks)
{
    // A low tone, so a click stands well above what the wave does by itself.
    constexpr double kHz = 200.0;
    const std::array<Setting, 6> settings{Setting{0.0f, -6.0f, kOff},
                                          Setting{-36.0f, -6.0f, kOff},
                                          Setting{-12.0f, 0.0f, kOff},
                                          Setting{-12.0f, -36.0f, kOff},
                                          Setting{-12.0f, -6.0f, 0.0f},
                                          Setting{-24.0f, -24.0f, -10.0f}};
    const float slope = 2.0f * std::numbers::pi_v<float> * static_cast<float>(kHz) / static_cast<float>(kSecond);
    for (const float level : {-40.0f, -18.0f, 0.0f}) {
        const Signal in = tone(kSecond, level, kHz);
        for (const Setting &from : settings) {
            for (const Setting &to : settings) {
                Rig rig(from);
                (void)rig.feed(in);
                set(rig.block, to);
                const Signal out = rig.feed(in);
                const float biggest = std::max(from.ceiling, to.ceiling);
                // A sine of the highest level it can reach, twice over: the
                // ramps add to the wave's own slope, and nothing may jump.
                EXPECT_LE(maxSampleStep(out), dbToGain(biggest) * slope * 2.0f)
                    << level << ": " << from.compression << "," << from.ceiling << "," << from.gate << " to "
                    << to.compression << "," << to.ceiling << "," << to.gate;
            }
        }
    }
}

TEST(VoiceCompressor, TheAttackIsWhatItSaysOnAToneBurst)
{
    // Read from what the block publishes, a millisecond at a time: on the sound
    // the limiter would stand in front of it, as the make-up meets the burst
    // before the gain has come down.
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        Rig rig({-24.0f, -24.0f, kOff}, rate);
        const auto perMs = static_cast<std::uint32_t>(std::round(rate / 1000.0));
        const auto seconds = static_cast<std::size_t>(rate);
        (void)rig.runner->run(MultiSignal(2, tone(seconds, -60.0f, 1000.0, rate)), CycleShape::fixed(256));
        const Signal loud = sine(perMs * 200, 1000.0, rate, dbToGain(-6.0f));
        std::vector<float> taken;
        for (std::size_t at = 0; at < loud.size(); at += perMs) {
            const Signal piece(loud.begin() + static_cast<std::ptrdiff_t>(at),
                               loud.begin() + static_cast<std::ptrdiff_t>(at + perMs));
            (void)rig.runner->run({piece, piece}, CycleShape::fixed(perMs));
            taken.push_back(rig.meter.value(VoiceCompressor::kCompressedValue));
        }
        const float settled = taken.back();
        ASSERT_GT(settled, 8.0f);
        const auto reached = std::ranges::find_if(taken, [settled](float db) { return db >= settled * 0.632f; });
        const auto ms = static_cast<std::size_t>(reached - taken.begin()) + 1;
        std::cerr << "voice compressor attack at " << rate << ": " << ms << " ms\n";
        // The attack's 5 ms and the level's 8 ms, in a row. A tone rises in the
        // follower well before its mean settles, so the sum is the most.
        EXPECT_GE(ms, 3U) << rate;
        EXPECT_LE(ms, 13U) << rate;
    }
}

namespace
{

/// How long the sound takes to come back within 1 dB of its steady level after
/// \a loudMs of a loud tone, in ms.
[[nodiscard]] std::size_t releaseAfter(std::size_t loudMs)
{
    Rig rig({-24.0f, -24.0f, kOff});
    constexpr std::size_t kLater = 3 * kSecond;
    Signal in = stepped(kSecond, -40.0f, loudMs * kMilli + kLater, -40.0f);
    for (std::size_t n = kSecond; n < kSecond + loudMs * kMilli; ++n) {
        in[n] *= dbToGain(34.0f);
    }
    const Signal out = rig.feed(in);
    const float steady = levelAt(out, out.size() - 10 * kMilli);
    const std::size_t end = kSecond + loudMs * kMilli + kDelay;
    return (firstNear(out, end, steady, 1.0f) - end) / kMilli;
}

} // namespace

TEST(VoiceCompressor, ASyllableLetsGoQuicklyAndALongLineSlowly)
{
    const std::size_t syllable = releaseAfter(40);
    const std::size_t line = releaseAfter(3000);
    std::cerr << "voice compressor release: " << syllable << " ms after a syllable, " << line
              << " ms after a long line\n";
    EXPECT_GE(syllable, 80U);
    EXPECT_LE(syllable, 400U);
    EXPECT_GE(line, 800U);
    EXPECT_LE(line, 2500U);
    EXPECT_GT(line, syllable * 3);
}

TEST(VoiceCompressor, TheGateFollowsItsCurveAndStopsAtItsRange)
{
    const std::array cases{std::pair{-44.0f, 0.0f}, // above the knee
                           std::pair{-50.0f, -1.5f}, // on it
                           std::pair{-60.0f, -10.0f}, // a decibel under, two down
                           std::pair{-90.0f, -24.0f}}; // its range
    for (const auto &[level, gated] : cases) {
        EXPECT_NEAR(VoiceCompressor::gateDb(level, -50.0f), gated, 0.001f) << level;
        Rig rig({0.0f, 0.0f, -50.0f});
        (void)rig.feed(tone(kSecond, level));
        EXPECT_NEAR(rig.meter.value(VoiceCompressor::kGatedValue), -gated, 0.2f) << level;
    }
}

TEST(VoiceCompressor, TheGateAtTheBottomIsExactlyOff)
{
    EXPECT_EQ(VoiceCompressor::gateDb(-120.0f, kOff), 0.0f);
    Rig rig({0.0f, 0.0f, kOff});
    (void)rig.feed(tone(kSecond, -110.0f));
    EXPECT_EQ(rig.meter.value(VoiceCompressor::kGatedValue), 0.0f);
}

TEST(VoiceCompressor, TheCompressorReadsTheLevelAfterTheGate)
{
    // Under the Gate, this tone would be compressed by its own level if the
    // compressor came first.
    Rig gated({-30.0f, -12.0f, -10.0f});
    Rig open({-30.0f, -12.0f, kOff});
    const Signal in = tone(kSecond, -32.0f);
    (void)gated.feed(in);
    (void)open.feed(in);
    EXPECT_GT(gated.meter.value(VoiceCompressor::kGatedValue), 20.0f);
    EXPECT_LT(gated.meter.value(VoiceCompressor::kCompressedValue),
              open.meter.value(VoiceCompressor::kCompressedValue));
}

TEST(VoiceCompressor, TheGateOpensQuicklyHoldsAndClosesSlowly)
{
    Rig rig({0.0f, 0.0f, -30.0f});
    constexpr std::size_t kSpell = kSecond;
    Signal in = stepped(kSpell, -70.0f, 2 * kSpell, -10.0f);
    for (std::size_t n = 2 * kSpell; n < in.size(); ++n) {
        in[n] *= dbToGain(-60.0f);
    }
    const Signal out = rig.feed(in);

    // Open within 20 ms of the sound arriving.
    const float open = levelAt(out, 2 * kSpell - 20 * kMilli);
    EXPECT_NEAR(levelAt(out, kSpell + kDelay + 20 * kMilli), open, 1.0f);

    // Past the fall: first the follower, about 20 ms, then the hold.
    const std::size_t fall = 2 * kSpell + kDelay;
    const float heldFor = levelAt(out, fall + 30 * kMilli) + 70.0f;
    EXPECT_GT(heldFor, -3.0f) << "the hold ended early";
    // Then it closes over about a tenth of a second, never at once.
    EXPECT_LT(levelAt(out, fall + 500 * kMilli) + 70.0f, -20.0f);
    float last = 0.0f;
    for (std::size_t ms = 30; ms < 300; ms += 10) {
        const float now = levelAt(out, fall + ms * kMilli) + 70.0f;
        EXPECT_GT(now, last - 6.0f) << ms;
        last = now;
    }
}

TEST(VoiceCompressor, TheGateWaitsBeforeItCloses)
{
    Rig rig({0.0f, 0.0f, -30.0f});
    (void)rig.feed(tone(kSecond, -10.0f));
    const Signal faint = tone(kMilli, -70.0f);
    std::vector<float> gated;
    for (std::size_t ms = 0; ms < 200; ++ms) {
        (void)rig.runner->run({faint, faint}, CycleShape::fixed(kMilli));
        gated.push_back(rig.meter.value(VoiceCompressor::kGatedValue));
    }
    // The level takes about 25 ms to fall to the knee, and the hold is the rest.
    EXPECT_LT(gated[45], 0.25f);
    EXPECT_GT(gated[120], 3.0f);
    EXPECT_GT(gated[199], gated[120]);
}

TEST(VoiceCompressor, ASoundOnTheGateDoesNotChatter)
{
    Rig rig({0.0f, 0.0f, -40.0f});
    const Signal out = rig.feed(tone(2 * kSecond, -40.0f, 300.0));
    float lowest = 0.0f;
    float highest = -200.0f;
    for (std::size_t at = kSecond; at + 20 * kMilli < out.size(); at += 20 * kMilli) {
        const float level = levelAt(out, at, 10 * kMilli);
        lowest = std::min(lowest, level);
        highest = std::max(highest, level);
    }
    EXPECT_LT(highest - lowest, 0.5f);
}

TEST(VoiceCompressor, OneGainForBothSidesAndTheLouderDecides)
{
    const Signal loud = tone(kSecond, -6.0f, 500.0);
    const Signal quiet = tone(kSecond, -26.0f, 500.0);
    for (const bool leftLoud : {true, false}) {
        Rig rig({-24.0f, -12.0f, kOff});
        const MultiSignal out =
            rig.runner->run({leftLoud ? loud : quiet, leftLoud ? quiet : loud}, CycleShape::fixed(256));
        const Signal &louder = out[leftLoud ? 0 : 1];
        const Signal &quieter = out[leftLoud ? 1 : 0];
        EXPECT_NEAR(levelAt(louder, louder.size() - 20 * kMilli) - levelAt(quieter, quieter.size() - 20 * kMilli),
                    20.0f,
                    0.01f);
        EXPECT_NEAR(rig.meter.value(VoiceCompressor::kInputValue), -6.0f, 0.5f) << leftLoud;
    }
}

TEST(VoiceCompressor, TheLevelOfTwoUnrelatedSidesIsTheLevelOfOne)
{
    // The same noise on both sides against two different noises at one level.
    // A largest sample at each moment would read high: which side is larger
    // keeps changing, and the larger is above either side's mean.
    const auto meanReading = [](const Signal &left, const Signal &right) {
        Rig rig({0.0f, -30.0f, kOff});
        constexpr std::size_t kPiece = 960;
        float sum = 0.0f;
        std::size_t reads = 0;
        for (std::size_t at = 0; at + kPiece <= left.size(); at += kPiece) {
            const auto start = static_cast<std::ptrdiff_t>(at);
            const auto end = static_cast<std::ptrdiff_t>(at + kPiece);
            (void)rig.runner->run(
                {Signal(left.begin() + start, left.begin() + end), Signal(right.begin() + start, right.begin() + end)},
                CycleShape::fixed(240));
            sum += rig.meter.value(VoiceCompressor::kInputValue);
            ++reads;
        }
        return sum / static_cast<float>(reads);
    };
    const Signal one = whiteNoise(4 * kSecond, 0.1f, 11);
    const Signal other = whiteNoise(4 * kSecond, 0.1f, 12);
    EXPECT_NEAR(meanReading(one, other), meanReading(one, one), 0.3f);
}

TEST(VoiceCompressor, ItReadsBothSidesBeforeWritingEither)
{
    constexpr Setting kSetting{-24.0f, -12.0f, kOff};
    VoiceCompressor block;
    set(block, kSetting);
    VoiceCompressor apart;
    set(apart, kSetting);
    PrepareSpec spec;
    ASSERT_TRUE(block.prepare(spec).has_value());
    ASSERT_TRUE(apart.prepare(spec).has_value());

    constexpr std::uint32_t kFrames = 4096;
    const Signal leftIn = sine(kFrames, 500.0, kTestSampleRate, 0.5f);
    const Signal rightIn = sine(kFrames, 700.0, kTestSampleRate, 0.2f);
    for (int cycle = 0; cycle < 4; ++cycle) {
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
}

TEST(VoiceCompressor, SilenceStaysSilent)
{
    Rig rig({-36.0f, 0.0f, kOff});
    const Signal out = rig.feed(Signal(kSecond / 4, 0.0f));
    EXPECT_EQ(peak(out), 0.0f);
    EXPECT_FLOAT_EQ(rig.meter.value(VoiceCompressor::kInputValue), kSilenceDb);
}

TEST(VoiceCompressor, ABadSampleGoesNoFurther)
{
    Rig rig({-12.0f, -6.0f, -40.0f});
    // Long enough for the slow follower to let go of what those few samples
    // were worth at the largest the block takes.
    Signal in = tone(4 * kSecond, -18.0f);
    in[1000] = std::numeric_limits<float>::quiet_NaN();
    in[2000] = std::numeric_limits<float>::infinity();
    // Finite, and each squares past what a float holds.
    in[3000] = 1.0e20f;
    in[4000] = -3.0e38f;
    const Signal out = rig.feed(in);
    for (const float sample : out) {
        ASSERT_TRUE(std::isfinite(sample));
    }
    EXPECT_NEAR(
        levelAt(out, out.size() - 20 * kMilli), VoiceCompressor::outputDb(-18.0f, -12.0f, -6.0f, -40.0f), 0.25f);
}

TEST(VoiceCompressor, ItPublishesWhatItDidAtEveryQuantum)
{
    for (const std::uint32_t quantum : kQuanta) {
        Rig rig({-24.0f, -6.0f, kOff});
        (void)rig.feed(tone(kSecond, -6.0f), quantum);
        EXPECT_NEAR(rig.meter.value(VoiceCompressor::kInputValue), -6.0f, 0.5f) << quantum;
        EXPECT_NEAR(
            rig.meter.value(VoiceCompressor::kCompressedValue), -VoiceCompressor::compressorDb(-6.0f, -24.0f), 0.5f)
            << quantum;
        EXPECT_NEAR(rig.meter.value(VoiceCompressor::kGatedValue), 0.0f, 0.001f) << quantum;
    }
}

TEST(VoiceCompressor, ALimiterCatchIsPublishedHoweverShort)
{
    Rig rig({-36.0f, -3.0f, kOff});
    Signal in = tone(kSecond, -60.0f);
    (void)rig.feed(in);
    Signal shot(2048, 0.0f);
    shot[1024] = 1.0f;
    (void)rig.feed(shot, 2048);
    EXPECT_GT(rig.meter.value(VoiceCompressor::kLimitedValue), 1.0f);
}

TEST(VoiceCompressor, TheSoundIsTheSameWhateverTheQuantum)
{
    // Moving: a quiet stretch, a loud one, and both ways of everything.
    Signal signal = whiteNoise(2 * kSecond, 0.001f, 5);
    const Signal loud = whiteNoise(kSecond, 0.9f, 6);
    std::copy(loud.begin(), loud.begin() + kSecond / 2, signal.begin() + static_cast<std::ptrdiff_t>(kSecond));
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    options.maxBlockFrames = 8192;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] {
            auto block = std::make_unique<VoiceCompressor>();
            set(*block, {-24.0f, -3.0f, -40.0f});
            return std::unique_ptr<Block>(std::move(block));
        },
        MultiSignal{signal, signal},
        options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(VoiceCompressor, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    VoiceCompressor block;
    set(block, {-24.0f, -3.0f, -40.0f});
    BlockRunner runner(block, 2, 2, kTestSampleRate, 2048);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal noise = whiteNoise(kSecond, 0.5f, 7);
    std::size_t allocations = 1;
    (void)runner.runWatchingForAllocations(MultiSignal{noise, noise}, CycleShape::random(1, 2048, 3), allocations);
    EXPECT_EQ(allocations, 0U);
}

TEST(VoiceCompressor, ItStateRoundTrips)
{
    VoiceCompressor block;
    set(block, {-20.0f, -9.0f, -55.0f});
    VoiceCompressor other;
    ASSERT_TRUE(other.loadState(block.saveState()).has_value());
    EXPECT_EQ(*other.param("compression").asFloat(), -20.0f);
    EXPECT_EQ(*other.param("ceiling").asFloat(), -9.0f);
    EXPECT_EQ(*other.param("gate").asFloat(), -55.0f);
}

TEST(VoiceCompressor, CostsLittle)
{
    constexpr double kBudget = 0.006;
    VoiceCompressor block;
    set(block, {-24.0f, -3.0f, -40.0f});
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal input{whiteNoise(kSecond * 30, 0.1f, 1), whiteNoise(kSecond * 30, 0.1f, 2)};
    const auto started = std::chrono::steady_clock::now();
    (void)runner.run(input, CycleShape::fixed(256));
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const double share = took / 30.0;
    std::cerr << "voice compressor: " << share * 100.0 << " % of a core\n";
#ifdef NDEBUG
    EXPECT_LT(share, kBudget);
#else
    (void)share;
    (void)kBudget;
#endif
}
