// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The trained voice model behind dsp::VoiceLikelihood, where the build has
// one: it tells a voice's shape from silence, on the same scale, and
// starts again cleanly, without touching the allocator. Where the build has none,
// it says so and the likelihood falls back.

#include "allocationtrap.h"
#include "blocks/voicemodel.h"
#include "dsp/voicelikelihood.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

#ifdef RB_HAVE_RNNOISE

namespace
{

constexpr std::size_t kSecond = 48000;

[[nodiscard]] bool inPause(std::size_t frame)
{
    return frame % (2 * kSecond) >= 2 * kSecond - kSecond / 2;
}

/// A buzz at a voice's pitch under two moving formants, in syllables of a
/// fifth of a second, with a pause in every two seconds.
[[nodiscard]] Signal voice(std::size_t frames)
{
    Signal out(frames, 0.0f);
    const std::size_t syllable = kSecond / 5;
    double phase = 0.0;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const double t = static_cast<double>(frame) / kTestSampleRate;
        const double pitch = 140.0 + 40.0 * std::sin(2.0 * std::numbers::pi * 0.7 * t);
        phase += 2.0 * std::numbers::pi * pitch / kTestSampleRate;
        if (inPause(frame)) {
            continue;
        }
        const auto index = static_cast<double>(frame / syllable);
        const double first = 600.0 + 200.0 * std::sin(index * 1.7);
        const double second = 1800.0 + 600.0 * std::sin(index * 2.3);
        double sample = 0.0;
        for (int harmonic = 1; harmonic * pitch < 4000.0; ++harmonic) {
            const double hz = harmonic * pitch;
            const double formants = std::exp(-std::pow((hz - first) / 250.0, 2.0))
                + 0.5 * std::exp(-std::pow((hz - second) / 400.0, 2.0)) + 0.03;
            sample += formants / harmonic * std::sin(harmonic * phase);
        }
        const double along = static_cast<double>(frame % syllable) / static_cast<double>(syllable);
        out[frame] = 0.05f * static_cast<float>(sample * (0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * along)));
    }
    return out;
}

[[nodiscard]] std::vector<float> framesOf(VoiceLikelihood &likelihood, const Signal &signal)
{
    std::vector<float> frames;
    for (const float sample : signal) {
        if (likelihood.push(sample)) {
            frames.push_back(likelihood.likelihood());
        }
    }
    return frames;
}

} // namespace

TEST(VoiceModelTest, ItRunsAtItsRateAndSaysWhyNotAtAnother)
{
    VoiceLikelihood likelihood;
    likelihood.prepare(48000.0, makeVoiceModel());
    EXPECT_EQ(likelihood.modelState(), VoiceModelState::Ready);
    EXPECT_EQ(likelihood.detector(), VoiceDetector::Model);

    likelihood.prepare(44100.0, makeVoiceModel());
    EXPECT_EQ(likelihood.modelState(), VoiceModelState::NeedsModelRate);
    EXPECT_EQ(likelihood.detector(), VoiceDetector::LevelAndRhythm);
}

TEST(VoiceModelTest, AVoicesShapeReadsAsAVoiceAndSilenceDoesNot)
{
    VoiceLikelihood likelihood;
    likelihood.prepare(48000.0, makeVoiceModel());
    const std::vector<float> frames = framesOf(likelihood, voice(20 * kSecond));
    std::size_t spoken = 0;
    std::size_t spokenVoiced = 0;
    for (std::size_t n = 0; n < frames.size(); ++n) {
        ASSERT_GE(frames[n], 0.0f);
        ASSERT_LE(frames[n], 1.0f);
        if (!inPause(n * 480)) {
            ++spoken;
            spokenVoiced += frames[n] >= VoiceLikelihood::kVoicedAt ? 1 : 0;
        }
    }
    EXPECT_GT(static_cast<double>(spokenVoiced) / static_cast<double>(spoken), 0.5);

    likelihood.reset();
    for (const float value : framesOf(likelihood, silence(2 * kSecond))) {
        EXPECT_LT(value, VoiceLikelihood::kVoicedAt);
    }
}

TEST(VoiceModelTest, AResetIsAFreshStart)
{
    const Signal signal = voice(kSecond);
    VoiceLikelihood likelihood;
    likelihood.prepare(48000.0, makeVoiceModel());
    const std::vector<float> first = framesOf(likelihood, signal);
    likelihood.reset();
    EXPECT_EQ(framesOf(likelihood, signal), first);
}

TEST(VoiceModelTest, TheModelTouchesNoAllocatorAndStartsAgainCleanly)
{
    const Signal signal = voice(kSecond);
    VoiceLikelihood likelihood;
    likelihood.prepare(48000.0, makeVoiceModel());
    {
        const NoAllocations realTime;
        for (const float sample : signal) {
            likelihood.push(sample);
        }
        likelihood.reset();
    }
}

#else

TEST(VoiceModelTest, ThisBuildHasNoneAndTheLikelihoodSaysSo)
{
    EXPECT_EQ(makeVoiceModel(), nullptr);
    VoiceLikelihood likelihood;
    likelihood.prepare(48000.0, makeVoiceModel());
    EXPECT_EQ(likelihood.modelState(), VoiceModelState::NotInThisBuild);
    EXPECT_EQ(likelihood.detector(), VoiceDetector::LevelAndRhythm);
}

#endif
