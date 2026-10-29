// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The lookahead limiter. What it has to prove is one thing above all:
// nothing gets past the ceiling, whatever the signal does.

#include "analysis.h"
#include "dsp/audiobuffer.h"
#include "dsp/decibel.h"
#include "dsp/limiter.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <random>
#include <vector>

using namespace rb;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr double kRate = 48000.0;

/// Runs a signal through a limiter in cycles, and gives back what came out.
[[nodiscard]] MultiSignal through(LookaheadLimiter &limiter, const MultiSignal &input, std::uint32_t cycle = 256)
{
    const std::size_t frames = input.front().size();
    const auto channels = static_cast<std::uint32_t>(input.size());
    AudioBuffer buffer(channels, cycle);
    MultiSignal out(channels);

    for (std::size_t at = 0; at < frames; at += cycle) {
        const auto length = static_cast<std::uint32_t>(std::min<std::size_t>(cycle, frames - at));
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            const std::span<float> destination = buffer.channel(channel);
            for (std::uint32_t frame = 0; frame < length; ++frame) {
                destination[frame] = input[channel][at + frame];
            }
        }
        limiter.process(buffer.block(length), length);
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            const std::span<float> from = buffer.channel(channel);
            out[channel].insert(out[channel].end(), from.begin(), from.begin() + length);
        }
    }
    return out;
}

[[nodiscard]] MultiSignal stereo(Signal left, Signal right)
{
    MultiSignal signal;
    signal.push_back(std::move(left));
    signal.push_back(std::move(right));
    return signal;
}

} // namespace

TEST(LookaheadLimiterTest, BelowTheCeilingItIsADelayAndNothingElse)
{
    // The claim the whole design rests on: always in the path, and inaudible
    // until it is needed.
    LookaheadLimiter limiter;
    limiter.prepare(kRate, 2);
    const std::uint32_t latency = limiter.latencySamples();
    EXPECT_EQ(latency, 72U) << "1.5 ms at 48 kHz";

    const MultiSignal input = stereo(sine(4096, 1000.0, kRate, 0.5f), sine(4096, 700.0, kRate, 0.25f));
    const MultiSignal out = through(limiter, input);

    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        // **A loop, not std::copy with the iterators moved about.** The
        // optimised build cannot prove the bounds of that and turns
        // -Warray-bounds into an error; this says the same thing plainly.
        Signal delayed(input[channel].size(), 0.0f);
        for (std::size_t frame = latency; frame < delayed.size(); ++frame) {
            delayed[frame] = input[channel][frame - latency];
        }
        // kSilenceDb is as low as the null test reads, and that is where a
        // difference of nothing lands.
        EXPECT_LE(nullTestDb(out[channel], delayed), kSilenceDb) << "channel " << channel << " was changed";
    }
}

TEST(LookaheadLimiterTest, NothingGetsPastTheCeiling)
{
    LookaheadLimiter limiter;
    limiter.prepare(kRate, 2);
    const float ceiling = limiter.ceiling();

    // Four times full scale, which is what the Volume block's overdrive can
    // ask for, as a sine and as a step.
    MultiSignal input = stereo(sine(8192, 220.0, kRate, 4.0f), sine(8192, 220.0, kRate, 4.0f));
    for (std::size_t frame = 4000; frame < 4100; ++frame) {
        input[0][frame] = 3.5f; // a burst with no warning
        input[1][frame] = -3.5f;
    }
    const MultiSignal out = through(limiter, input);

    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        EXPECT_LE(peak(out[channel]), ceiling * 1.0001f)
            << "channel " << channel << " reached " << peakDb(out[channel]) << " dBFS";
    }
}

TEST(LookaheadLimiterTest, ASingleSampleSpikeIsCaughtBeforeItArrives)
{
    // The case a limiter without lookahead cannot do anything about.
    LookaheadLimiter limiter;
    limiter.prepare(kRate, 1);
    MultiSignal input;
    input.push_back(Signal(2048, 0.1f));
    input[0][1000] = 8.0f;

    const MultiSignal out = through(limiter, input);
    EXPECT_LE(peak(out[0]), limiter.ceiling() * 1.0001f) << "the spike went straight through";
}

TEST(LookaheadLimiterTest, TheGainComesBackUpAfterTheLoudPartAndTheQuietOneIsUntouched)
{
    LookaheadLimiter limiter;
    limiter.prepare(kRate, 1, LookaheadLimiter::kDefaultCeilingDb, LookaheadLimiter::kDefaultLookaheadMs, 20.0f);

    MultiSignal input;
    input.push_back(Signal(48000, 0.2f));
    for (std::size_t frame = 1000; frame < 2000; ++frame) {
        input[0][frame] = 4.0f; // a loud second of it
    }
    const MultiSignal out = through(limiter, input);

    // Long after the loud part, the quiet signal is back at its own level:
    // a limiter that stayed down would be a compressor nobody asked for.
    const Signal late(out[0].end() - 4096, out[0].end());
    EXPECT_NEAR(peak(late), 0.2f, 0.002f);
    EXPECT_FLOAT_EQ(limiter.gain(), 1.0f);
}

TEST(LookaheadLimiterTest, BothSidesComeDownTogether)
{
    // A peak on one side must not move the image; the gain is linked.
    LookaheadLimiter limiter;
    limiter.prepare(kRate, 2);
    MultiSignal input = stereo(Signal(4096, 0.5f), Signal(4096, 0.5f));
    for (std::size_t frame = 2000; frame < 2100; ++frame) {
        input[0][frame] = 4.0f; // only the left side is loud
    }

    const MultiSignal out = through(limiter, input);
    // Where the left side was held down, the right side came down by the
    // same amount, so the two are still equal where the input was equal.
    for (std::size_t frame = 2200; frame < 2300; ++frame) {
        EXPECT_NEAR(out[0][frame], out[1][frame], 1e-6f) << "at frame " << frame;
    }
}

TEST(LookaheadLimiterTest, ItSoundsTheSameWhateverTheCycleLength)
{
    // The quantum changes while a session runs; a limiter that kept
    // something per cycle would be heard changing.
    MultiSignal input = stereo(sine(8192, 300.0, kRate, 2.0f), sine(8192, 300.0, kRate, 2.0f));

    LookaheadLimiter first;
    first.prepare(kRate, 2);
    const MultiSignal steady = through(first, input, 256);

    LookaheadLimiter second;
    second.prepare(kRate, 2);
    MultiSignal ragged(2);
    std::mt19937 random(7);
    std::size_t at = 0;
    while (at < input[0].size()) {
        const std::size_t length = std::min<std::size_t>(32 + random() % 1000, input[0].size() - at);
        MultiSignal piece = stereo(Signal(input[0].begin() + static_cast<Signal::difference_type>(at),
                                          input[0].begin() + static_cast<Signal::difference_type>(at + length)),
                                   Signal(input[1].begin() + static_cast<Signal::difference_type>(at),
                                          input[1].begin() + static_cast<Signal::difference_type>(at + length)));
        const MultiSignal out = through(second, piece, static_cast<std::uint32_t>(length));
        for (std::uint32_t channel = 0; channel < 2; ++channel) {
            ragged[channel].insert(ragged[channel].end(), out[channel].begin(), out[channel].end());
        }
        at += length;
    }

    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        EXPECT_LE(nullTestDb(steady[channel], ragged[channel]), kSilenceDb) << "channel " << channel;
    }
}

TEST(LookaheadLimiterTest, NoiseNeverGetsPastItEither)
{
    // A sine is polite about where its peaks are. Noise is not.
    LookaheadLimiter limiter;
    limiter.prepare(kRate, 2);
    std::mt19937 random(20260921);
    std::uniform_real_distribution<float> loud(-4.0f, 4.0f);

    MultiSignal input = stereo(Signal(48000), Signal(48000));
    for (std::size_t frame = 0; frame < input[0].size(); ++frame) {
        // Bursts, so the gain is forever coming back up and being pulled
        // down again — the hardest thing for a release to get right.
        const bool burst = (frame / 2000) % 2 == 0;
        input[0][frame] = burst ? loud(random) : loud(random) * 0.05f;
        input[1][frame] = burst ? loud(random) : loud(random) * 0.05f;
    }

    const MultiSignal out = through(limiter, input, 128);
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        EXPECT_LE(peak(out[channel]), limiter.ceiling() * 1.0001f)
            << "channel " << channel << " reached " << peakDb(out[channel]) << " dBFS";
    }
}
