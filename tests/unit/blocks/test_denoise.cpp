// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Denoise: a learned noise taken out of speech by at least 15 dB,
// the speech kept, the profile saved and restored, the latency reported.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/denoise.h"
#include "blocksizeinvariance.h"
#include "dsp/biquad.h"
#include "dsp/decibel.h"
#include "offlinerunner.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <numbers>
#include <random>
#include <thread>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr std::size_t kSecond = 48000;
constexpr std::size_t kLatency = Denoise::kFrameSize;

/// A steady background: pink noise, darkened a little, as a fan or an air
/// conditioner sounds, at -30 dBFS.
[[nodiscard]] Signal fanNoise(std::size_t frames, std::uint32_t seed)
{
    Signal noise = pinkNoise(frames, 1.0f, seed);
    Biquad shelf;
    shelf.setCoefficients(designBiquad({FilterShape::LowPass, 6000.0, kButterworthQ, 0.0}, kTestSampleRate));
    for (float &sample : noise) {
        sample = shelf.process(sample);
    }
    const float scale = dbToGain(-30.0f) / rms(noise);
    for (float &sample : noise) {
        sample *= scale;
    }
    return noise;
}

/// Something with a voice's shape: a 140 Hz buzz with its harmonics up to
/// 4 kHz under two formant-like bumps, in syllables of a fifth of a second
/// at their own levels, and half a second of silence every two seconds.
/// Nothing like intelligible, but the parts a denoiser can break — steady
/// harmonics, onsets, gaps — are all there.
[[nodiscard]] Signal voice(std::size_t frames)
{
    Signal out(frames, 0.0f);
    std::mt19937 random(82);
    std::uniform_real_distribution<float> level(-6.0f, 0.0f);
    float syllableGain = 1.0f;
    const std::size_t syllable = kSecond / 5;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        if (frame % syllable == 0) {
            syllableGain = dbToGain(level(random));
        }
        const bool pause = frame % (2 * kSecond) >= 2 * kSecond - kSecond / 2;
        if (pause) {
            continue;
        }
        const double t = static_cast<double>(frame) / kTestSampleRate;
        double sample = 0.0;
        for (int harmonic = 1; harmonic * 140.0 < 4000.0; ++harmonic) {
            const double hz = harmonic * 140.0;
            const double formants = std::exp(-std::pow((hz - 600.0) / 300.0, 2.0))
                + 0.5 * std::exp(-std::pow((hz - 1800.0) / 500.0, 2.0)) + 0.05;
            sample += formants / harmonic * std::sin(2.0 * std::numbers::pi * hz * t);
        }
        const double phase = static_cast<double>(frame % syllable) / static_cast<double>(syllable);
        out[frame] = static_cast<float>(sample * (0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * phase))) * syllableGain;
    }
    const float scale = dbToGain(-18.0f) / peak(out);
    for (float &sample : out) {
        sample *= scale;
    }
    return out;
}

/// Runs \a signal through \a runner's block in cycles of 256, both sides
/// the same.
[[nodiscard]] Signal run(BlockRunner &runner, const Signal &signal)
{
    return runner.run(MultiSignal{signal, signal}, CycleShape::fixed(256))[0];
}

/// \a signal from \a from for \a length samples.
[[nodiscard]] Signal part(const Signal &signal, std::size_t from, std::size_t length)
{
    return Signal(signal.begin() + static_cast<Signal::difference_type>(from),
                  signal.begin() + static_cast<Signal::difference_type>(std::min(from + length, signal.size())));
}

/// Learns \a noise: asks, and plays it until the block says it has it.
void learn(Denoise &block, BlockRunner &runner, const Signal &noise)
{
    EXPECT_TRUE(block.setParam("learn", ParamValue::ofInt(1)).has_value());
    (void)run(runner, noise);
    EXPECT_TRUE(block.active()) << "it did not finish learning";
    EXPECT_FALSE(block.learning());
}

} // namespace

TEST(DenoiseTest, ALearnedNoiseIsTakenOutOfSpeechBy15Decibels)
{
    // The criterion. Three and a half seconds of the fan alone to learn
    // from, then eight of the voice over a different stretch of the same
    // fan: in the voice's pauses what is left of the fan is at least 15 dB
    // under what went in.
    Denoise block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    learn(block, runner, fanNoise(kSecond * 7 / 2, 1));

    const Signal fan = fanNoise(kSecond * 8, 2);
    const Signal speech = voice(kSecond * 8);
    Signal mixed(fan.size());
    for (std::size_t n = 0; n < mixed.size(); ++n) {
        mixed[n] = fan[n] + speech[n];
    }
    const Signal out = run(runner, mixed);

    // The pauses are the last half second of every two; the output is late
    // by the latency, and the edges of a pause are left to the frames that
    // straddle them.
    for (std::size_t pause = 1; pause <= 3; ++pause) {
        const std::size_t start = pause * 2 * kSecond - kSecond / 2 + kSecond / 10;
        const std::size_t length = kSecond / 4;
        const double before = rmsDb(part(fan, start, length));
        const double after = rmsDb(part(out, start + kLatency, length));
        EXPECT_LT(after - before, -15.0) << "pause " << pause << ": the fan came down only " << before - after << " dB";
    }
}

TEST(DenoiseTest, TheSpeechComesThrough)
{
    // The criterion's other half, measured: in the syllables the output is
    // closer to the clean voice than the noisy input was (the error against
    // the clean voice falls by at least 8 dB), and the voice's strong
    // harmonics, the ones well above the fan, keep their level within 3 dB.
    // A harmonic down at the fan's own level is taken down with it: that is
    // what a subtraction can and cannot tell apart.
    Denoise block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    learn(block, runner, fanNoise(kSecond * 7 / 2, 3));

    const Signal fan = fanNoise(kSecond * 6, 4);
    const Signal speech = voice(kSecond * 6);
    Signal mixed(fan.size());
    for (std::size_t n = 0; n < mixed.size(); ++n) {
        mixed[n] = fan[n] + speech[n];
    }
    const Signal out = run(runner, mixed);

    const std::size_t start = kSecond / 2;
    const std::size_t length = kSecond;
    const Signal clean = part(speech, start, length);
    const Signal noisy = part(mixed, start, length);
    const Signal cleaned = part(out, start + kLatency, length);
    Signal errorBefore(length);
    Signal errorAfter(length);
    for (std::size_t n = 0; n < length; ++n) {
        errorBefore[n] = noisy[n] - clean[n];
        errorAfter[n] = cleaned[n] - clean[n];
    }
    const double improvement = rmsDb(errorBefore) - rmsDb(errorAfter);
    EXPECT_GT(improvement, 8.0) << "the error against the clean voice fell by only " << improvement << " dB";
    for (const double hz : {420.0, 560.0, 700.0}) {
        const double kept = 20.0 * std::log10(amplitudeAt(cleaned, hz) / amplitudeAt(clean, hz));
        EXPECT_NEAR(kept, 0.0, 3.0) << "the harmonic at " << hz << " Hz";
    }
}

TEST(DenoiseTest, WithNothingLearnedItIsADelayAndNothingElse)
{
    // Before anything is learned, and while learning, the sound passes
    // untouched, late by the reported latency.
    Denoise block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    EXPECT_EQ(block.info().latencySamples, kLatency);
    const Signal noise = whiteNoise(kSecond, 0.5f, 5);
    const Signal out = run(runner, noise);
    Signal delayed(noise.size(), 0.0f);
    std::copy(noise.begin(), noise.end() - kLatency, delayed.begin() + kLatency);
    EXPECT_LE(nullTestDb(out, delayed), -100.0f);
}

TEST(DenoiseTest, TheReportedLatencyIsTheMeasuredOne)
{
    Denoise block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    Signal impulse(kSecond / 4, 0.0f);
    impulse[1000] = 0.5f;
    const Signal out = run(runner, impulse);
    const std::optional<int> measured = rb::test::latencySamples(impulse, out, 4096);
    ASSERT_TRUE(measured.has_value());
    EXPECT_EQ(static_cast<std::uint32_t>(*measured), block.info().latencySamples);
}

TEST(DenoiseTest, TheProfileIsSavedAndComesBack)
{
    // The criterion: a profile learned in one block, saved, and loaded into
    // another, removes the noise there as well as it did here.
    Denoise first;
    BlockRunner runner(first, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    learn(first, runner, fanNoise(kSecond * 7 / 2, 6));
    const ParamValueMap saved = first.saveState();
    const std::string text = saved.at("profile").text();
    ASSERT_FALSE(text.empty()) << "nothing was saved";
    EXPECT_EQ(text.rfind("v1;rate=48000;size=2048;seconds=3.0;db=", 0), 0U) << text.substr(0, 60);

    Denoise second;
    ASSERT_TRUE(second.loadState(saved).has_value());
    BlockRunner secondRunner(second, 2, 2);
    ASSERT_TRUE(secondRunner.prepare().has_value());
    EXPECT_TRUE(second.active()) << "the loaded profile was not used";
    const Signal fan = fanNoise(kSecond * 2, 7);
    const Signal out = run(secondRunner, fan);
    EXPECT_LT(rmsDb(part(out, kSecond, kSecond / 2)) - rmsDb(part(fan, kSecond, kSecond / 2)), -15.0);

    // And the text is the profile: decoded and written again it is the same.
    const std::optional<Denoise::NoiseProfile> decoded = Denoise::decodeProfile(text);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(Denoise::encodeProfile(*decoded) == text) << "the text read back was written differently";
    EXPECT_FALSE(Denoise::decodeProfile("v1;rate=48000;size=1024;seconds=1;db=0").has_value());
    EXPECT_FALSE(Denoise::decodeProfile("nonsense").has_value());
}

TEST(DenoiseTest, ForgettingPassesTheSoundAgainAndSavesNoProfile)
{
    Denoise block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    learn(block, runner, fanNoise(kSecond * 7 / 2, 8));
    ASSERT_TRUE(block.setParam("forget", ParamValue::ofInt(1)).has_value());
    (void)run(runner, Signal(256, 0.0f));
    EXPECT_FALSE(block.active());
    EXPECT_EQ(block.saveState().at("profile").text(), "");
}

TEST(DenoiseTest, ItSoundsTheSameWhateverTheQuantum)
{
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const Signal noise = whiteNoise(16384, 0.3f, 9);
    const InvarianceReport report = checkBlockSizeInvariance(
        [] {
            auto block = std::make_unique<Denoise>();
            // A profile, so it is removing and not only delaying.
            Denoise::NoiseProfile profile;
            profile.valid = true;
            profile.sampleRate = 48000.0;
            profile.seconds = 3.0f;
            profile.power.fill(4.0f);
            EXPECT_TRUE(block->setParam("profile", ParamValue::ofString(Denoise::encodeProfile(profile))).has_value());
            return std::unique_ptr<Block>(std::move(block));
        },
        MultiSignal{noise, noise},
        options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(DenoiseTest, ARunTouchesTheAllocatorNotAtAll)
{
    // The criterion. Learning, finishing learning (which publishes the
    // profile for saving) and removing, all under the trap.
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    Denoise block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    ASSERT_TRUE(block.setParam("learn", ParamValue::ofInt(1)).has_value());
    const Signal fan = fanNoise(kSecond * 4, 10);
    std::size_t allocations = 1;
    const MultiSignal out =
        runner.runWatchingForAllocations(MultiSignal{fan, fan}, CycleShape::random(1, 512, 3), allocations);
    EXPECT_EQ(allocations, 0U);
    EXPECT_TRUE(block.active());
    EXPECT_EQ(out[0].size(), fan.size());
}

TEST(DenoiseTest, SavingWhileItLearnsNeverReadsHalfAProfile)
{
    // The profile is learned on the audio thread and saved from another:
    // every profile a save reads is one whole profile, never half of one
    // learning and half of the next. Two learns of two different noises,
    // with a thread saving as fast as it can the whole time; under tsan this
    // is also the race check.
    Denoise block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal quiet = fanNoise(kSecond * 7 / 2, 11);
    Signal loud = fanNoise(kSecond * 7 / 2, 12);
    for (float &sample : loud) {
        sample *= 10.0f;
    }

    // **One saver for the whole run, presses and all**: a press
    // reaches the store from the audio thread while the other thread saves,
    // as it does in a running session, now that the store's numbers are
    // safe to share.
    std::atomic<bool> done{false};
    std::atomic<int> torn{0};
    std::atomic<int> saves{0};
    std::thread saver([&] {
        while (!done.load()) {
            const std::string text = block.saveState().at("profile").text();
            if (text.empty()) {
                continue;
            }
            const std::optional<Denoise::NoiseProfile> profile = Denoise::decodeProfile(text);
            // A whole profile of either noise is smooth between two bands
            // this close; half of each is 20 dB apart.
            if (!profile.has_value()
                || std::fabs(10.0f * std::log10(profile->power[400] / profile->power[600])) > 12.0f) {
                ++torn;
            }
            ++saves;
        }
    });
    block.applyParam(Denoise::Learn, 1.0f);
    (void)run(runner, quiet);
    block.applyParam(Denoise::Learn, 2.0f);
    (void)run(runner, loud);
    done = true;
    saver.join();
    EXPECT_EQ(torn.load(), 0);
    EXPECT_GT(saves.load(), 0);
}

TEST(DenoiseTest, AMinuteCostsLessThanThreePerCentOfACore)
{
    // Four 2048-point transforms every 512 samples, and a pass over 1025
    // bands: the most expensive block yet, so it is measured.
    Denoise block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    Denoise::NoiseProfile profile;
    profile.valid = true;
    profile.sampleRate = 48000.0;
    profile.seconds = 3.0f;
    profile.power.fill(1.0f);
    ASSERT_TRUE(block.setParam("profile", ParamValue::ofString(Denoise::encodeProfile(profile))).has_value());
    ASSERT_TRUE(runner.prepare().has_value());
    ASSERT_TRUE(block.active());

    const Signal noise = whiteNoise(kSecond * 60, 0.3f, 13);
    const auto started = std::chrono::steady_clock::now();
    (void)runner.run(MultiSignal{noise, noise}, CycleShape::fixed(256));
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const double share = took / 60.0;
    std::cerr << "denoise: a minute of stereo in " << took << " s, " << share * 100.0 << " % of a core\n";
#ifdef NDEBUG
    EXPECT_LT(share, 0.03) << "a minute took " << took << " s";
#else
    GTEST_SKIP() << "the figure above is from a debug build";
#endif
}
