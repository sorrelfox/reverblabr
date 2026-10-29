// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Declick: clicks of every kind a record or a stream has — a spike,
// a pop, a dropout to nothing — from a tenth of a millisecond to three,
// added to music and to speech, found and mended; the same music and speech
// with none added left alone; the latency reported; and cheap enough.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/declick.h"
#include "blocksizeinvariance.h"
#include "dsp/decibel.h"
#include "offlinerunner.h"
#include "rt/meter.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>
#include <optional>
#include <random>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr std::size_t kSecond = 48000;

/// Brings \a signal in from silence over 10 ms and back out, as a
/// recording starts and ends: one that starts at full level after the
/// silence before it has a click of its own there, and so does one that
/// stops dead.
void fadeIn(Signal &signal)
{
    const std::size_t fade = std::min(kSecond / 100, signal.size() / 2);
    for (std::size_t n = 0; n < fade; ++n) {
        const float gain = static_cast<float>(n) / static_cast<float>(fade);
        signal[n] *= gain;
        signal[signal.size() - 1 - n] *= gain;
    }
}

/// Something with music's shape: three-note chords that change every half
/// second, each note six harmonics falling away and struck with a quick
/// attack, a slow decay and a quick release, over a bass line, with a
/// little hiss under it. Nothing in it is a click.
[[nodiscard]] Signal music(std::size_t frames, std::uint32_t seed)
{
    Signal out(frames, 0.0f);
    std::mt19937 random(seed);
    std::uniform_int_distribution<int> note(0, 24);
    std::normal_distribution<double> hiss(0.0, 1.0);
    const std::size_t chord = kSecond / 2;
    std::array<double, 4> hz{};
    for (std::size_t frame = 0; frame < frames; ++frame) {
        if (frame % chord == 0) {
            for (std::size_t voice = 0; voice < 3; ++voice) {
                hz[voice] = 220.0 * std::pow(2.0, note(random) / 12.0);
            }
            hz[3] = 55.0 * std::pow(2.0, note(random) / 24.0);
        }
        const double t = static_cast<double>(frame) / kTestSampleRate;
        const double since = static_cast<double>(frame % chord) / kTestSampleRate;
        // Struck, dying away, and let go of just before the next chord:
        // nothing in it steps.
        const double until = static_cast<double>(chord - frame % chord) / kTestSampleRate;
        const double envelope = std::min(1.0, since / 0.01) * std::exp(-since / 0.35) * std::min(1.0, until / 0.01);
        double sample = 0.0;
        for (std::size_t voice = 0; voice < 4; ++voice) {
            for (int harmonic = 1; harmonic <= 6; ++harmonic) {
                sample += std::sin(2.0 * std::numbers::pi * hz[voice] * harmonic * t) / harmonic;
            }
        }
        out[frame] = static_cast<float>(sample * envelope * 0.08 + hiss(random) * 1.0e-3);
    }
    fadeIn(out);
    return out;
}

/// Something with a voice's shape: a 140 Hz buzz with its harmonics up to
/// 4 kHz under two formant-like bumps, in syllables of a fifth of a second
/// at their own levels, with a pause every two seconds, over a little
/// hiss.
[[nodiscard]] Signal speech(std::size_t frames, std::uint32_t seed)
{
    Signal out(frames, 0.0f);
    std::mt19937 random(seed);
    std::uniform_real_distribution<float> level(-6.0f, 0.0f);
    std::normal_distribution<double> hiss(0.0, 1.0);
    float syllableGain = 1.0f;
    const std::size_t syllable = kSecond / 5;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        if (frame % syllable == 0) {
            syllableGain = dbToGain(level(random));
        }
        double sample = 0.0;
        // The pause starts and ends between syllables, as a speaker's does.
        if (frame % (2 * kSecond) < 2 * kSecond - 2 * syllable) {
            const double t = static_cast<double>(frame) / kTestSampleRate;
            for (int harmonic = 1; harmonic * 140.0 < 4000.0; ++harmonic) {
                const double hz = harmonic * 140.0;
                const double formants = std::exp(-std::pow((hz - 600.0) / 300.0, 2.0))
                    + 0.5 * std::exp(-std::pow((hz - 1800.0) / 500.0, 2.0)) + 0.05;
                sample += formants / harmonic * std::sin(2.0 * std::numbers::pi * hz * t);
            }
            const double phase = static_cast<double>(frame % syllable) / static_cast<double>(syllable);
            sample *= (0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * phase)) * syllableGain * 0.3;
        }
        out[frame] = static_cast<float>(sample + hiss(random) * 1.0e-3);
    }
    fadeIn(out);
    return out;
}

/// A click put into a signal: where, how long, and what it did.
/// The kinds of click put in: a record's crackle (a burst of noise dying
/// away, 0.1 to 3 ms), an impulse (a step dying away within 0.1 to 0.5 ms),
/// and a dropout, where the sound goes to nothing for 0.1 to 1 ms.
enum class Kind {
    Crackle,
    Impulse,
    Dropout
};

struct Click {
    std::size_t at = 0;
    std::size_t width = 0;
    Kind kind = Kind::Crackle;
};

/// Puts \a count clicks into \a signal, spread out and each where there is
/// sound to hear it against (-40 dBFS over 10 ms), of every kind in turn at
/// random.
[[nodiscard]] std::vector<Click> addClicks(Signal &signal, std::size_t count, std::uint32_t seed)
{
    std::mt19937 random(seed);
    std::uniform_real_distribution<float> height(0.1f, 0.6f);
    std::uniform_int_distribution<int> kind(0, 2);
    std::uniform_int_distribution<int> sign(0, 1);
    std::normal_distribution<float> noise(0.0f, 1.0f);
    std::vector<Click> clicks;
    const std::size_t spacing = signal.size() / (count + 1);
    const auto loud = [&signal](std::size_t at) {
        return rms(Signal(signal.begin() + static_cast<Signal::difference_type>(at - 240),
                          signal.begin() + static_cast<Signal::difference_type>(at + 240)))
            > 0.01f;
    };
    for (std::size_t i = 0; i < count; ++i) {
        Click click;
        click.kind = static_cast<Kind>(kind(random));
        const std::size_t longest = click.kind == Kind::Crackle ? 144 : (click.kind == Kind::Impulse ? 24 : 48);
        click.width = std::uniform_int_distribution<std::size_t>(5, longest)(random);
        click.at = spacing * (i + 1);
        for (std::size_t tries = 0; tries < 64 && !loud(click.at); ++tries) {
            click.at += 97;
        }
        const float amplitude = height(random) * (sign(random) == 0 ? 1.0f : -1.0f);
        for (std::size_t n = 0; n < click.width; ++n) {
            const double x = static_cast<double>(n) / static_cast<double>(click.width);
            float &sample = signal[click.at + n];
            switch (click.kind) {
            case Kind::Crackle:
                sample += amplitude * noise(random) * static_cast<float>(std::exp(-3.0 * x));
                break;
            case Kind::Impulse:
                sample += amplitude * static_cast<float>(std::exp(-6.0 * x));
                break;
            case Kind::Dropout:
                sample = 0.0f;
                break;
            }
        }
        clicks.push_back(click);
    }
    return clicks;
}

/// \a signal through a Declick at \a threshold, lined back up with the
/// input by the latency it reports.
[[nodiscard]] Signal
mended(const Signal &signal, float threshold = Declick::kDefaultThreshold, std::uint64_t *clicks = nullptr)
{
    Declick block;
    EXPECT_TRUE(block.setParam("threshold", ParamValue::ofFloat(threshold)).has_value());
    BlockRunner runner(block, 2, 2);
    EXPECT_TRUE(runner.prepare().has_value());
    const std::size_t latency = block.info().latencySamples;
    Signal padded = signal;
    padded.resize(signal.size() + latency, 0.0f);
    const MultiSignal out = runner.run(MultiSignal{padded, padded}, CycleShape::fixed(256));
    if (clicks != nullptr) {
        *clicks = block.clicks() / 2;
    }
    return Signal(out[0].begin() + static_cast<Signal::difference_type>(latency), out[0].end());
}

/// The squared difference between \a a and \a b over [from, to).
[[nodiscard]] double squaredError(const Signal &a, const Signal &b, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (std::size_t n = from; n < to; ++n) {
        const double d = static_cast<double>(a[n]) - static_cast<double>(b[n]);
        sum += d * d;
    }
    return sum;
}

/// What a Declick made of some clicks: how many it found (changed any of
/// their samples), and how far down the error they made is, all of them
/// together.
struct Outcome {
    std::size_t found = 0;
    std::size_t count = 0;
    double reductionDb = 0.0;
};

[[nodiscard]] Outcome judge(const Signal &clean,
                            const Signal &clicked,
                            const Signal &out,
                            const std::vector<Click> &clicks,
                            std::optional<Kind> only = std::nullopt)
{
    Outcome outcome;
    double before = 0.0;
    double after = 0.0;
    for (const Click &click : clicks) {
        if (only && click.kind != *only) {
            continue;
        }
        ++outcome.count;
        bool changed = false;
        for (std::size_t n = click.at; n < click.at + click.width; ++n) {
            changed = changed || std::fabs(out[n] - clicked[n]) > 1.0e-6f;
        }
        outcome.found += changed ? 1 : 0;
        const std::size_t from = click.at - 4;
        const std::size_t to = click.at + click.width + 4;
        before += squaredError(clicked, clean, from, to);
        after += squaredError(out, clean, from, to);
    }
    outcome.reductionDb = 10.0 * std::log10(after / before);
    return outcome;
}

/// Prints \a outcome, and for each kind of click.
void report(const char *what,
            const Signal &clean,
            const Signal &clicked,
            const Signal &out,
            const std::vector<Click> &clicks)
{
    const Outcome all = judge(clean, clicked, out, clicks);
    std::cerr << "declick: " << what << ", " << all.found << " of " << all.count << " found, " << all.reductionDb
              << " dB";
    const char *names[] = {"crackle", "impulses", "dropouts"};
    for (const Kind kind : {Kind::Crackle, Kind::Impulse, Kind::Dropout}) {
        const Outcome some = judge(clean, clicked, out, clicks, kind);
        std::cerr << "; " << names[static_cast<int>(kind)] << " " << some.found << "/" << some.count << " "
                  << some.reductionDb << " dB";
    }
    std::cerr << "\n";
}

} // namespace

TEST(DeclickTest, ClicksInMusicAreFoundAndMended)
{
    // The criterion's first half: 60 clicks of every kind in twenty seconds
    // of music. At least 90 % are found; all of them together are taken
    // down by 6 dB and more, the impulses by 10 and more. (A 3 ms stretch of
    // dense music cannot be told from the samples around it much better
    // than that: what is mended in its place is right in shape and
    // loudness, and fainter than the click was.)
    const Signal clean = music(kSecond * 20, 1);
    Signal clicked = clean;
    const std::vector<Click> clicks = addClicks(clicked, 60, 2);
    const Signal out = mended(clicked);
    report("music", clean, clicked, out, clicks);
    const Outcome all = judge(clean, clicked, out, clicks);
    EXPECT_GE(all.found * 10, all.count * 9);
    EXPECT_LT(all.reductionDb, -6.0);
    EXPECT_LT(judge(clean, clicked, out, clicks, Kind::Impulse).reductionDb, -10.0);
}

TEST(DeclickTest, ClicksInSpeechAreFoundAndMended)
{
    const Signal clean = speech(kSecond * 20, 3);
    Signal clicked = clean;
    const std::vector<Click> clicks = addClicks(clicked, 60, 4);
    const Signal out = mended(clicked);
    report("speech", clean, clicked, out, clicks);
    const Outcome all = judge(clean, clicked, out, clicks);
    EXPECT_GE(all.found * 10, all.count * 9);
    EXPECT_LT(all.reductionDb, -9.0);
    EXPECT_LT(judge(clean, clicked, out, clicks, Kind::Impulse).reductionDb, -10.0);
}

TEST(DeclickTest, CleanMaterialIsLeftAlone)
{
    // The criterion's second half: the same music and speech with nothing
    // added, half a minute of each, at the default threshold and at the most
    // sensitive. A false click is a stretch the block changed; counted
    // away from the very start and end, where the sound comes in from and
    // goes out to digital silence. At the default there are none; at the
    // most sensitive, a couple a second (note onsets, taken for clicks),
    // each changed by little.
    for (const bool isMusic : {true, false}) {
        const Signal clean = isMusic ? music(kSecond * 30, 5) : speech(kSecond * 30, 6);
        for (const float threshold : {Declick::kDefaultThreshold, Declick::kMinimumThreshold}) {
            const Signal out = mended(clean, threshold);
            const std::size_t from = kSecond / 10;
            const std::size_t to = clean.size() - kSecond / 10;
            std::size_t changes = 0;
            std::size_t lastChanged = 0;
            for (std::size_t n = from; n < to; ++n) {
                if (std::fabs(out[n] - clean[n]) > 1.0e-6f) {
                    // Two a minute for each in these thirty seconds.
                    changes += n > lastChanged + 256 ? 2 : 0;
                    lastChanged = n;
                }
            }
            const double changed = 10.0 * std::log10(squaredError(out, clean, from, to) + 1.0e-30)
                - 10.0 * std::log10(squaredError(clean, Signal(clean.size(), 0.0f), from, to));
            std::cerr << "declick: clean " << (isMusic ? "music" : "speech") << " at " << threshold << " dB, "
                      << changes << " false clicks a minute, the difference " << changed << " dB under the sound\n";
            if (threshold == Declick::kDefaultThreshold) {
                EXPECT_EQ(changes, 0U) << (isMusic ? "music" : "speech");
            } else {
                EXPECT_LE(changes, 200U) << (isMusic ? "music" : "speech");
                EXPECT_LT(changed, -40.0) << (isMusic ? "music" : "speech");
            }
        }
    }
}

TEST(DeclickTest, TheThresholdTradesClicksFoundForFalseOnes)
{
    // At 0 dB more is found than at 20, in the clicked signal and in the
    // clean one alike.
    const Signal clean = music(kSecond * 10, 7);
    Signal clicked = clean;
    (void)addClicks(clicked, 30, 8);
    std::uint64_t sensitive = 0;
    std::uint64_t insensitive = 0;
    (void)mended(clicked, 0.0f, &sensitive);
    (void)mended(clicked, 20.0f, &insensitive);
    EXPECT_GT(sensitive, insensitive);
    EXPECT_GE(sensitive, 30U);
}

TEST(DeclickTest, TheLatencyIsThreeQuartersOfAFrame)
{
    // With nothing to mend the output is the input, exactly the reported
    // latency later: 768 samples at 48 kHz, a frame of 1024 being the
    // first power of two past 20 ms.
    Declick block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    EXPECT_EQ(block.frameSize(), 1024U);
    EXPECT_EQ(block.info().latencySamples, 768U);
    EXPECT_EQ(block.longestClick(), 144U) << "3 ms at 48 kHz";
    // A steady tone, away from where it comes in from silence and goes back
    // to it, which it is right to take for clicks.
    Signal tone = sine(kSecond, 440.0, kTestSampleRate, 0.5f);
    fadeIn(tone);
    const MultiSignal out = runner.run(MultiSignal{tone, tone}, CycleShape::random(1, 2048, 80));
    for (std::size_t n = 2048; n + 2048 < tone.size(); ++n) {
        ASSERT_EQ(out[0][n + 768], tone[n]) << n;
    }
    for (std::size_t n = 0; n < 768; ++n) {
        ASSERT_EQ(out[0][n], 0.0f);
    }

    BlockRunner fast(block, 2, 2, 96000.0);
    ASSERT_TRUE(fast.prepare().has_value());
    EXPECT_EQ(block.info().latencySamples, 1536U) << "a frame of 2048 at 96 kHz";
    EXPECT_EQ(block.longestClick(), 288U);
}

TEST(DeclickTest, TheCycleLengthMakesNoDifference)
{
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    Signal clicked = music(kSecond * 2, 9);
    (void)addClicks(clicked, 10, 10);
    const InvarianceReport report =
        checkBlockSizeInvariance([] { return Declick::make(); }, MultiSignal{clicked, clicked}, options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(DeclickTest, NothingIsTakenOrGivenBackOnTheAudioThread)
{
    Declick block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    Signal clicked = music(kSecond * 2, 11);
    (void)addClicks(clicked, 10, 12);
    std::size_t allocations = 0;
    (void)runner.runWatchingForAllocations(MultiSignal{clicked, clicked}, CycleShape::random(1, 2048, 13), allocations);
    EXPECT_EQ(allocations, 0U);
    EXPECT_GT(block.clicks(), 0U) << "nothing was mended, so the mending was not watched";
}

TEST(DeclickTest, TheClicksMendedArePublished)
{
    Declick block;
    rt::BlockMeter meter;
    BlockRunner runner(block, 2, 2);
    runner.setMeter(&meter);
    ASSERT_TRUE(runner.prepare().has_value());
    Signal clicked = music(kSecond * 2, 14);
    (void)addClicks(clicked, 10, 15);
    (void)runner.run(MultiSignal{clicked, clicked});
    EXPECT_EQ(meter.value(Declick::kClicksValue), static_cast<float>(block.clicks()));
    EXPECT_GE(block.clicks(), 20U) << "ten clicks on each of two channels";
}

TEST(DeclickTest, TheThresholdIsSavedAndLoaded)
{
    Declick block;
    ASSERT_TRUE(block.setParam("threshold", ParamValue::ofFloat(12.5f)).has_value());
    Declick opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    EXPECT_EQ(*opened.param("threshold").asNumber(), 12.5);
    EXPECT_EQ(*Declick().param("threshold").asNumber(), static_cast<double>(Declick::kDefaultThreshold));
}

TEST(DeclickTest, AMinuteCostsLessThanThreePerCentOfACore)
{
    Declick block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    // A badly worn record: fifty clicks a second, every one of them found,
    // measured and mended, on both channels.
    Signal clicked = music(kSecond * 60, 16);
    (void)addClicks(clicked, 3000, 17);
    const auto started = std::chrono::steady_clock::now();
    (void)runner.run(MultiSignal{clicked, clicked}, CycleShape::fixed(256));
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const double share = took / 60.0;
    std::cerr << "declick: a minute of stereo in " << took << " s, " << share * 100.0 << " % of a core\n";
#ifdef NDEBUG
    EXPECT_LT(share, 0.03) << "a minute took " << took << " s";
#else
    GTEST_SKIP() << "the figure above is from a debug build";
#endif
}
