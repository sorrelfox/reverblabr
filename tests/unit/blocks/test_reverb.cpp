// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Reverb: a decay that is the one set, measured the way a room is;
// damping that shortens the top and not the bottom; silence that ends in
// zeros rather than denormals; a network that never runs away; and no
// click when the size, the pre-delay or the decay move.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/reverb.h"
#include "blocksizeinvariance.h"
#include "dsp/biquad.h"
#include "dsp/decibel.h"
#include "offlinerunner.h"
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

[[nodiscard]] MultiSignal through(Reverb &block, const MultiSignal &input, std::uint32_t cycle = 256)
{
    BlockRunner runner(block, 2, 2);
    EXPECT_TRUE(runner.prepare().has_value());
    return runner.run(input, CycleShape::fixed(cycle));
}

void set(Reverb &block, std::string_view id, float value)
{
    EXPECT_TRUE(block.setParam(id, ParamValue::ofFloat(value)).has_value()) << id;
}

/// The reverb alone, from the start, with nothing in its way: no pre-delay,
/// no damping, full width, the mix at the reverb alone.
void wetOnly(Reverb &block, float decayMs, float size, float damping = 0.0f)
{
    set(block, "preDelay", 0.0f);
    set(block, "size", size);
    set(block, "decay", decayMs);
    set(block, "damping", damping);
    set(block, "width", 100.0f);
    set(block, "mix", 100.0f);
}

/// The decay time of \a response, the way a room's is measured: Schroeder's
/// backward integration (the energy still to come at each moment), a straight
/// line fitted to it from -5 to -35 dB, and that slope taken to -60 (T30).
[[nodiscard]] double rt60Seconds(const Signal &response)
{
    std::vector<double> remaining(response.size() + 1, 0.0);
    for (std::size_t n = response.size(); n > 0; --n) {
        const auto sample = static_cast<double>(response[n - 1]);
        remaining[n - 1] = remaining[n] + sample * sample;
    }
    const double total = remaining[0];
    if (total <= 0.0) {
        return 0.0;
    }
    double sumX = 0.0;
    double sumY = 0.0;
    double sumXX = 0.0;
    double sumXY = 0.0;
    double count = 0.0;
    for (std::size_t n = 0; n < response.size(); ++n) {
        const double level = 10.0 * std::log10(remaining[n] / total);
        if (level > -5.0) {
            continue;
        }
        if (level < -35.0) {
            break;
        }
        const double x = static_cast<double>(n);
        sumX += x;
        sumY += level;
        sumXX += x * x;
        sumXY += x * level;
        count += 1.0;
    }
    if (count < 2.0) {
        return 0.0;
    }
    const double slope = (count * sumXY - sumX * sumY) / (count * sumXX - sumX * sumX);
    return -60.0 / slope / static_cast<double>(kSecond);
}

/// What an impulse into both sides becomes on the left, \a seconds long.
[[nodiscard]] Signal impulseResponse(Reverb &block, double seconds)
{
    const auto frames = static_cast<std::size_t>(seconds * static_cast<double>(kSecond));
    const Signal in = impulse(frames, 0, 1.0f);
    return through(block, MultiSignal{in, in})[0];
}

/// \a signal through a band pass at \a hz: the part of a decay near there.
[[nodiscard]] Signal band(const Signal &signal, double hz)
{
    Biquad first;
    Biquad second;
    first.setCoefficients(designBiquad({FilterShape::BandPass, hz, 2.0, 0.0}, kTestSampleRate));
    second.setCoefficients(designBiquad({FilterShape::BandPass, hz, 2.0, 0.0}, kTestSampleRate));
    Signal out(signal.size());
    for (std::size_t n = 0; n < signal.size(); ++n) {
        out[n] = second.process(first.process(signal[n]));
    }
    return out;
}

/// The loudest a signal gets above 12 kHz, once the filters used to look
/// have settled: where a click shows, and a dark reverb of a low tone does
/// not.
[[nodiscard]] float highestAbove12k(const Signal &signal, std::size_t from)
{
    Biquad first;
    Biquad second;
    first.setCoefficients(designBiquad({FilterShape::HighPass, 12000.0, kButterworth4Q1, 0.0}, kTestSampleRate));
    second.setCoefficients(designBiquad({FilterShape::HighPass, 12000.0, kButterworth4Q2, 0.0}, kTestSampleRate));
    float highest = 0.0f;
    for (std::size_t frame = 0; frame < signal.size(); ++frame) {
        const float high = second.process(first.process(signal[frame]));
        if (frame >= from) {
            highest = std::max(highest, std::fabs(high));
        }
    }
    return gainToDb(highest);
}

/// Where \a signal first reaches a thousandth of its loudest.
[[nodiscard]] std::size_t onset(const Signal &signal)
{
    const float threshold = 1.0e-3f * peak(signal);
    for (std::size_t n = 0; n < signal.size(); ++n) {
        if (std::fabs(signal[n]) >= threshold) {
            return n;
        }
    }
    return signal.size();
}

/// Plucked notes and a noise hit, dry: what a room is heard on.
[[nodiscard]] Signal dryMaterial(std::size_t frames)
{
    Signal out(frames, 0.0f);
    const Signal hit = whiteNoise(frames, 1.0f, 41);
    constexpr std::array<double, 4> kNotes{220.0, 277.2, 329.6, 440.0};
    for (std::size_t n = 0; n < frames; ++n) {
        const double t = static_cast<double>(n) / kTestSampleRate;
        const double beat = std::fmod(t, 1.0);
        const double note = kNotes[static_cast<std::size_t>(t) % kNotes.size()];
        double sample = 0.0;
        if (beat < 0.4) {
            sample += 0.3 * std::exp(-beat / 0.06) * std::sin(2.0 * std::numbers::pi * note * beat);
        }
        const double off = std::fmod(t + 0.5, 1.0);
        if (off < 0.05) {
            sample += 0.2 * std::exp(-off / 0.01) * hit[n];
        }
        out[n] = static_cast<float>(sample);
    }
    return out;
}

} // namespace

TEST(ReverbTest, TheDecayIsTheOneSetAtEverySize)
{
    // The criterion: RT60 within 15 % of the setting. With no damping, every
    // line loses 60 dB in the decay time, so the whole does too.
    for (const float size : {0.0f, 50.0f, 100.0f}) {
        for (const float decayMs : {300.0f, 1000.0f, 3000.0f, 8000.0f}) {
            Reverb block;
            wetOnly(block, decayMs, size);
            const double seconds = static_cast<double>(decayMs) * 1.0e-3;
            const double measured = rt60Seconds(impulseResponse(block, seconds * 1.2 + 0.3));
            std::cerr << "[measure] size " << size << " %, decay " << decayMs << " ms: RT60 " << measured * 1000.0
                      << " ms\n";
            EXPECT_NEAR(measured, seconds, 0.15 * seconds) << "size " << size << " %, decay " << decayMs << " ms";
        }
    }
}

TEST(ReverbTest, DampingShortensTheTopAndLeavesTheBottom)
{
    // At 70 % damping the top should die away much sooner than the bottom,
    // and the bottom still in the time set. "The top" is where the loop's
    // low pass bites: an octave under the highest frequency there is.
    Reverb block;
    wetOnly(block, 2000.0f, 50.0f, 70.0f);
    const Signal response = impulseResponse(block, 2.8);
    const double low = rt60Seconds(band(response, 250.0));
    const double high = rt60Seconds(band(response, 12000.0));
    std::cerr << "[measure] damping 70 %, decay 2000 ms: RT60 " << low * 1000.0 << " ms at 250 Hz, " << high * 1000.0
              << " ms at 12 kHz\n";
    EXPECT_NEAR(low, 2.0, 0.3) << "the bottom did not keep the decay set";
    EXPECT_LT(high, 0.5 * low) << "the top did not die away sooner";

    // And with no damping the two are alike.
    Reverb flat;
    wetOnly(flat, 2000.0f, 50.0f, 0.0f);
    const Signal even = impulseResponse(flat, 2.8);
    EXPECT_NEAR(rt60Seconds(band(even, 12000.0)), rt60Seconds(band(even, 250.0)), 0.3);
}

TEST(ReverbTest, SilenceAfterASoundEndsInZerosNotDenormals)
{
    // The criterion's first half. The shortest decay reaches the denormal
    // range within a couple of seconds; this runs without the session's
    // flush-to-zero, so only the network's own flushing keeps it out.
    Reverb block;
    wetOnly(block, 100.0f, 30.0f);
    const std::size_t frames = 4 * kSecond;
    Signal in = silence(frames);
    for (std::size_t n = 0; n < 480; ++n) {
        in[n] = 0.5f * std::sin(2.0f * std::numbers::pi_v<float> * 1000.0f * static_cast<float>(n) / 48000.0f);
    }
    const MultiSignal out = through(block, MultiSignal{in, in});
    std::size_t subnormal = 0;
    for (const Signal &side : out) {
        for (const float sample : side) {
            if (std::fpclassify(sample) == FP_SUBNORMAL) {
                ++subnormal;
            }
        }
    }
    EXPECT_EQ(subnormal, 0U) << "the network's tail went through the denormal range";
    const auto lastSecond = std::span<const float>(out[0]).subspan(3 * kSecond);
    EXPECT_EQ(peak(lastSecond), 0.0f) << "the tail never reached zero";
}

TEST(ReverbTest, SilenceCostsNoMoreThanSound)
{
    // The criterion's first half again, as time: no second of the silence
    // after a burst takes longer than a second of the burst did. Second by
    // second, because the tail goes through the denormal range in about a
    // third of one and then reaches zero, so a minute's average hides it.
    Reverb block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    set(block, "decay", 150.0f);
    set(block, "damping", 0.0f);
    const Signal noise = whiteNoise(10 * kSecond, 0.5f, 61);
    const auto soundStarted = std::chrono::steady_clock::now();
    (void)runner.run(MultiSignal{noise, noise}, CycleShape::fixed(256));
    const double sound = std::chrono::duration<double>(std::chrono::steady_clock::now() - soundStarted).count() / 10.0;
    const Signal quiet = silence(kSecond);
    double slowest = 0.0;
    for (int second = 0; second < 20; ++second) {
        const auto started = std::chrono::steady_clock::now();
        (void)runner.run(MultiSignal{quiet, quiet}, CycleShape::fixed(256));
        slowest = std::max(slowest, std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
    }
    std::cerr << "[measure] a second of sound took " << sound * 1000.0 << " ms, the slowest second of silence after it "
              << slowest * 1000.0 << " ms\n";
#ifdef NDEBUG
    EXPECT_LT(slowest, 2.0 * sound) << "silence cost more than sound";
#else
    GTEST_SKIP() << "the figures above are from a debug build";
#endif
}

TEST(ReverbTest, TheLongestDecayStaysBoundedAndStillDiesAway)
{
    // Stable: half a minute of loud noise into the longest, largest,
    // undamped room never runs away, and once the noise stops the room falls
    // at the rate set.
    Reverb block;
    wetOnly(block, Reverb::kLongestDecayMs, 100.0f);
    Signal in = whiteNoise(40 * kSecond, 0.7f, 71);
    std::fill(in.begin() + static_cast<Signal::difference_type>(30 * kSecond), in.end(), 0.0f);
    const MultiSignal out = through(block, MultiSignal{in, in});
    for (const float sample : out[0]) {
        ASSERT_TRUE(std::isfinite(sample));
    }
    EXPECT_LT(peakDb(out[0]), 12.0f) << "the room ran away";
    // Ten seconds after the noise stops, half the decay time: 30 dB down.
    const float atStop = rmsDb(std::span<const float>(out[0]).subspan(29 * kSecond, kSecond));
    const float later = rmsDb(std::span<const float>(out[0]).subspan(39 * kSecond, kSecond));
    std::cerr << "[measure] the longest room, 10 s after the noise stopped: " << later - atStop << " dB\n";
    EXPECT_NEAR(later - atStop, -30.0f, 4.5f);
}

TEST(ReverbTest, SteadyNoiseComesBackAtAboutItsOwnLevelWhateverTheDecay)
{
    // Mix alone sets how loud the reverb is: a long room spreads the same
    // energy over more time rather than piling it up. The same noise on both
    // sides, as a voice or any single source is: the network hears the two
    // folded to one, so two unlike sides come back 3 dB lower.
    for (const float decayMs : {300.0f, 2000.0f, 10000.0f}) {
        Reverb block;
        wetOnly(block, decayMs, 50.0f);
        const auto frames = static_cast<std::size_t>(decayMs * 48.0f) + 3 * kSecond;
        const Signal left = whiteNoise(frames, 0.3f, 81);
        const Signal &right = left;
        const MultiSignal out = through(block, MultiSignal{left, right});
        const auto last = [&](const Signal &signal) {
            return rmsDb(std::span<const float>(signal).subspan(frames - kSecond));
        };
        const float dry = 0.5f * (last(left) + last(right));
        const float wet = 0.5f * (last(out[0]) + last(out[1]));
        std::cerr << "[measure] decay " << decayMs << " ms: steady noise at " << wet - dry << " dB\n";
        EXPECT_NEAR(wet, dry, 3.0f) << "decay " << decayMs << " ms";
    }
}

TEST(ReverbTest, AtNoMixItIsTheOriginalWithNoLatency)
{
    Reverb block;
    set(block, "mix", 0.0f);
    EXPECT_EQ(block.info().latencySamples, 0U);
    const Signal left = whiteNoise(kSecond, 0.5f, 91);
    const Signal right = whiteNoise(kSecond, 0.5f, 92);
    const MultiSignal out = through(block, MultiSignal{left, right});
    EXPECT_EQ(out[0], left);
    EXPECT_EQ(out[1], right);
}

TEST(ReverbTest, ThePreDelayHoldsTheReverbBack)
{
    // The reverb starts the pre-delay later, to the sample.
    Reverb soon;
    wetOnly(soon, 1000.0f, 50.0f);
    Reverb late;
    wetOnly(late, 1000.0f, 50.0f);
    set(late, "preDelay", 100.0f);
    const std::size_t first = onset(impulseResponse(soon, 0.5));
    const std::size_t second = onset(impulseResponse(late, 0.5));
    EXPECT_EQ(second - first, 4800U) << "onsets at " << first << " and " << second;
}

TEST(ReverbTest, AtNoWidthBothSidesAreTheSame)
{
    Reverb block;
    wetOnly(block, 1500.0f, 50.0f);
    set(block, "width", 0.0f);
    const Signal left = whiteNoise(kSecond, 0.5f, 101);
    const Signal right = whiteNoise(kSecond, 0.5f, 102);
    const MultiSignal out = through(block, MultiSignal{left, right});
    EXPECT_EQ(out[0], out[1]);

    // And at full width they are nothing like each other: what differs
    // between them is about as loud as either.
    Reverb wide;
    wetOnly(wide, 1500.0f, 50.0f);
    const MultiSignal apart = through(wide, MultiSignal{left, right});
    EXPECT_GT(nullTestDb(apart[0], apart[1]), -3.0f);
}

TEST(ReverbTest, MovingTheSizeThePreDelayAndTheDecayMakesNoClick)
{
    // A low tone in a dark room has nothing above 12 kHz; a line jumping to
    // a new length, or the pre-delay jumping, would put a click there. Left
    // alone the run shows nothing at all; moved, what shows must stay 60 dB
    // under the tone (the loop filters are worked out every 32 samples, a
    // faint step well below that).
    const auto run = [](bool move) {
        Reverb block;
        BlockRunner runner(block, 2, 2);
        EXPECT_TRUE(runner.prepare().has_value());
        set(block, "damping", 90.0f);
        set(block, "mix", 50.0f);
        set(block, "preDelay", 10.0f);
        const Signal tone = sine(kSecond, 330.0, kTestSampleRate, 0.5f);
        MultiSignal out = runner.run(MultiSignal{tone, tone}, CycleShape::fixed(256));
        if (move) {
            set(block, "size", 90.0f);
            set(block, "preDelay", 180.0f);
            set(block, "decay", 6000.0f);
        }
        const MultiSignal after = runner.run(MultiSignal{tone, tone}, CycleShape::fixed(256));
        out[0].insert(out[0].end(), after[0].begin(), after[0].end());
        return out[0];
    };
    const float still = highestAbove12k(run(false), kSecond / 2);
    const float moved = highestAbove12k(run(true), kSecond / 2);
    std::cerr << "[measure] above 12 kHz: " << still << " dB left alone, " << moved << " dB moved\n";
    EXPECT_LT(still, -100.0f);
    EXPECT_LT(moved, -60.0f) << "moving the room clicked";
}

TEST(ReverbTest, ItSoundsTheSameWhateverTheQuantum)
{
    const Signal noise = whiteNoise(16384, 0.5f, 111);
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] {
            auto block = std::make_unique<Reverb>();
            set(*block, "decay", 1500.0f);
            set(*block, "damping", 50.0f);
            set(*block, "mix", 60.0f);
            set(*block, "preDelay", 7.3f);
            return std::unique_ptr<Block>(std::move(block));
        },
        MultiSignal{noise, noise},
        options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(ReverbTest, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    Reverb block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    // A size move, a pre-delay glide and a decay glide in progress too.
    set(block, "size", 80.0f);
    set(block, "preDelay", 120.0f);
    set(block, "decay", 4000.0f);
    set(block, "damping", 60.0f);
    const Signal noise = whiteNoise(9600, 0.5f, 121);
    std::size_t allocations = 1;
    const MultiSignal out =
        runner.runWatchingForAllocations(MultiSignal{noise, noise}, CycleShape::random(1, 512, 3), allocations);
    EXPECT_EQ(allocations, 0U);
    EXPECT_EQ(out[0].size(), noise.size());
}

TEST(ReverbTest, WhatWasSetIsWhatComesBack)
{
    Reverb block;
    set(block, "preDelay", 45.0f);
    set(block, "size", 70.0f);
    set(block, "decay", 3300.0f);
    set(block, "damping", 25.0f);
    set(block, "width", 80.0f);
    set(block, "mix", 30.0f);
    Reverb opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    EXPECT_FLOAT_EQ(*opened.param("preDelay").asFloat(), 45.0f);
    EXPECT_FLOAT_EQ(*opened.param("size").asFloat(), 70.0f);
    EXPECT_FLOAT_EQ(*opened.param("decay").asFloat(), 3300.0f);
    EXPECT_FLOAT_EQ(*opened.param("damping").asFloat(), 25.0f);
    EXPECT_FLOAT_EQ(*opened.param("width").asFloat(), 80.0f);
    EXPECT_FLOAT_EQ(*opened.param("mix").asFloat(), 30.0f);
    EXPECT_FALSE(block.setParam("decay", ParamValue::ofFloat(20001.0f)).has_value());
    EXPECT_FALSE(block.setParam("decay", ParamValue::ofFloat(99.0f)).has_value());
    EXPECT_FALSE(block.setParam("preDelay", ParamValue::ofFloat(251.0f)).has_value());
}

TEST(ReverbTest, EveryLineIsAPrimeLengthAndNoTwoAreAlike)
{
    // Prime lengths share no factor, so no two lines' echoes keep landing
    // together as a flutter.
    for (const float size : {0.0f, 37.0f, 100.0f}) {
        Reverb block;
        set(block, "size", size);
        BlockRunner runner(block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        const auto &lengths = block.lineLengths();
        for (std::size_t line = 0; line < lengths.size(); ++line) {
            for (std::uint32_t divisor = 2; divisor * divisor <= lengths[line]; ++divisor) {
                EXPECT_NE(lengths[line] % divisor, 0U) << "line " << line << " at size " << size;
            }
            for (std::size_t other = line + 1; other < lengths.size(); ++other) {
                EXPECT_NE(lengths[line], lengths[other]);
            }
        }
    }
}

TEST(ReverbTest, AMinuteCostsLessThanThreePerCentOfACore)
{
    Reverb block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    set(block, "size", 100.0f);
    set(block, "decay", 5000.0f);
    const Signal music = dryMaterial(60 * kSecond);
    const auto started = std::chrono::steady_clock::now();
    (void)runner.run(MultiSignal{music, music}, CycleShape::fixed(256));
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const double share = took / 60.0;
    std::cerr << "reverb: a minute of stereo in " << took << " s, " << share * 100.0 << " % of a core\n";
#ifdef NDEBUG
    EXPECT_LT(share, 0.03) << "a minute took " << took << " s";
#else
    GTEST_SKIP() << "the figure above is from a debug build";
#endif
}

TEST(ReverbTest, ListeningFilesWhenAskedFor)
{
    // Only for a person to listen to: RB_LISTEN_DIR=/some/folder writes the
    // dry material and the three factory rooms' settings on it. Never played.
    const char *folder = std::getenv("RB_LISTEN_DIR");
    if (folder == nullptr || !audioFilesSupported()) {
        GTEST_SKIP() << "set RB_LISTEN_DIR to write the listening files";
    }
    const std::string dir{folder};
    const Signal dry = dryMaterial(8 * kSecond);
    ASSERT_TRUE(writeWavFile(dir + "/input-dry.wav", MultiSignal{dry, dry}).has_value());
    struct Room {
        const char *name;
        float preDelay;
        float size;
        float decay;
        float damping;
        float width;
        float mix;
    };
    for (const Room room : {Room{"small-room", 5.0f, 20.0f, 600.0f, 50.0f, 80.0f, 25.0f},
                            Room{"hall", 30.0f, 90.0f, 3200.0f, 40.0f, 100.0f, 30.0f},
                            Room{"plate", 0.0f, 55.0f, 1800.0f, 15.0f, 100.0f, 30.0f},
                            Room{"hall-reverb-only", 30.0f, 90.0f, 3200.0f, 40.0f, 100.0f, 100.0f}}) {
        Reverb block;
        set(block, "preDelay", room.preDelay);
        set(block, "size", room.size);
        set(block, "decay", room.decay);
        set(block, "damping", room.damping);
        set(block, "width", room.width);
        set(block, "mix", room.mix);
        const MultiSignal out = through(block, MultiSignal{dry, dry});
        ASSERT_TRUE(writeWavFile(dir + "/" + room.name + ".wav", out).has_value());
    }
}
