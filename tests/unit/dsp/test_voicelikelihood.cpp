// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The voice likelihood: which detector runs and why, a reset that is a fresh
// start, nothing allocated, and what Level and rhythm makes of sound with
// and without syllables. The trained model is the blocks layer's, and is
// tested there; here a stand-in takes its place.

#include "allocationtrap.h"
#include "dsp/biquad.h"
#include "dsp/decibel.h"
#include "dsp/voicelikelihood.h"
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

using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr std::size_t kSecond = 48000;
constexpr std::size_t kFrame = 480;

/// Says the first sample of what it hears, so a test can tell what reached it.
class StandIn final : public VoiceModel
{
public:
    explicit StandIn(int *restarts = nullptr) noexcept
        : m_restarts(restarts)
    {
    }

    float hear(std::span<const float, kFrame> frame) noexcept RB_NONBLOCKING override
    {
        return frame[0];
    }

    void restart() noexcept RB_NONBLOCKING override
    {
        if (m_restarts != nullptr) {
            ++*m_restarts;
        }
    }

private:
    int *m_restarts;
};

[[nodiscard]] Signal atLevel(Signal signal, float levelDb)
{
    double energy = 0.0;
    for (const float sample : signal) {
        energy += static_cast<double>(sample) * sample;
    }
    const float scale = dbToGain(levelDb) / static_cast<float>(std::sqrt(energy / static_cast<double>(signal.size())));
    for (float &sample : signal) {
        sample *= scale;
    }
    return signal;
}

[[nodiscard]] Signal room(std::size_t frames, float levelDb, std::uint32_t seed)
{
    Signal noise = pinkNoise(frames, 1.0f, seed);
    Biquad dark;
    dark.setCoefficients(designBiquad({FilterShape::LowPass, 6000.0, kButterworthQ, 0.0}, kTestSampleRate));
    for (float &sample : noise) {
        sample = dark.process(sample);
    }
    return atLevel(std::move(noise), levelDb);
}

/// Half a second without a word in every three.
[[nodiscard]] bool inPause(std::size_t frame)
{
    return frame % (3 * kSecond) >= 3 * kSecond - kSecond / 2;
}

/// Band-limited noise in syllables of a fifth of a second, at their own
/// levels, with pauses. Nothing like a voice but its rhythm and its band.
[[nodiscard]] Signal syllables(std::size_t frames, std::uint32_t seed)
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
    float gain = 1.0f;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        if (frame % syllable == 0) {
            gain = dbToGain(level(random));
        }
        const double along = static_cast<double>(frame % syllable) / static_cast<double>(syllable);
        const float envelope =
            inPause(frame) ? 0.0f : static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * along));
        out[frame] = high.process(low.process(noise[frame])) * envelope * gain;
    }
    return atLevel(std::move(out), -24.0f);
}

[[nodiscard]] std::vector<float> framesOf(VoiceLikelihood &voice, const Signal &signal)
{
    std::vector<float> frames;
    for (const float sample : signal) {
        if (voice.push(sample)) {
            frames.push_back(voice.likelihood());
        }
    }
    return frames;
}

[[nodiscard]] std::vector<float> levelAndRhythm(const Signal &signal)
{
    VoiceLikelihood voice;
    voice.prepare(kTestSampleRate, nullptr);
    return framesOf(voice, signal);
}

[[nodiscard]] std::size_t voicedCount(const std::vector<float> &frames)
{
    return static_cast<std::size_t>(
        std::ranges::count_if(frames, [](float value) { return value >= VoiceLikelihood::kVoicedAt; }));
}

/// The share of the frames in or out of the pauses that read as voiced.
[[nodiscard]] double voicedShare(const std::vector<float> &frames, bool pauses)
{
    std::size_t counted = 0;
    std::size_t voiced = 0;
    for (std::size_t n = 0; n < frames.size(); ++n) {
        if (inPause(n * kFrame) == pauses) {
            ++counted;
            voiced += frames[n] >= VoiceLikelihood::kVoicedAt ? 1 : 0;
        }
    }
    return static_cast<double>(voiced) / static_cast<double>(counted);
}

} // namespace

TEST(VoiceLikelihoodTest, WithNoModelItSaysSoAndFallsBack)
{
    VoiceLikelihood voice;
    voice.prepare(48000.0, nullptr);
    EXPECT_EQ(voice.modelState(), VoiceModelState::NotInThisBuild);
    EXPECT_EQ(voice.detector(), VoiceDetector::LevelAndRhythm);
}

TEST(VoiceLikelihoodTest, AtAnotherRateTheModelIsLeftOutAndTheReasonGiven)
{
    VoiceLikelihood voice;
    voice.prepare(44100.0, std::make_unique<StandIn>());
    EXPECT_EQ(voice.modelState(), VoiceModelState::NeedsModelRate);
    EXPECT_EQ(voice.detector(), VoiceDetector::LevelAndRhythm);
    EXPECT_EQ(voice.frameSamples(), 441u);
    // No build's lack beats the rate: more than the rate would have to change.
    voice.prepare(44100.0, nullptr);
    EXPECT_EQ(voice.modelState(), VoiceModelState::NotInThisBuild);
}

TEST(VoiceLikelihoodTest, TheModelRunsWhereItCanOnFramesOfTenMilliseconds)
{
    VoiceLikelihood voice;
    voice.prepare(48000.0, std::make_unique<StandIn>());
    EXPECT_EQ(voice.modelState(), VoiceModelState::Ready);
    EXPECT_EQ(voice.detector(), VoiceDetector::Model);
    Signal ramp(2 * kFrame);
    ramp[0] = 0.25f;
    ramp[kFrame] = 0.75f;
    const std::vector<float> frames = framesOf(voice, ramp);
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_FLOAT_EQ(frames[0], 0.25f);
    EXPECT_FLOAT_EQ(frames[1], 0.75f);
    EXPECT_TRUE(voice.voiced());
}

TEST(VoiceLikelihoodTest, WhatTheModelSaysStaysOnTheScale)
{
    VoiceLikelihood voice;
    voice.prepare(48000.0, std::make_unique<StandIn>());
    Signal frame(kFrame);
    frame[0] = 3.0f;
    EXPECT_EQ(framesOf(voice, frame).back(), 1.0f);
    frame[0] = -3.0f;
    EXPECT_EQ(framesOf(voice, frame).back(), 0.0f);
}

TEST(VoiceLikelihoodTest, AskedForTheCheapOneItChangesWhereAFrameEnds)
{
    int restarts = 0;
    VoiceLikelihood voice;
    voice.prepare(48000.0, std::make_unique<StandIn>(&restarts));
    EXPECT_EQ(restarts, 1);
    voice.setPreferred(VoiceDetector::LevelAndRhythm);
    voice.push(0.0f);
    EXPECT_EQ(voice.detector(), VoiceDetector::Model) << "half a frame cannot change hands";
    for (std::size_t n = 1; n < kFrame; ++n) {
        voice.push(0.0f);
    }
    EXPECT_EQ(voice.detector(), VoiceDetector::LevelAndRhythm);
    EXPECT_EQ(voice.modelState(), VoiceModelState::Ready) << "the model can still run, and a card says so";

    voice.setPreferred(VoiceDetector::Model);
    for (std::size_t n = 0; n < kFrame; ++n) {
        voice.push(0.0f);
    }
    EXPECT_EQ(voice.detector(), VoiceDetector::Model);
    EXPECT_EQ(restarts, 2) << "the model comes back with nothing old in its head";
}

TEST(VoiceLikelihoodTest, ThePreferenceCannotBringBackAModelThatIsNotThere)
{
    VoiceLikelihood voice;
    voice.prepare(48000.0, nullptr);
    voice.setPreferred(VoiceDetector::Model);
    voice.reset();
    EXPECT_EQ(voice.detector(), VoiceDetector::LevelAndRhythm);
}

TEST(VoiceLikelihoodTest, AResetIsAFreshStart)
{
    const Signal signal = syllables(2 * kSecond, 13);
    VoiceLikelihood voice;
    voice.prepare(48000.0, nullptr);
    const std::vector<float> first = framesOf(voice, signal);
    voice.reset();
    EXPECT_EQ(voice.likelihood(), 0.0f);
    EXPECT_EQ(framesOf(voice, signal), first);
}

TEST(VoiceLikelihoodTest, NothingIsAllocatedOnEitherPath)
{
    const Signal signal = syllables(2 * kSecond, 14);
    VoiceLikelihood fallback;
    fallback.prepare(48000.0, nullptr);
    VoiceLikelihood model;
    model.prepare(48000.0, std::make_unique<StandIn>());
    {
        const NoAllocations realTime;
        for (const float sample : signal) {
            fallback.push(sample);
            model.push(sample);
        }
        fallback.reset();
        model.setPreferred(VoiceDetector::LevelAndRhythm);
        for (std::size_t n = 0; n < 2 * kFrame; ++n) {
            model.push(0.0f);
        }
        model.reset();
    }
}

TEST(LevelAndRhythmTest, SilenceIsNoVoice)
{
    EXPECT_EQ(voicedCount(levelAndRhythm(silence(5 * kSecond))), 0u);
}

TEST(LevelAndRhythmTest, SteadySoundFromTheStartIsNoVoice)
{
    const std::size_t frames = 10 * kSecond;
    EXPECT_EQ(voicedCount(levelAndRhythm(atLevel(whiteNoise(frames, 1.0f, 3), -30.0f))), 0u);
    EXPECT_EQ(voicedCount(levelAndRhythm(atLevel(pinkNoise(frames, 1.0f, 3), -30.0f))), 0u);
    EXPECT_EQ(voicedCount(levelAndRhythm(room(frames, -60.0f, 4))), 0u);
    EXPECT_EQ(voicedCount(levelAndRhythm(sine(frames, 1000.0, kTestSampleRate, 0.1f))), 0u);
}

TEST(LevelAndRhythmTest, AHeldNoteOverAQuietRoomOpensOnLevelThenStops)
{
    // The price of a quick start: a new steady sound reads as a voice until
    // the rhythm has been heard not to be there, and no longer.
    Signal signal = room(6 * kSecond, -70.0f, 6);
    const Signal tone = sine(signal.size(), 1000.0, kTestSampleRate, 0.1f);
    for (std::size_t n = 3 * kSecond; n < signal.size(); ++n) {
        signal[n] += tone[n];
    }
    const std::vector<float> frames = levelAndRhythm(signal);
    const auto after = [&](double seconds) {
        return std::vector<float>(frames.begin() + static_cast<std::ptrdiff_t>(seconds * 100.0), frames.end());
    };
    EXPECT_EQ(voicedCount(std::vector<float>(frames.begin(), frames.begin() + 299)), 0u);
    EXPECT_GT(voicedCount(after(3.0)), 0u) << "it opens quickly";
    EXPECT_EQ(voicedCount(after(4.0)), 0u) << "and has stopped within a second";
}

TEST(LevelAndRhythmTest, DigitalSilenceDoesNotUndoWhatItLearnedOfTheRoom)
{
    // A call's lane goes silent between two stretches of the same room. Taken
    // as a floor, the silence would make the room stand out for half a minute.
    Signal signal = room(12 * kSecond, -50.0f, 7);
    std::fill(signal.begin() + 4 * kSecond, signal.begin() + 5 * kSecond, 0.0f);
    EXPECT_EQ(voicedCount(levelAndRhythm(signal)), 0u);
}

TEST(LevelAndRhythmTest, SyllablesReadAsAVoiceAndThePausesDoNot)
{
    const std::vector<float> frames = levelAndRhythm(syllables(30 * kSecond, 70));
    EXPECT_GT(voicedShare(frames, false), 0.8);
    EXPECT_LT(voicedShare(frames, true), 0.05);
}

TEST(LevelAndRhythmTest, ARoomUnderTheSyllablesStaysOutOfThePauses)
{
    Signal signal = syllables(30 * kSecond, 70);
    const Signal noise = room(signal.size(), -50.0f, 5);
    for (std::size_t n = 0; n < signal.size(); ++n) {
        signal[n] += noise[n];
    }
    const std::vector<float> frames = levelAndRhythm(signal);
    EXPECT_GT(voicedShare(frames, false), 0.7);
    EXPECT_LT(voicedShare(frames, true), 0.05);
}

TEST(LevelAndRhythmTest, ItIsAboutAsCheapAsAMeter)
{
    const Signal signal = syllables(60 * kSecond, 15);
    VoiceLikelihood voice;
    voice.prepare(48000.0, nullptr);
    const auto started = std::chrono::steady_clock::now();
    for (const float sample : signal) {
        voice.push(sample);
    }
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const double share = took / 60.0;
    std::cerr << "level and rhythm: a minute in " << took << " s, " << share * 100.0 << " % of a core\n";
#ifdef NDEBUG
    EXPECT_LT(share, 0.005) << "a minute took " << took << " s";
#else
    GTEST_SKIP() << "the figure above is from a debug build";
#endif
}
