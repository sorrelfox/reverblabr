// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The dynamics processor. The curves are pure functions, so most of what has
// to be true can be asked of them directly; what the processor has to prove
// is that it follows a curve once the signal has settled, and that its timing
// is what the controls say. There are two curves: the compressor's, with a
// ratio and a knee, and the Dynamics Processor's four-point shape.

#include "analysis.h"
#include "dsp/audiobuffer.h"
#include "dsp/decibel.h"
#include "dsp/dynamics.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>
#include <vector>

using namespace rb;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr double kRate = 48000.0;

[[nodiscard]] DynamicsSettings defaultSettings()
{
    // The settings the "Default" preset is built from: a high ratio with a
    // wide knee that starts working gently, and no expander.
    DynamicsSettings settings;
    settings.thresholdDb = -20.0f;
    settings.ratio = 20.0f;
    settings.kneeDb = 12.0f;
    settings.expanderRatio = 1.0f;
    settings.expanderRangeDb = 0.0f;
    settings.makeupDb = 0.0f;
    return settings;
}

/// Runs a steady sine through the processor and gives back the level that
/// comes out once the timing has settled.
[[nodiscard]] double settledOutputDb(DynamicsProcessor &processor, float amplitude, double seconds = 1.0)
{
    const auto frames = static_cast<std::uint32_t>(kRate * seconds);
    AudioBuffer buffer(2, 512);
    double loudest = 0.0;
    const Signal tone = sine(frames, 1000.0, kRate, amplitude);

    for (std::uint32_t at = 0; at < frames; at += 512) {
        const std::uint32_t length = std::min<std::uint32_t>(512, frames - at);
        for (std::uint32_t channel = 0; channel < 2; ++channel) {
            const std::span<float> samples = buffer.channel(channel);
            for (std::uint32_t frame = 0; frame < length; ++frame) {
                samples[frame] = tone[at + frame];
            }
        }
        processor.process(buffer.block(length), length);
        // The last tenth of the run is what "settled" means.
        if (at > frames * 9 / 10) {
            for (std::uint32_t frame = 0; frame < length; ++frame) {
                loudest = std::max(loudest, std::fabs(static_cast<double>(buffer.channel(0)[frame])));
            }
        }
    }
    return 20.0 * std::log10(std::max(loudest, 1e-12));
}

} // namespace

TEST(DynamicsTest, TheCurveIsFlatBelowTheKneeAndBendsAboveIt)
{
    const DynamicsSettings settings = defaultSettings();

    // Well below the knee, nothing happens at all.
    EXPECT_FLOAT_EQ(outputDb(-60.0f, settings), -60.0f);
    EXPECT_FLOAT_EQ(outputDb(-26.01f, settings), -26.01f);

    // At the threshold, half the slope has been used up: the knee is
    // symmetrical about it.
    const float atThreshold = outputDb(-20.0f, settings);
    EXPECT_NEAR(atThreshold, -20.0f - 0.95f * 36.0f / 24.0f, 0.05f);

    // Well above, the straight part: a 20:1 ratio lets a twentieth through.
    EXPECT_NEAR(outputDb(0.0f, settings) - outputDb(-10.0f, settings), 0.5f, 0.01f);
}

TEST(DynamicsTest, TheKneeJoinsTheStraightPartsWithoutACorner)
{
    // A corner in the curve is a corner in the sound. The slope either side
    // of each join has to match.
    const DynamicsSettings settings = defaultSettings();
    const float step = 0.01f;
    for (const float at : {-26.0f, -14.0f}) {
        const float before = (outputDb(at - step, settings) - outputDb(at - 2.0f * step, settings)) / step;
        const float after = (outputDb(at + 2.0f * step, settings) - outputDb(at + step, settings)) / step;
        EXPECT_NEAR(before, after, 0.05f) << "the curve has a corner at " << at << " dB";
    }
}

TEST(DynamicsTest, AnInfiniteRatioIsALimiter)
{
    DynamicsSettings settings = defaultSettings();
    settings.ratio = kInfiniteRatio;
    settings.kneeDb = 0.0f;

    EXPECT_FLOAT_EQ(outputDb(-30.0f, settings), -30.0f);
    EXPECT_NEAR(outputDb(0.0f, settings), -20.0f, 0.01f);
    EXPECT_NEAR(outputDb(20.0f, settings), -20.0f, 0.01f) << "nothing should get past the threshold";
}

TEST(DynamicsTest, TheExpanderPushesTheQuietDownButOnlySoFar)
{
    DynamicsSettings settings = defaultSettings();
    settings.expanderThresholdDb = -50.0f;
    settings.expanderRatio = 3.0f;
    settings.expanderRangeDb = 20.0f;

    // Above its threshold the expander is not there.
    EXPECT_FLOAT_EQ(outputDb(-40.0f, settings), -40.0f);
    // Ten below: twice as far again, because 3:1 takes two more decibels
    // for every one.
    EXPECT_NEAR(outputDb(-60.0f, settings), -80.0f, 0.01f);
    // **The range is the floor.** Far below, it stops taking more off, so
    // the room is still there rather than a hole where it was.
    EXPECT_NEAR(outputDb(-100.0f, settings), -120.0f, 0.01f);
    EXPECT_NEAR(outputDb(-110.0f, settings), -130.0f, 0.01f);
}

TEST(DynamicsTest, MakeupGainMovesTheWholeCurve)
{
    DynamicsSettings settings = defaultSettings();
    const float without = outputDb(-30.0f, settings);
    settings.makeupDb = 6.0f;
    EXPECT_NEAR(outputDb(-30.0f, settings), without + 6.0f, 0.001f);
}

TEST(DynamicsTest, WhatComesOutMatchesTheCurveAtEveryLevel)
{
    // The criterion the whole block rests on: the curve editor draws
    // outputDb() and the sound has to agree with it.
    const DynamicsSettings settings = defaultSettings();
    DynamicsTiming timing;
    timing.attackMs = 1.0f;
    timing.releaseMs = 50.0f;

    for (float inputDb = -80.0f; inputDb <= 20.0f; inputDb += 5.0f) {
        DynamicsProcessor processor;
        processor.prepare(kRate, 2);
        processor.setSettings(settings);
        processor.setTiming(timing);

        const double measured = settledOutputDb(processor, dbToGain(inputDb));
        EXPECT_NEAR(measured, outputDb(inputDb, settings), 0.2)
            << "a steady " << inputDb << " dBFS sine did not come out where the curve says";
    }
}

TEST(DynamicsTest, TheAttackAndReleaseTakeTheTimeTheControlsSay)
{
    // At every rate the engine runs at, because a time constant that came
    // out of a coefficient rather than a clock would drift with the rate.
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        DynamicsSettings settings = defaultSettings();
        settings.kneeDb = 0.0f;
        settings.ratio = kInfiniteRatio;
        DynamicsTiming timing;
        timing.attackMs = 10.0f;
        timing.releaseMs = 300.0f;

        DynamicsProcessor processor;
        processor.prepare(rate, 2);
        processor.setSettings(settings);
        processor.setTiming(timing);

        // A step from the threshold to 20 dB above it: the reduction has to
        // travel 20 dB, and the attack is how long it takes to cover
        // 1 - 1/e of that.
        AudioBuffer buffer(2, 1);
        const float loud = dbToGain(0.0f);
        const float wanted = -20.0f * (1.0f - std::exp(-1.0f));
        std::uint32_t samples = 0;
        for (; samples < static_cast<std::uint32_t>(rate); ++samples) {
            buffer.channel(0)[0] = loud;
            buffer.channel(1)[0] = loud;
            processor.process(buffer.block(1), 1);
            if (processor.gainReduction() <= wanted) {
                break;
            }
        }
        const double attackMs = 1000.0 * samples / rate;
        EXPECT_NEAR(attackMs, timing.attackMs, timing.attackMs * 0.1) << "attack at " << rate << " Hz";

        // **And back down to another level, not to silence.** A step into
        // silence measures the detector's window as well as the release;
        // between two levels the detector has settled in a few milliseconds
        // and what is left is the release itself.
        const float quieter = dbToGain(-10.0f);
        for (std::uint32_t settle = 0; settle < static_cast<std::uint32_t>(rate / 20); ++settle) {
            buffer.channel(0)[0] = loud;
            buffer.channel(1)[0] = loud;
            processor.process(buffer.block(1), 1);
        }
        const float from = processor.gainReduction();
        const float to = -10.0f; // an infinite ratio at −10 dBFS holds it back by ten
        const float back = from + (to - from) * (1.0f - std::exp(-1.0f));
        samples = 0;
        for (; samples < static_cast<std::uint32_t>(rate); ++samples) {
            buffer.channel(0)[0] = quieter;
            buffer.channel(1)[0] = quieter;
            processor.process(buffer.block(1), 1);
            if (processor.gainReduction() >= back) {
                break;
            }
        }
        const double releaseMs = 1000.0 * samples / rate;
        EXPECT_NEAR(releaseMs, timing.releaseMs, timing.releaseMs * 0.1) << "release at " << rate << " Hz";
    }
}

TEST(DynamicsTest, BothSidesAreHeldBackTogether)
{
    // Only the left side is loud. If the right side were left alone the
    // image would swing across the room every time something peaked.
    DynamicsSettings settings = defaultSettings();
    settings.kneeDb = 0.0f;
    settings.ratio = 4.0f;

    DynamicsProcessor processor;
    processor.prepare(kRate, 2);
    processor.setSettings(settings);

    AudioBuffer buffer(2, 256);
    for (int cycle = 0; cycle < 200; ++cycle) {
        for (std::uint32_t frame = 0; frame < 256; ++frame) {
            buffer.channel(0)[frame] = 0.9f;
            buffer.channel(1)[frame] = 0.9f * dbToGain(-20.0f);
        }
        processor.process(buffer.block(256), 256);
    }

    // The same gain was applied to both, so the difference between the
    // sides is the difference it went in with.
    const double left = 20.0 * std::log10(std::fabs(buffer.channel(0)[255]));
    const double right = 20.0 * std::log10(std::fabs(buffer.channel(1)[255]));
    EXPECT_NEAR(left - right, 20.0, 0.01) << "the two sides were given different gains";
}

TEST(DynamicsTest, TheExpanderHoldsOpenSoItDoesNotChatter)
{
    // A signal sitting on the expander's threshold, dipping under it for a
    // moment at a time. Without a hold the gate opens and shuts over and
    // over, which is the noise a hold exists to stop.
    DynamicsSettings settings = defaultSettings();
    settings.expanderThresholdDb = -30.0f;
    settings.expanderRatio = 4.0f;
    settings.expanderRangeDb = 40.0f;
    DynamicsTiming timing;
    timing.attackMs = 1.0f;
    timing.releaseMs = 20.0f;
    timing.holdMs = 100.0f;

    DynamicsProcessor processor;
    processor.prepare(kRate, 2);
    processor.setSettings(settings);
    processor.setTiming(timing);

    AudioBuffer buffer(2, 480); // 10 ms a cycle
    const float above = dbToGain(-28.0f);
    const float below = dbToGain(-34.0f);
    float deepest = 0.0f;
    for (int cycle = 0; cycle < 40; ++cycle) {
        // Two cycles above, one below, over and over: the dips are shorter
        // than the hold.
        const float level = cycle % 3 == 2 ? below : above;
        for (std::uint32_t frame = 0; frame < 480; ++frame) {
            buffer.channel(0)[frame] = level;
            buffer.channel(1)[frame] = level;
        }
        processor.process(buffer.block(480), 480);
        if (cycle > 3) {
            deepest = std::min(deepest, processor.gainReduction());
        }
    }
    // The hold keeps it open: a few decibels of movement, not forty.
    EXPECT_GT(deepest, -6.0f) << "the expander shut during a dip shorter than its hold";
}

TEST(DynamicsTest, TenMinutesOfSilenceCostNothingAndProduceNothing)
{
    DynamicsSettings settings = defaultSettings();
    settings.expanderThresholdDb = -60.0f;
    settings.expanderRatio = 10.0f;
    settings.expanderRangeDb = 60.0f;
    DynamicsTiming timing;
    timing.detector = DynamicsDetector::Rms;

    // Ten minutes twice: once of silence, once of a tone. **What is
    // compared is the two against each other**, not either against a
    // stopwatch. A denormal storm is a run that gets slower the quieter the
    // signal is, and saying that as a ratio makes the answer the code's
    // rather than the build's — a sanitizer instruments every call, and a
    // budget measured under one is a budget for the instrumentation.
    const auto tenMinutesOf = [&](float amplitude) {
        DynamicsProcessor processor;
        processor.prepare(kRate, 2);
        processor.setSettings(settings);
        processor.setTiming(timing);

        AudioBuffer buffer(2, 512);
        const Signal tone = sine(512, 220.0, kRate, amplitude);
        const auto cycles = static_cast<int>(kRate * 600 / 512);
        const auto started = std::chrono::steady_clock::now();
        for (int cycle = 0; cycle < cycles; ++cycle) {
            for (std::uint32_t channel = 0; channel < 2; ++channel) {
                std::ranges::copy(tone, buffer.channel(channel).begin());
            }
            processor.process(buffer.block(512), 512);
        }
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

        for (std::uint32_t channel = 0; channel < 2; ++channel) {
            for (const float sample : buffer.channel(channel)) {
                EXPECT_FALSE(std::isnan(sample)) << "a NaN came out";
            }
        }
        return took;
    };

    const double quiet = tenMinutesOf(0.0f);
    const double loud = tenMinutesOf(0.5f);
    EXPECT_LT(quiet, loud * 1.5) << "ten minutes of silence took " << quiet << " s against " << loud
                                 << " s of tone: the quiet is costing more than the sound, which is what a "
                                    "denormal storm looks like";
}

TEST(DynamicsTest, ACycleCostsLessThanHalfAPerCentOfACore)
{
    for (const DynamicsDetector detector : {DynamicsDetector::Rms, DynamicsDetector::Peak}) {
        DynamicsSettings settings = defaultSettings();
        DynamicsTiming timing;
        timing.detector = detector;

        DynamicsProcessor processor;
        processor.prepare(kRate, 2);
        processor.setSettings(settings);
        processor.setTiming(timing);

        // A minute of a loud sine, which is the expensive case: the curve is
        // being worked out for every sample.
        const auto frames = static_cast<std::uint32_t>(kRate * 60);
        const Signal tone = sine(4800, 1000.0, kRate, 0.9f);
        AudioBuffer buffer(2, 480);
        const auto started = std::chrono::steady_clock::now();
        for (std::uint32_t at = 0; at < frames; at += 480) {
            for (std::uint32_t channel = 0; channel < 2; ++channel) {
                const std::span<float> samples = buffer.channel(channel);
                for (std::uint32_t frame = 0; frame < 480; ++frame) {
                    samples[frame] = tone[(at + frame) % tone.size()];
                }
            }
            processor.process(buffer.block(480), 480);
        }
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        const double share = took / 60.0;
        std::cerr << "dynamics (" << (detector == DynamicsDetector::Peak ? "peak" : "rms") << "): a minute of audio in "
                  << took << " s, " << share * 100.0 << " % of a core\n";
#ifdef NDEBUG
        EXPECT_LT(share, 0.005) << "a minute of audio took " << took << " s, which is " << share * 100.0
                                << " % of a core";
#endif
    }
#ifndef NDEBUG
    // **Only the optimised build has anything to say about this.** A debug
    // build is several times slower, and a budget measured against it would
    // be a budget for the compiler rather than for the code.
    GTEST_SKIP() << "the figures above are from a debug build";
#endif
}

TEST(DynamicsTest, ItSoundsTheSameWhateverTheCycleLength)
{
    // The quantum changes while a session runs; nothing here may be kept
    // per cycle.
    const DynamicsSettings settings = defaultSettings();
    const Signal tone = sine(48000, 220.0, kRate, 0.8f);

    const auto run = [&](std::uint32_t cycle) {
        DynamicsProcessor processor;
        processor.prepare(kRate, 2);
        processor.setSettings(settings);
        AudioBuffer buffer(2, 2048);
        Signal out;
        for (std::size_t at = 0; at < tone.size(); at += cycle) {
            const auto length = static_cast<std::uint32_t>(std::min<std::size_t>(cycle, tone.size() - at));
            for (std::uint32_t channel = 0; channel < 2; ++channel) {
                const std::span<float> samples = buffer.channel(channel);
                for (std::uint32_t frame = 0; frame < length; ++frame) {
                    samples[frame] = tone[at + frame];
                }
            }
            processor.process(buffer.block(length), length);
            out.insert(out.end(), buffer.channel(0).begin(), buffer.channel(0).begin() + length);
        }
        return out;
    };

    EXPECT_LE(nullTestDb(run(64), run(1024)), kSilenceDb);
}

TEST(DynamicsTest, ThePeakDetectorReadsATonesPeakBetweenItsSamplesToo)
{
    // **A tone's samples need not touch its peak.** At a quarter of the
    // rate a sine lands on the same four points every cycle; started half
    // way between them, all four sit at 0.707 of the peak, and a detector
    // that reads samples takes the tone for 3 dB quieter than it is. A
    // third or a sixth of the rate does the same by 1.25 dB. Every tone
    // here starts at the phase that keeps its samples lowest, and a limiter
    // curve has to bring each one down to its threshold, not above it.
    DynamicsSettings settings;
    settings.thresholdDb = -20.0f;
    settings.ratio = kInfiniteRatio;
    settings.kneeDb = 0.0f;
    DynamicsTiming timing;
    timing.detector = DynamicsDetector::Peak;
    timing.attackMs = 1.0f;
    timing.releaseMs = 50.0f;
    const float inputDb = -6.0f;

    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        for (const double hz : {1000.0, 5000.0, 10000.0, 15000.0, 20000.0, rate / 3.0, rate / 4.0, rate / 6.0}) {
            if (hz > 20000.0) {
                continue;
            }
            // The starting phase whose samples come nearest to nothing at
            // the top: searched, so the test does not depend on working
            // out which phase is worst for which ratio.
            double worstPhase = 0.0;
            double lowestTop = 2.0;
            for (int step = 0; step < 256; ++step) {
                const double phase = std::numbers::pi * step / 256.0;
                double top = 0.0;
                for (int frame = 0; frame < 64; ++frame) {
                    top = std::max(top, std::fabs(std::sin(phase + 2.0 * std::numbers::pi * hz * frame / rate)));
                }
                if (top < lowestTop) {
                    lowestTop = top;
                    worstPhase = phase;
                }
            }

            DynamicsProcessor processor;
            processor.prepare(rate, 2);
            processor.setSettings(settings);
            processor.setTiming(timing);
            const auto frames = static_cast<std::uint32_t>(rate * 0.6);
            const Signal tone = sine(frames, hz, rate, dbToGain(inputDb), worstPhase);
            AudioBuffer buffer(2, 480);
            Signal out;
            for (std::uint32_t at = 0; at < frames; at += 480) {
                const std::uint32_t length = std::min<std::uint32_t>(480, frames - at);
                for (std::uint32_t channel = 0; channel < 2; ++channel) {
                    const std::span<float> samples = buffer.channel(channel);
                    for (std::uint32_t frame = 0; frame < length; ++frame) {
                        samples[frame] = tone[at + frame];
                    }
                }
                processor.process(buffer.block(length), length);
                out.insert(out.end(), buffer.channel(0).begin(), buffer.channel(0).begin() + length);
            }
            // A sampled sine's RMS is its amplitude over root two whatever
            // its phase, so the last tenth's RMS says where its peak is.
            const std::span<const float> settled(out.data() + out.size() * 9 / 10, out.size() / 10);
            const double peakDb = static_cast<double>(rmsDb(settled)) + 20.0 * std::log10(std::numbers::sqrt2);
            EXPECT_NEAR(peakDb, settings.thresholdDb, 0.25)
                << hz << " Hz at " << rate << " Hz came out with its peak at " << peakDb
                << " dBFS; its samples reach only " << 20.0 * std::log10(lowestTop) << " dB of it";
        }
    }
}

namespace
{

[[nodiscard]] DynamicsShape shapeOf(float expanderDb, float ratio, float thresholdDb, float heightDb)
{
    DynamicsShape shape;
    shape.expanderThresholdDb = expanderDb;
    shape.expanderRatio = ratio;
    shape.thresholdDb = thresholdDb;
    shape.heightDb = heightDb;
    return shape;
}

} // namespace

TEST(DynamicsShapeTest, TheCurveIsTheTableWorkedOutSeparately)
{
    // Worked out on the side in thirty-digit arithmetic, from the words: the
    // line under the expander threshold, the diagonal, and the bend
    // threshold + height * (1 - exp(-over / height)).
    struct Row {
        float expanderDb;
        float ratio;
        float thresholdDb;
        float heightDb;
        float inputDb;
        float outputDb;
    };
    constexpr std::array rows{
        Row{-80.0f, 1.0f, -26.0f, 7.0f, -110.0f, -110.0f},   Row{-80.0f, 1.0f, -26.0f, 7.0f, -30.0f, -30.0f},
        Row{-80.0f, 1.0f, -26.0f, 7.0f, -20.0f, -21.9706f},  Row{-80.0f, 1.0f, -26.0f, 7.0f, -10.0f, -19.7119f},
        Row{-80.0f, 1.0f, -26.0f, 7.0f, 0.0f, -19.1706f},    Row{-80.0f, 1.0f, -26.0f, 7.0f, 40.0f, -19.0006f},
        Row{-60.0f, 2.0f, -20.0f, 12.0f, -110.0f, -120.0f},  Row{-60.0f, 2.0f, -20.0f, 12.0f, -90.0f, -120.0f},
        Row{-60.0f, 2.0f, -20.0f, 12.0f, -70.0f, -80.0f},    Row{-60.0f, 2.0f, -20.0f, 12.0f, -50.0f, -50.0f},
        Row{-60.0f, 2.0f, -20.0f, 12.0f, -10.0f, -13.2152f}, Row{-60.0f, 2.0f, -20.0f, 12.0f, 0.0f, -10.2665f},
        Row{-60.0f, 2.0f, -20.0f, 12.0f, 40.0f, -8.0809f},   Row{-74.4f, 1.7f, -28.9f, 39.69f, -110.0f, -120.0f},
        Row{-74.4f, 1.7f, -28.9f, 39.69f, -90.0f, -100.92f}, Row{-74.4f, 1.7f, -28.9f, 39.69f, -20.0f, -20.9273f},
        Row{-74.4f, 1.7f, -28.9f, 39.69f, 0.0f, -8.3725f},   Row{-74.4f, 1.7f, -28.9f, 39.69f, 40.0f, 3.7953f},
        Row{-45.0f, 20.0f, 20.0f, 40.0f, -50.0f, -120.0f},   Row{-45.0f, 20.0f, 20.0f, 40.0f, -44.0f, -44.0f},
        Row{-45.0f, 20.0f, 20.0f, 40.0f, 10.0f, 10.0f},      Row{-45.0f, 20.0f, 20.0f, 40.0f, 30.0f, 28.848f},
        Row{-45.0f, 20.0f, 20.0f, 40.0f, 40.0f, 35.7388f},   Row{-80.0f, 1.0f, -3.0f, 0.0f, -10.0f, -10.0f},
        Row{-80.0f, 1.0f, -3.0f, 0.0f, 0.0f, -3.0f},         Row{-80.0f, 1.0f, -3.0f, 0.0f, 40.0f, -3.0f},
    };
    for (const Row &row : rows) {
        EXPECT_NEAR(outputDb(row.inputDb, shapeOf(row.expanderDb, row.ratio, row.thresholdDb, row.heightDb)),
                    row.outputDb,
                    0.001f)
            << "at " << row.inputDb << " dB in, expander " << row.expanderDb << " at " << row.ratio << ", threshold "
            << row.thresholdDb << ", height " << row.heightDb;
    }
}

TEST(DynamicsShapeTest, TheRatioIsTheSlopeOfTheLineFromTheFloor)
{
    // The two figures read off the graph's own readings at E = −74.4.
    EXPECT_NEAR(dynamicsRatioFor(-74.4f, -101.2f), 1.70f, 0.01f);
    EXPECT_NEAR(dynamicsRatioFor(-74.4f, -108.4f), 1.34f, 0.01f);

    // At the corner of the graph the line is the diagonal.
    EXPECT_FLOAT_EQ(dynamicsRatioFor(-74.4f, -120.0f), 1.0f);
    // And past it nothing changes.
    EXPECT_FLOAT_EQ(dynamicsRatioFor(-74.4f, -130.0f), 1.0f);

    // At the top the foot stops short of the expander threshold: nearly
    // upright, and never a division by nothing.
    EXPECT_FLOAT_EQ(dynamicsRatioFor(-74.4f, -74.4f), kDynamicsHighestRatio);
    EXPECT_FLOAT_EQ(dynamicsRatioFor(-74.4f, -60.0f), kDynamicsHighestRatio);

    // With the expander in the corner there is no line to be steep.
    EXPECT_FLOAT_EQ(dynamicsRatioFor(kDynamicsFloorDb, kDynamicsFloorDb), 1.0f);

    // The two ways round say the same thing.
    for (const float ratio : {1.0f, 1.7f, 4.0f, 16.0f, 50.0f}) {
        const DynamicsShape shape = shapeOf(-60.0f, ratio, -20.0f, 12.0f);
        EXPECT_NEAR(dynamicsRatioFor(expanderStartDb(shape), dynamicsCutoffDb(shape)), ratio, 0.001f * ratio);
        EXPECT_NEAR(outputDb(dynamicsCutoffDb(shape), shape), kDynamicsFloorDb, 0.01f);
    }
}

TEST(DynamicsShapeTest, ALineComesOutAboveTheFloorAndNothingBelowIt)
{
    const DynamicsShape shape = shapeOf(-60.0f, 4.0f, -20.0f, 12.0f);
    const float cutoff = dynamicsCutoffDb(shape);
    EXPECT_FLOAT_EQ(cutoff, -75.0f);

    EXPECT_NEAR(outputDb(cutoff + 1.0f, shape), -116.0f, 0.01f);
    for (const float inputDb : {cutoff, cutoff - 1.0f, -100.0f, -119.0f}) {
        EXPECT_FLOAT_EQ(outputDb(inputDb, shape), kDynamicsFloorDb) << "at " << inputDb << " dB in";
    }
    // **What is under the floor already is not raised to it.** The gain only
    // ever goes down, so digital silence stays what it is.
    EXPECT_FLOAT_EQ(gainReductionDb(-140.0f, shape), 0.0f);
}

TEST(DynamicsShapeTest, AHeightOfNothingIsAFlatLineAtTheThreshold)
{
    const DynamicsShape shape = shapeOf(-80.0f, 1.0f, -10.0f, 0.0f);
    EXPECT_FLOAT_EQ(outputDb(-10.0f, shape), -10.0f);
    for (const float inputDb : {-9.99f, 0.0f, 20.0f, 40.0f}) {
        EXPECT_FLOAT_EQ(outputDb(inputDb, shape), -10.0f) << "at " << inputDb << " dB in";
    }
}

TEST(DynamicsShapeTest, TheBendLeavesTheDiagonalWithoutACornerAndNeverComesBack)
{
    // A corner in the curve is a corner in the sound.
    for (const float heightDb : {1.0f, 7.0f, 40.0f}) {
        const DynamicsShape shape = shapeOf(-80.0f, 1.0f, -20.0f, heightDb);
        const float step = 0.01f;
        const float slope = (outputDb(-20.0f + 2.0f * step, shape) - outputDb(-20.0f + step, shape)) / step;
        EXPECT_NEAR(slope, 1.0f, 0.06f / heightDb + 0.03f) << "height " << heightDb;
    }

    // And each decibel above it is worth less than the last, and is still
    // worth something: it never turns over.
    const DynamicsShape shape = shapeOf(-80.0f, 1.0f, -20.0f, 12.0f);
    float before = outputDb(-20.0f, shape);
    float rise = 1.0f;
    for (float inputDb = -19.0f; inputDb <= 40.0f; inputDb += 1.0f) {
        const float now = outputDb(inputDb, shape);
        EXPECT_GE(now, before) << "at " << inputDb;
        EXPECT_LE(now - before, rise + 1e-3f) << "at " << inputDb;
        rise = now - before;
        before = now;
    }
}

TEST(DynamicsShapeTest, AHeightStaysWhatItIsAndTheCurveStaysUnderTheDiagonal)
{
    // At the top of the threshold's range a full height is more than there
    // is room for. The number is kept, and the curve keeps under the line.
    const DynamicsShape shape = shapeOf(-80.0f, 1.0f, kDynamicsHighestThresholdDb, kDynamicsHighestHeightDb);
    const float top = outputDb(kDynamicsTopDb, shape);
    EXPECT_LT(top, kDynamicsTopDb);
    EXPECT_NEAR(top, 35.7388f, 0.001f);
    EXPECT_FLOAT_EQ(shape.heightDb, kDynamicsHighestHeightDb);
}

TEST(DynamicsShapeTest, NoSettingPutsAnyOfTheCurveAboveTheDiagonal)
{
    // The owner's rule, across everything a session can hold: nothing rises
    // above the main line, and the curve only ever climbs.
    for (float expanderDb = -120.0f; expanderDb <= 20.0f; expanderDb += 14.0f) {
        for (const float ratio : {1.0f, 1.01f, 2.0f, 8.0f, 50.0f}) {
            for (float thresholdDb = -120.0f; thresholdDb <= 20.0f; thresholdDb += 14.0f) {
                for (const float heightDb : {0.0f, 1.0e-4f, 0.5f, 7.0f, 25.0f, 40.0f}) {
                    const DynamicsShape shape = shapeOf(expanderDb, ratio, thresholdDb, heightDb);
                    float before = -200.0f;
                    for (float inputDb = -120.0f; inputDb <= 40.0f; inputDb += 0.25f) {
                        const float out = outputDb(inputDb, shape);
                        ASSERT_TRUE(std::isfinite(out));
                        ASSERT_LE(out, inputDb)
                            << expanderDb << " " << ratio << " " << thresholdDb << " " << heightDb << " at " << inputDb;
                        ASSERT_GE(out, kDynamicsFloorDb);
                        ASSERT_GE(out, before - 1e-3f) << "the curve went down at " << inputDb;
                        before = out;
                    }
                }
            }
        }
    }
}

TEST(DynamicsShapeTest, AnExpanderThresholdAboveTheThresholdStopsThere)
{
    // A session may hold the two in the wrong order; the curve may not.
    const DynamicsShape shape = shapeOf(-10.0f, 3.0f, -30.0f, 6.0f);
    EXPECT_FLOAT_EQ(expanderStartDb(shape), -30.0f);
    EXPECT_FLOAT_EQ(outputDb(-30.0f, shape), -30.0f);
    EXPECT_FLOAT_EQ(outputDb(-40.0f, shape), -60.0f);
}

TEST(DynamicsShapeTest, AHeightCanBeFoundFromWhereTheCurveEnds)
{
    for (const float thresholdDb : {-100.0f, -34.1f, -9.0f, 0.0f, 19.0f}) {
        for (const float heightDb : {0.25f, 1.0f, 6.52f, 19.3f, 33.0f}) {
            const DynamicsShape shape = shapeOf(-120.0f, 1.0f, thresholdDb, heightDb);
            EXPECT_NEAR(dynamicsHeightFor(thresholdDb, kDynamicsTopDb, outputDb(kDynamicsTopDb, shape)),
                        heightDb,
                        0.002f + heightDb * 0.002f)
                << "threshold " << thresholdDb;
        }
    }
    // Lower than the threshold it cannot go, and past the most a height can
    // reach it stops there.
    EXPECT_FLOAT_EQ(dynamicsHeightFor(-34.0f, kDynamicsTopDb, -40.0f), 0.0f);
    EXPECT_FLOAT_EQ(dynamicsHeightFor(-34.0f, kDynamicsTopDb, 39.0f), kDynamicsHighestHeightDb);
    EXPECT_FLOAT_EQ(dynamicsHeightFor(kDynamicsTopDb, kDynamicsTopDb, 39.0f), 0.0f);
}

TEST(DynamicsShapeTest, WhatComesOutMatchesTheCurveAtEveryLevel)
{
    // The criterion the whole block rests on, for this curve as well.
    const DynamicsShape shape = shapeOf(-60.0f, 2.0f, -24.0f, 12.0f);
    DynamicsTiming timing;
    timing.attackMs = 1.0f;
    timing.releaseMs = 50.0f;

    for (const float inputDb : {-80.0f, -70.0f, -60.0f, -50.0f, -30.0f, -20.0f, -10.0f, 0.0f, 10.0f}) {
        ShapedDynamicsProcessor processor;
        processor.prepare(kRate, 2);
        processor.setSettings(shape);
        processor.setTiming(timing);

        const auto frames = static_cast<std::uint32_t>(kRate);
        const Signal tone = sine(frames, 1000.0, kRate, dbToGain(inputDb));
        AudioBuffer buffer(2, 512);
        double loudest = 0.0;
        for (std::uint32_t at = 0; at < frames; at += 512) {
            const std::uint32_t length = std::min<std::uint32_t>(512, frames - at);
            for (std::uint32_t channel = 0; channel < 2; ++channel) {
                std::copy_n(tone.begin() + at, length, buffer.channel(channel).begin());
            }
            processor.process(buffer.block(length), length);
            if (at > frames * 9 / 10) {
                for (std::uint32_t frame = 0; frame < length; ++frame) {
                    loudest = std::max(loudest, std::fabs(static_cast<double>(buffer.channel(0)[frame])));
                }
            }
        }
        EXPECT_NEAR(20.0 * std::log10(std::max(loudest, 1e-12)), outputDb(inputDb, shape), 0.25)
            << "a steady " << inputDb << " dBFS sine did not come out where the curve says";
    }
}

TEST(DynamicsShapeTest, AGateHoldsOpenForAsLongAsItIsTold)
{
    // Steep, with its foot just under the threshold: anything under it is
    // gone, except for as long as the hold keeps it open.
    const DynamicsShape shape = shapeOf(-30.0f, 50.0f, 20.0f, 40.0f);
    DynamicsTiming timing;
    timing.attackMs = 1.0f;
    timing.releaseMs = 20.0f;
    timing.holdMs = 100.0f;

    ShapedDynamicsProcessor processor;
    processor.prepare(kRate, 2);
    processor.setSettings(shape);
    processor.setTiming(timing);

    AudioBuffer buffer(2, 480);
    const auto play = [&](float amplitude, int cycles) {
        for (int cycle = 0; cycle < cycles; ++cycle) {
            for (std::uint32_t channel = 0; channel < 2; ++channel) {
                std::ranges::fill(buffer.channel(channel), amplitude);
            }
            processor.process(buffer.block(480), 480);
        }
    };
    play(dbToGain(-20.0f), 10);
    EXPECT_FLOAT_EQ(processor.gainReduction(), 0.0f);

    play(dbToGain(-40.0f), 5); // 50 ms, inside the hold
    EXPECT_GT(processor.gainReduction(), -1.0f) << "the gate shut during its hold";

    play(dbToGain(-40.0f), 20); // 200 ms more, well past it
    EXPECT_LT(processor.gainReduction(), -60.0f) << "the gate never shut";
}

TEST(DynamicsShapeTest, ACycleCostsLessThanHalfAPerCentOfACore)
{
    // Above the threshold, which is the expensive part of this curve: the
    // bend is worked out for every sample.
    for (const DynamicsDetector detector : {DynamicsDetector::Rms, DynamicsDetector::Peak}) {
        DynamicsTiming timing;
        timing.detector = detector;

        ShapedDynamicsProcessor processor;
        processor.prepare(kRate, 2);
        processor.setSettings(shapeOf(-60.0f, 2.0f, -40.0f, 12.0f));
        processor.setTiming(timing);

        const auto frames = static_cast<std::uint32_t>(kRate * 60);
        const Signal tone = sine(4800, 1000.0, kRate, 0.9f);
        AudioBuffer buffer(2, 480);
        const auto started = std::chrono::steady_clock::now();
        for (std::uint32_t at = 0; at < frames; at += 480) {
            for (std::uint32_t channel = 0; channel < 2; ++channel) {
                const std::span<float> samples = buffer.channel(channel);
                for (std::uint32_t frame = 0; frame < 480; ++frame) {
                    samples[frame] = tone[(at + frame) % tone.size()];
                }
            }
            processor.process(buffer.block(480), 480);
        }
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        const double share = took / 60.0;
        std::cerr << "shaped dynamics (" << (detector == DynamicsDetector::Peak ? "peak" : "rms")
                  << "): " << share * 100.0 << " % of a core\n";
#ifdef NDEBUG
        EXPECT_LT(share, 0.005) << "a minute of audio took " << took << " s";
#endif
    }
#ifndef NDEBUG
    GTEST_SKIP() << "the figures above are from a debug build";
#endif
}
