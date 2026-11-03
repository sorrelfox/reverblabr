// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Denoise: a learned noise taken out of speech by at least 15 dB, the speech
// kept; the ticks a sound card picks up from the computer turned down as far
// as the rest of its noise, and a quiet note under them kept; the profile
// saved and restored, at another rate too; the latency reported.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/denoise.h"
#include "blocksizeinvariance.h"
#include "dsp/biquad.h"
#include "dsp/decibel.h"
#include "offlinerunner.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
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
/// Enough of a noise for the block to learn it, and a little over.
constexpr std::size_t kLearnFrames = kSecond * 21 / 2;

/// A steady background: pink noise, darkened a little, as a fan or an air
/// conditioner sounds, at -30 dBFS.
[[nodiscard]] Signal fanNoise(std::size_t frames, std::uint32_t seed, double sampleRate = kTestSampleRate)
{
    Signal noise = pinkNoise(frames, 1.0f, seed);
    Biquad shelf;
    shelf.setCoefficients(designBiquad({FilterShape::LowPass, 6000.0, kButterworthQ, 0.0}, sampleRate));
    for (float &sample : noise) {
        sample = shelf.process(sample);
    }
    const float scale = dbToGain(-30.0f) / rms(noise);
    for (float &sample : noise) {
        sample *= scale;
    }
    return noise;
}

/// What a sound card picks up from the computer it is plugged into, after
/// one measured: a faint hiss at -80 dBFS, a hum near 102 Hz whose level
/// wanders, small ticks every 7 to 50 ms, and now and then, every 150 to
/// 400 ms, a big one to -48 dBFS: a sharp drop that comes back over a few
/// milliseconds. The ticks are what a noise's average leaves out. \a gain
/// is how much louder the whole of it is.
[[nodiscard]] Signal deviceNoise(std::size_t frames, std::uint32_t seed, float gain = 1.0f)
{
    std::mt19937 random(seed);
    std::normal_distribution<float> hiss(0.0f, dbToGain(-80.0f));
    std::uniform_real_distribution<double> smallGap(0.007, 0.050);
    std::uniform_real_distribution<double> bigGap(0.150, 0.400);
    std::uniform_real_distribution<double> depth(0.3, 1.0);
    std::uniform_real_distribution<double> recovery(0.001, 0.004);
    Signal out(frames, 0.0f);
    double tick = 0.0;
    double decay = 0.0;
    auto nextSmall = static_cast<std::size_t>(smallGap(random) * kTestSampleRate);
    auto nextBig = static_cast<std::size_t>(bigGap(random) * kTestSampleRate);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        if (frame == nextSmall) {
            tick -= static_cast<double>(dbToGain(-72.0f)) * depth(random);
            decay = std::exp(-1.0 / (recovery(random) * kTestSampleRate));
            nextSmall = frame + static_cast<std::size_t>(smallGap(random) * kTestSampleRate);
        }
        if (frame == nextBig) {
            tick -= static_cast<double>(dbToGain(-48.0f)) * depth(random);
            decay = std::exp(-1.0 / (recovery(random) * kTestSampleRate));
            nextBig = frame + static_cast<std::size_t>(bigGap(random) * kTestSampleRate);
        }
        tick *= decay;
        const double t = static_cast<double>(frame) / kTestSampleRate;
        const double hum = static_cast<double>(dbToGain(-84.0f))
            * (1.0 + 0.6 * std::sin(2.0 * std::numbers::pi * 0.3 * t)) * std::sin(2.0 * std::numbers::pi * 102.0 * t);
        out[frame] = gain * (static_cast<float>(tick + hum) + hiss(random));
    }
    return out;
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

/// The loudest 10 ms of \a signal, in dBFS.
[[nodiscard]] double loudestTenMs(const Signal &signal)
{
    const std::size_t window = kSecond / 100;
    double loudest = -200.0;
    for (std::size_t from = 0; from + window <= signal.size(); from += window / 4) {
        loudest = std::max(loudest, static_cast<double>(rmsDb(part(signal, from, window))));
    }
    return loudest;
}

/// \a a and \a b added, sample by sample.
[[nodiscard]] Signal sum(const Signal &a, const Signal &b)
{
    Signal out(std::min(a.size(), b.size()));
    for (std::size_t n = 0; n < out.size(); ++n) {
        out[n] = a[n] + b[n];
    }
    return out;
}

/// Learns \a noise: asks, and plays it until the block says it has it.
void learn(Denoise &block, BlockRunner &runner, const Signal &noise)
{
    EXPECT_TRUE(block.setParam("learn", ParamValue::ofInt(1)).has_value());
    (void)run(runner, noise);
    EXPECT_TRUE(block.active()) << "it did not finish learning";
    EXPECT_FALSE(block.learning());
}

/// The profile \a block would save.
[[nodiscard]] Denoise::NoiseProfile savedProfile(const Denoise &block)
{
    const std::optional<Denoise::NoiseProfile> profile = Denoise::decodeProfile(block.saveState().at("profile").text());
    EXPECT_TRUE(profile.has_value());
    return profile.value_or(Denoise::NoiseProfile{});
}

/// A profile of a flat noise at \a level dB in every band, for a block that
/// has to be removing without learning first.
[[nodiscard]] std::string flatProfile(float level, float limit)
{
    Denoise::NoiseProfile profile;
    profile.valid = true;
    profile.sampleRate = 48000.0;
    profile.seconds = 10.0f;
    profile.now.fill(level);
    profile.held.fill(level - 4.0f);
    profile.nowLimits.fill(limit);
    profile.heldLimits.fill(limit);
    profile.rest.fill(-6.0f);
    return Denoise::encodeProfile(profile);
}

} // namespace

TEST(DenoiseTest, ALearnedNoiseIsTakenOutOfSpeechBy15Decibels)
{
    // The criterion. Ten and a half seconds of the fan alone to learn from,
    // then eight of the voice over a different stretch of the same fan: in
    // the voice's pauses what is left of the fan is at least 15 dB under
    // what went in.
    Denoise block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    learn(block, runner, fanNoise(kLearnFrames, 1));

    const Signal fan = fanNoise(kSecond * 8, 2);
    const Signal mixed = sum(fan, voice(kSecond * 8));
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
    Denoise block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    learn(block, runner, fanNoise(kLearnFrames, 3));

    const Signal fan = fanNoise(kSecond * 6, 4);
    const Signal speech = voice(kSecond * 6);
    const Signal mixed = sum(fan, speech);
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

TEST(DenoiseTest, TheComputersTicksGoDownAsFarAsTheRestOfItsNoise)
{
    // The case the fingerprint is for. A sound card's noise with its ticks,
    // learned; then more of it, with nothing else playing. The whole of it
    // comes down by the reduction, and so do the ticks: the loudest 10 ms
    // that is left is the loudest that went in, the reduction down. A
    // noise's average would have let the ticks through, they being far
    // above it.
    Denoise block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    learn(block, runner, deviceNoise(kLearnFrames, 21));

    const Signal noise = deviceNoise(kSecond * 6, 22);
    const Signal out = run(runner, noise);
    const Signal in = part(noise, kSecond, kSecond * 4);
    const Signal left = part(out, kSecond + kLatency, kSecond * 4);
    const double reduction = Denoise::kDefaultReductionDb;
    EXPECT_NEAR(rmsDb(left) - rmsDb(in), -reduction, 1.0);
    EXPECT_LT(loudestTenMs(left) - loudestTenMs(in), -(reduction - 1.0))
        << "the ticks came down only " << loudestTenMs(in) - loudestTenMs(left) << " dB";
    EXPECT_TRUE(block.heardNoiseAlone());
}

TEST(DenoiseTest, AQuietNoteTheTicksReachButThatLastsComesThrough)
{
    // The held level's reason to be. A tone in a band where the ticks reach
    // well above the steady noise: quieter than the ticks get, so a frame on
    // its own cannot tell it from them, but louder than the noise ever is
    // for long. It comes through within 2 dB; frame by frame it would lose
    // some 15. What it does lose is its skirts: the bands beside it hold a
    // ninth of its power each, are under the threshold, and go down.
    Denoise block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    learn(block, runner, deviceNoise(kLearnFrames, 31));
    const Denoise::NoiseProfile profile = savedProfile(block);

    // The band between 600 Hz and 2.4 kHz where the ticks stand furthest
    // above the steady noise. Not lower: there the hum and the ticks swing
    // the held level itself, and a sound has to stand out more to be heard.
    std::size_t band = 0;
    float widest = 0.0f;
    for (std::size_t bin = 26; bin <= 102; ++bin) {
        if (profile.now[bin] - profile.held[bin] > widest) {
            widest = profile.now[bin] - profile.held[bin];
            band = bin;
        }
    }
    // A tick is in four frames, the fourth at a seventh of its power: the
    // held level cannot stand much more than 9 dB under the ticks' ceiling.
    ASSERT_GT(widest, 8.0f) << "the ticks do not stand above the steady noise anywhere";
    // 6 dB over the held ceiling, past the threshold, and 2 dB or more under
    // the ceiling of the ticks, where a frame on its own would take it down.
    const float level = profile.held[band] + 6.0f;
    const double hz = static_cast<double>(band) * kTestSampleRate / static_cast<double>(Denoise::kFrameSize);
    const Signal tone = sine(kSecond * 4, hz, kTestSampleRate, dbToGain(level));
    const Signal out = run(runner, sum(deviceNoise(kSecond * 4, 32), tone));

    const Signal heard = part(out, kSecond + kLatency, kSecond * 2);
    const double kept = 20.0 * std::log10(amplitudeAt(heard, hz) / amplitudeAt(part(tone, kSecond, kSecond * 2), hz));
    EXPECT_NEAR(kept, 0.0, 2.0) << "a tone at " << hz << " Hz, " << level << " dB, " << widest
                                << " dB between the ceilings";
}

TEST(DenoiseTest, WhenTheNoiseGrowsTheFingerprintFollowsItUp)
{
    // A computer's noise grows when the computer works harder. Learned, and
    // then 4 dB louder: within a few seconds the ceilings have followed it,
    // and it goes down by the reduction again, ticks and all.
    Denoise block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    learn(block, runner, deviceNoise(kLearnFrames, 61));
    const Signal louder = deviceNoise(kSecond * 10, 62, dbToGain(4.0f));
    const Signal out = run(runner, louder);
    const Signal in = part(louder, kSecond * 7, kSecond * 2);
    const Signal left = part(out, kSecond * 7 + kLatency, kSecond * 2);
    const double reduction = Denoise::kDefaultReductionDb;
    EXPECT_NEAR(rmsDb(left) - rmsDb(in), -reduction, 1.0);
    EXPECT_LT(loudestTenMs(left) - loudestTenMs(in), -(reduction - 1.5));
    float most = 0.0f;
    for (std::size_t octave = 0; octave < Denoise::kOctaves; ++octave) {
        most = std::max(most, block.followed(octave));
    }
    EXPECT_GT(most, 2.0f) << "the ceilings did not follow the noise up";
}

TEST(DenoiseTest, WhenTheNoiseQuietensTheFingerprintStaysWhereItWasLearned)
{
    // Never below what was learned: a hiss can quieten while the ticks do
    // not. 6 dB quieter, the ceilings stay, and the ticks still go down.
    Denoise block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    learn(block, runner, deviceNoise(kLearnFrames, 71));
    const Signal quieter = deviceNoise(kSecond * 6, 72, dbToGain(-6.0f));
    const Signal out = run(runner, quieter);
    for (std::size_t octave = 0; octave < Denoise::kOctaves; ++octave) {
        EXPECT_EQ(block.followed(octave), 0.0f) << "octave " << octave;
    }
    const Signal in = part(quieter, kSecond * 3, kSecond * 2);
    const Signal left = part(out, kSecond * 3 + kLatency, kSecond * 2);
    EXPECT_LT(loudestTenMs(left) - loudestTenMs(in), -(Denoise::kDefaultReductionDb - 1.0));
}

TEST(DenoiseTest, InSilenceTheNoiseGoesDownByTheReduction)
{
    // "Noise reduction" is how far: with nothing but the learned noise, the
    // noise comes down by that, from a little to the most there is.
    for (const float reduction : {6.0f, 20.0f, Denoise::kHighestReductionDb}) {
        Denoise block;
        ASSERT_TRUE(block.setParam("reduction", ParamValue::ofFloat(reduction)).has_value());
        BlockRunner runner(block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        learn(block, runner, fanNoise(kLearnFrames, 41));
        const Signal fan = fanNoise(kSecond * 3, 42);
        const Signal out = run(runner, fan);
        const double change = rmsDb(part(out, kSecond + kLatency, kSecond)) - rmsDb(part(fan, kSecond, kSecond));
        EXPECT_NEAR(change, -reduction, 1.0) << "at " << reduction << " dB";
    }
}

TEST(DenoiseTest, TheThresholdIsHowFarAboveTheNoiseASoundPassesUntouched)
{
    // A steady tone 9 dB over the noise's held ceiling in its band: well
    // over "Keep sound above noise by" at its default, so it passes as it
    // came; and under it at 15 dB, so it goes down by the whole reduction.
    for (const auto &[threshold, expected] : {std::pair{3.0f, 0.0}, std::pair{15.0f, -20.0}}) {
        Denoise block;
        ASSERT_TRUE(block.setParam("threshold", ParamValue::ofFloat(threshold)).has_value());
        BlockRunner runner(block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        learn(block, runner, fanNoise(kLearnFrames, 51));
        const Denoise::NoiseProfile profile = savedProfile(block);
        const std::size_t bin = 43;
        const double hz = static_cast<double>(bin) * kTestSampleRate / static_cast<double>(Denoise::kFrameSize);
        const Signal tone = sine(kSecond * 3, hz, kTestSampleRate, dbToGain(profile.held[bin] + 9.0f));
        const Signal out = run(runner, sum(fanNoise(kSecond * 3, 52), tone));
        const double kept = 20.0
            * std::log10(amplitudeAt(part(out, kSecond + kLatency, kSecond), hz)
                         / amplitudeAt(part(tone, kSecond, kSecond), hz));
        EXPECT_NEAR(kept, expected, 1.5) << "with the threshold at " << threshold << " dB";
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
    learn(first, runner, fanNoise(kLearnFrames, 6));
    const ParamValueMap saved = first.saveState();
    const std::string text = saved.at("profile").text();
    ASSERT_FALSE(text.empty()) << "nothing was saved";
    EXPECT_EQ(text.rfind("v2;rate=48000;size=2048;seconds=10.0;now=", 0), 0U) << text.substr(0, 60);

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
    EXPECT_FALSE(Denoise::decodeProfile("v2;rate=48000;size=1024;seconds=1;now=0").has_value());
    EXPECT_FALSE(Denoise::decodeProfile(text.substr(0, text.find(";rest="))).has_value()) << "no rests";
    EXPECT_FALSE(Denoise::decodeProfile("nonsense").has_value());
}

TEST(DenoiseTest, AnEarlierVersionsProfileIsNotUsed)
{
    // The first version saved an average per band ("v1"). The fingerprint
    // cannot be had from that, so such a profile is not read: the block
    // passes the sound until the noise is learned again.
    std::string old = "v1;rate=48000;size=2048;seconds=3.0;db=";
    for (std::size_t bin = 0; bin < Denoise::kBins; ++bin) {
        old += bin > 0 ? ",-60.0" : "-60.0";
    }
    EXPECT_FALSE(Denoise::decodeProfile(old).has_value());
    Denoise block;
    ASSERT_TRUE(block.setParam("profile", ParamValue::ofString(old)).has_value());
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    EXPECT_FALSE(block.active());
    const Signal noise = whiteNoise(kSecond / 2, 0.5f, 8);
    const Signal out = run(runner, noise);
    Signal delayed(noise.size(), 0.0f);
    std::copy(noise.begin(), noise.end() - kLatency, delayed.begin() + kLatency);
    EXPECT_LE(nullTestDb(out, delayed), -100.0f);
}

TEST(DenoiseTest, ANoiseLearnedAtOneRateIsTakenOutAtAnother)
{
    // Learned at 44.1 kHz, saved, and used by a block running at 48: each
    // band is read at the frequency it now stands for, and the fan in
    // silence still comes down by 15 dB and more.
    constexpr double kCdRate = 44100.0;
    Denoise first;
    BlockRunner runner(first, 2, 2, kCdRate);
    ASSERT_TRUE(runner.prepare().has_value());
    learn(first, runner, fanNoise(static_cast<std::size_t>(kCdRate * 10.5), 9, kCdRate));
    const ParamValueMap saved = first.saveState();
    ASSERT_EQ(saved.at("profile").text().rfind("v2;rate=44100;", 0), 0U);

    Denoise second;
    ASSERT_TRUE(second.loadState(saved).has_value());
    BlockRunner secondRunner(second, 2, 2);
    ASSERT_TRUE(secondRunner.prepare().has_value());
    ASSERT_TRUE(second.active());
    const Signal fan = fanNoise(kSecond * 3, 10);
    const Signal out = run(secondRunner, fan);
    EXPECT_LT(rmsDb(part(out, kSecond + kLatency, kSecond)) - rmsDb(part(fan, kSecond, kSecond)), -15.0);
}

TEST(DenoiseTest, ForgettingPassesTheSoundAgainAndSavesNoProfile)
{
    Denoise block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    learn(block, runner, fanNoise(kLearnFrames, 8));
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
            // A profile about as loud as the noise, so that bands open and
            // close and octaves are heard and not.
            EXPECT_TRUE(block->setParam("profile", ParamValue::ofString(flatProfile(-36.0f, 20.0f))).has_value());
            return std::unique_ptr<Block>(std::move(block));
        },
        MultiSignal{noise, noise},
        options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(DenoiseTest, ARunTouchesTheAllocatorNotAtAll)
{
    // The criterion. Learning, finding the ceilings half way, finishing
    // learning (which publishes the profile for saving) and removing, all
    // under the trap.
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    Denoise block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    ASSERT_TRUE(block.setParam("learn", ParamValue::ofInt(1)).has_value());
    const Signal fan = fanNoise(kLearnFrames + kSecond, 10);
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
    const Signal quiet = fanNoise(kLearnFrames, 11);
    Signal loud = fanNoise(kLearnFrames, 12);
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
            if (!profile.has_value() || std::fabs(profile->now[400] - profile->now[450]) > 12.0f) {
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
    // Four 2048-point transforms every 512 samples, and three passes over
    // 1025 bands: the most expensive block yet, so it is measured.
    Denoise block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(block.setParam("profile", ParamValue::ofString(flatProfile(-60.0f, 10.0f))).has_value());
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
