// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Pitch Shift: a tone lands where the setting says; the sound comes
// out exactly as late as the block says, at every pitch and every cycle
// length; a change of pitch crosses over without a click; and SoundTouch,
// which allocates when its pitch changes, never does so on the audio thread.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/pitchshift.h"
#include "blocksizeinvariance.h"
#include "dsp/biquad.h"
#include "dsp/decibel.h"
#include "offlinerunner.h"
#include "rt/meter.h"
#include "signals.h"
#include "wavfile.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <numbers>
#include <string>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr std::size_t kSecond = 48000;

void setShift(PitchShift &block, int semitones, float cents = 0.0f)
{
    EXPECT_TRUE(block.setParam("semitones", ParamValue::ofInt(semitones)).has_value());
    EXPECT_TRUE(block.setParam("cents", ParamValue::ofFloat(cents)).has_value());
}

/// The frequency of a steady tone, from its rising zero crossings, each
/// placed between samples: good to a few hundredths of a per cent over a
/// second.
[[nodiscard]] double frequencyOf(const Signal &signal, std::size_t from, std::size_t to, double sampleRate)
{
    double first = -1.0;
    double last = -1.0;
    int crossings = 0;
    for (std::size_t frame = from + 1; frame < to; ++frame) {
        const float before = signal[frame - 1];
        const float now = signal[frame];
        if (before < 0.0f && now >= 0.0f) {
            const double at = static_cast<double>(frame - 1) + (before / (before - now));
            if (first < 0.0) {
                first = at;
            } else {
                ++crossings;
            }
            last = at;
        }
    }
    return crossings > 0 ? sampleRate * crossings / (last - first) : 0.0;
}

/// The loudest the sound gets above 10 kHz, after \a from: a click shows up
/// there, a low tone does not.
[[nodiscard]] float highestAbove10k(const Signal &signal, std::size_t from)
{
    Biquad first;
    Biquad second;
    first.setCoefficients(designBiquad({FilterShape::HighPass, 10000.0, kButterworth4Q1, 0.0}, kTestSampleRate));
    second.setCoefficients(designBiquad({FilterShape::HighPass, 10000.0, kButterworth4Q2, 0.0}, kTestSampleRate));
    float highest = 0.0f;
    for (std::size_t frame = 0; frame < signal.size(); ++frame) {
        const float high = second.process(first.process(signal[frame]));
        if (frame >= from) {
            highest = std::max(highest, std::fabs(high));
        }
    }
    return gainToDb(highest);
}

/// A few seconds of something to listen to: a voice-like buzz with two
/// formants, gliding, and a plucked note above it.
[[nodiscard]] Signal voiceMaterial(std::size_t frames)
{
    Signal out(frames, 0.0f);
    double phase = 0.0;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const double t = static_cast<double>(frame) / kTestSampleRate;
        const double hz = 150.0 + (30.0 * std::sin(2.0 * std::numbers::pi * 0.5 * t));
        phase += hz / kTestSampleRate;
        double sample = 0.0;
        for (int harmonic = 1; harmonic <= 30; ++harmonic) {
            const double f = hz * harmonic;
            const double formants = std::exp(-std::pow((f - 700.0) / 250.0, 2.0))
                + (0.6 * std::exp(-std::pow((f - 1200.0) / 300.0, 2.0))) + 0.02;
            sample += formants * std::sin(2.0 * std::numbers::pi * harmonic * phase) / harmonic;
        }
        const double pluck = std::exp(-3.0 * std::fmod(t, 1.0)) * std::sin(2.0 * std::numbers::pi * 660.0 * t);
        out[frame] = static_cast<float>((0.25 * sample) + (0.15 * pluck));
    }
    return out;
}

} // namespace

TEST(PitchShiftTest, AnOctaveUpTurns440Into880)
{
    PitchShift block;
    setShift(block, 12);
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal tone = sine(3 * kSecond, 440.0, kTestSampleRate, 0.5f);
    const MultiSignal out = runner.run(MultiSignal{tone, tone});
    const double heard = frequencyOf(out[0], kSecond, 3 * kSecond, kTestSampleRate);
    std::cerr << "[measure] 440 Hz shifted +12 semitones: " << heard << " Hz\n";
    EXPECT_NEAR(heard, 880.0, 880.0 * 0.01);
    EXPECT_NEAR(frequencyOf(out[1], kSecond, 3 * kSecond, kTestSampleRate), 880.0, 880.0 * 0.01);
}

TEST(PitchShiftTest, EveryShiftLandsWhereTheSettingSays)
{
    for (const auto &[semitones, cents] : {std::pair{-12, 0.0f},
                                           std::pair{-7, 0.0f},
                                           std::pair{-1, -50.0f},
                                           std::pair{0, 25.0f},
                                           std::pair{3, 0.0f},
                                           std::pair{7, 60.0f},
                                           std::pair{12, 100.0f}}) {
        PitchShift block;
        setShift(block, semitones, cents);
        BlockRunner runner(block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        const Signal tone = sine(3 * kSecond, 330.0, kTestSampleRate, 0.5f);
        const MultiSignal out = runner.run(MultiSignal{tone, tone});
        const double wanted = 330.0 * std::pow(2.0, (semitones + (cents / 100.0)) / 12.0);
        const double heard = frequencyOf(out[0], kSecond, 3 * kSecond, kTestSampleRate);
        EXPECT_NEAR(heard, wanted, wanted * 0.005) << semitones << " st " << cents << " ct";
    }
}

TEST(PitchShiftTest, AtNoShiftTheSoundComesOutExactlyAsLateAsReported)
{
    // Noise has one lag where it lines up with itself and nowhere else, so
    // the cross-correlation says how late the sound came out.
    PitchShift block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const std::uint32_t reported = block.info().latencySamples;
    const Signal noise = whiteNoise(2 * kSecond, 0.5f, 181);
    const MultiSignal out = runner.run(MultiSignal{noise, noise}, CycleShape::random(1, 1024, 7));
    const std::optional<int> lag = latencySamples(noise, out[0], 8192);
    ASSERT_TRUE(lag.has_value());
    std::cerr << "[measure] reported " << reported << " samples (" << reported * 1000.0 / kTestSampleRate
              << " ms), heard " << *lag << "\n";
    EXPECT_EQ(*lag, static_cast<int>(reported));
    EXPECT_GT(reported, 0U);
}

TEST(PitchShiftTest, TheLatencyCoversTheLongestWaitAtEveryPitchAndRate)
{
    // Frames that were due before SoundTouch had given them back come out as
    // silence. With the latency the block works out, there are none, at
    // either end of the range and in the middle, at every rate, whatever the
    // cycle length.
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        for (const auto &[semitones, cents] : {std::pair{-12, -100.0f},
                                               std::pair{-6, 0.0f},
                                               std::pair{0, 0.0f},
                                               std::pair{6, 0.0f},
                                               std::pair{12, 100.0f}}) {
            PitchShift block;
            setShift(block, semitones, cents);
            BlockRunner runner(block, 2, 2, rate, 2048);
            ASSERT_TRUE(runner.prepare().has_value());
            const Signal noise = whiteNoise(static_cast<std::size_t>(rate * 2.0), 0.5f, 5);
            (void)runner.run(MultiSignal{noise, noise}, CycleShape::random(1, 2048, 11));
            EXPECT_EQ(block.missedFrames(), 0U) << rate << " Hz, " << semitones << " st " << cents << " ct";
        }
    }
}

TEST(PitchShiftTest, ItSoundsTheSameWhateverTheQuantum)
{
    const Signal music = voiceMaterial(kSecond);
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] {
            auto block = std::make_unique<PitchShift>();
            setShift(*block, 5, 30.0f);
            return std::unique_ptr<Block>(std::move(block));
        },
        MultiSignal{music, music},
        options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(PitchShiftTest, AChangeOfPitchCrossesOverWithoutAClick)
{
    // A low tone shifted has nothing above 10 kHz but SoundTouch's own faint
    // seams; a jump from one pitch to another would put a click there.
    const auto run = [](bool change) {
        PitchShift block;
        setShift(block, 2);
        BlockRunner runner(block, 2, 2);
        EXPECT_TRUE(runner.prepare().has_value());
        const Signal tone = sine(kSecond, 220.0, kTestSampleRate, 0.5f);
        MultiSignal out = runner.run(MultiSignal{tone, tone});
        if (change) {
            setShift(block, -5, 40.0f);
        }
        for (int piece = 0; piece < 2; ++piece) {
            const MultiSignal after = runner.run(MultiSignal{tone, tone});
            out[0].insert(out[0].end(), after[0].begin(), after[0].end());
        }
        return out[0];
    };
    const float still = highestAbove10k(run(false), kSecond / 2);
    const float changed = highestAbove10k(run(true), kSecond / 2);
    std::cerr << "[measure] above 10 kHz: " << still << " dB left alone, " << changed << " dB changed\n";
    EXPECT_LT(changed, still + 6.0f) << "the change clicked";
    EXPECT_LT(changed, -50.0f);
}

TEST(PitchShiftTest, TheNewPitchIsHeardOnceTheCrossoverIsDone)
{
    PitchShift block;
    rt::BlockMeter meter;
    BlockRunner runner(block, 2, 2);
    runner.setMeter(&meter);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal tone = sine(kSecond, 440.0, kTestSampleRate, 0.5f);
    (void)runner.run(MultiSignal{tone, tone});
    EXPECT_FLOAT_EQ(meter.value(PitchShift::kShiftValue), 0.0f);
    setShift(block, 7);
    const MultiSignal out = runner.run(MultiSignal{tone, tone});
    EXPECT_FLOAT_EQ(meter.value(PitchShift::kShiftValue), 7.0f);
    const double heard = frequencyOf(out[0], kSecond / 2, kSecond, kTestSampleRate);
    EXPECT_NEAR(heard, 440.0 * std::pow(2.0, 7.0 / 12.0), 5.0);
}

TEST(PitchShiftTest, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    PitchShift block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal noise = whiteNoise(kSecond, 0.5f, 121);
    // Every change goes over to a new SoundTouch while the sound runs: to the
    // top of the range, the bottom, and back, each crossed over in full.
    for (const auto &[semitones, cents] :
         {std::pair{0, 0.0f}, std::pair{12, 100.0f}, std::pair{-12, -100.0f}, std::pair{0, 50.0f}}) {
        setShift(block, semitones, cents);
        std::size_t allocations = 1;
        const MultiSignal out =
            runner.runWatchingForAllocations(MultiSignal{noise, noise}, CycleShape::random(1, 512, 3), allocations);
        EXPECT_EQ(allocations, 0U) << semitones << " st " << cents << " ct";
        EXPECT_EQ(out[0].size(), noise.size());
    }
    {
        const NoAllocations realTime;
        block.reset();
    }
}

TEST(PitchShiftTest, WhatWasSetIsWhatComesBack)
{
    PitchShift block;
    setShift(block, -4, 37.5f);
    PitchShift opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    EXPECT_EQ(*opened.param("semitones").asInt(), -4);
    EXPECT_FLOAT_EQ(*opened.param("cents").asFloat(), 37.5f);
    EXPECT_FLOAT_EQ(opened.shift(), -3.625f);
    EXPECT_FALSE(block.setParam("semitones", ParamValue::ofInt(13)).has_value());
    EXPECT_FALSE(block.setParam("cents", ParamValue::ofFloat(100.5f)).has_value());
}

TEST(PitchShiftTest, ASessionOpenedWithAShiftIsHeardWithItFromTheStart)
{
    // Settings put back after the block was prepared, as a session opening
    // does, are the ones heard: nothing has been heard yet to cross over from.
    PitchShift block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    ParamValueMap state;
    state["semitones"] = ParamValue::ofInt(-12);
    ASSERT_TRUE(block.loadState(state).has_value());
    const Signal tone = sine(2 * kSecond, 440.0, kTestSampleRate, 0.5f);
    const MultiSignal out = runner.run(MultiSignal{tone, tone});
    // From the first moment anything is heard: a crossover from the pitch
    // the block was prepared with would put 440 Hz here for a while.
    const std::size_t heardFrom = block.info().latencySamples + 256;
    const double heard = frequencyOf(out[0], heardFrom, heardFrom + 2048, kTestSampleRate);
    EXPECT_NEAR(heard, 220.0, 220.0 * 0.02);
}

TEST(PitchShiftTest, AMinuteCostsLessThanThreePerCentOfACore)
{
    PitchShift block;
    setShift(block, 5);
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal voice = voiceMaterial(60 * kSecond);
    const auto started = std::chrono::steady_clock::now();
    (void)runner.run(MultiSignal{voice, voice}, CycleShape::fixed(256));
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const double share = took / 60.0;
    std::cerr << "pitch shift: a minute of stereo in " << took << " s, " << share * 100.0 << " % of a core\n";
#ifdef NDEBUG
    EXPECT_LT(share, 0.03) << "a minute took " << took << " s";
#else
    GTEST_SKIP() << "the figure above is from a debug build";
#endif
}

TEST(PitchShiftTest, ListeningFilesWhenAskedFor)
{
    // Only for a person to listen to: RB_LISTEN_DIR=/some/folder writes the
    // voice-like material and a few shifts of it. Never played.
    const char *folder = std::getenv("RB_LISTEN_DIR");
    if (folder == nullptr || !audioFilesSupported()) {
        GTEST_SKIP() << "set RB_LISTEN_DIR to write the listening files";
    }
    const std::string dir{folder};
    const Signal voice = voiceMaterial(6 * kSecond);
    ASSERT_TRUE(writeWavFile(dir + "/input-voice.wav", MultiSignal{voice, voice}).has_value());
    struct Shift {
        const char *name;
        int semitones;
        float cents;
    };
    for (const Shift shift : {Shift{"up-3-st", 3, 0.0f},
                              Shift{"up-12-st", 12, 0.0f},
                              Shift{"down-5-st", -5, 0.0f},
                              Shift{"down-12-st", -12, 0.0f},
                              Shift{"up-25-ct", 0, 25.0f}}) {
        PitchShift block;
        setShift(block, shift.semitones, shift.cents);
        BlockRunner runner(block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        const MultiSignal out = runner.run(MultiSignal{voice, voice});
        ASSERT_TRUE(writeWavFile(dir + "/" + shift.name + ".wav", out).has_value());
    }
    // A change halfway, to hear the crossover.
    PitchShift block;
    setShift(block, 0);
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal half(voice.begin(), voice.begin() + static_cast<std::ptrdiff_t>(3 * kSecond));
    const Signal rest(voice.begin() + static_cast<std::ptrdiff_t>(3 * kSecond), voice.end());
    MultiSignal out = runner.run(MultiSignal{half, half});
    setShift(block, 4);
    const MultiSignal after = runner.run(MultiSignal{rest, rest});
    for (std::size_t channel = 0; channel < 2; ++channel) {
        out[channel].insert(out[channel].end(), after[channel].begin(), after[channel].end());
    }
    ASSERT_TRUE(writeWavFile(dir + "/change-0-to-4-st.wav", out).has_value());
}
