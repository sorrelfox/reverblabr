// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The tracker behind Dehum's Auto: it locks onto a hum within a
// second, follows it while it drifts, holds when there is none, and looks
// only between 48 and 62 Hz.

#include "dsp/humtracker.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <numbers>
#include <random>
#include <vector>

using namespace rb;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr double kRate = 48000.0;

/// Hum: a base following \a frequency(t) and its first ten harmonics, each
/// at \a levelDb, sounding from \a on to \a off seconds.
[[nodiscard]] Signal hum(std::size_t frames,
                         const std::function<double(double)> &frequency,
                         double levelDb = -20.0,
                         double on = 0.0,
                         double off = 1.0e9,
                         double rate = kRate)
{
    Signal signal(frames, 0.0f);
    const double amplitude = std::pow(10.0, levelDb / 20.0);
    double phase = 0.0;
    for (std::size_t n = 0; n < frames; ++n) {
        const double t = static_cast<double>(n) / rate;
        phase += 2.0 * std::numbers::pi * frequency(t) / rate;
        if (t < on || t >= off) {
            continue;
        }
        double sum = 0.0;
        for (int number = 1; number <= 10; ++number) {
            sum += std::cos(number * phase + 0.3 * number);
        }
        signal[n] = static_cast<float>(amplitude * sum);
    }
    return signal;
}

[[nodiscard]] std::function<double(double)> steady(double hz)
{
    return [hz](double) { return hz; };
}

/// Runs \a input through a tracker started at \a startHz, calling \a each
/// with the time, the estimate and whether it is locked, every sample.
void track(HumTracker &tracker,
           const Signal &input,
           const std::function<void(double, double, bool)> &each,
           double rate = kRate)
{
    for (std::size_t n = 0; n < input.size(); ++n) {
        tracker.push(input[n]);
        each(static_cast<double>(n) / rate, tracker.estimate(), tracker.locked());
    }
}

} // namespace

TEST(HumTrackerTest, ItLocksWithinASecondOfTheHumAppearing)
{
    // **The acceptance criterion.** A second of silence, then hum up to
    // 0.9 Hz from where it starts looking (a 50 Hz supply running off, a
    // 60 Hz one): locked, and within 10 mHz, inside a second of the hum
    // appearing.
    for (const auto &[start, actual] :
         std::vector<std::pair<double, double>>{{50.0, 50.9}, {50.0, 49.1}, {60.0, 59.2}, {60.0, 60.8}}) {
        HumTracker tracker;
        tracker.prepare(kRate);
        tracker.reset(start);
        const Signal input = hum(static_cast<std::size_t>(3.0 * kRate), steady(actual), -20.0, 1.0);
        double lockedAt = -1.0;
        track(tracker, input, [&](double t, double estimate, bool locked) {
            if (lockedAt < 0.0 && t >= 1.0 && locked && std::abs(estimate - actual) < 0.01) {
                lockedAt = t - 1.0;
            }
        });
        std::printf(
            "[measure] %.1f Hz hum, looking from %.0f: locked within 10 mHz after %.2f s\n", actual, start, lockedAt);
        EXPECT_GE(lockedAt, 0.0) << actual << " Hz from " << start << " never locked";
        EXPECT_LT(lockedAt, 1.0) << actual << " Hz from " << start;
        EXPECT_NEAR(tracker.estimate(), actual, 0.005) << actual << " from " << start;
    }
}

TEST(HumTrackerTest, ItFollowsAHumThatDrifts)
{
    // Locked on 49.5 Hz, then the base drifts to 50.5 over ten seconds:
    // never more than 16 mHz behind or ahead, the loop's settling as the
    // drift sets off included, and once under way within 5 mHz. A loop like
    // this one would trail a steady drift by 2ζr/ωn, about 10 mHz here; the
    // estimate makes that up from how fast the loop's integral is moving.
    HumTracker tracker;
    tracker.prepare(kRate);
    tracker.reset(50.0);
    const auto frequency = [](double t) { return 49.5 + std::clamp((t - 2.0) / 10.0, 0.0, 1.0); };
    const Signal input = hum(static_cast<std::size_t>(12.0 * kRate), frequency);
    double worst = 0.0;
    double underWay = 0.0;
    bool alwaysLocked = true;
    track(tracker, input, [&](double t, double estimate, bool locked) {
        if (t >= 2.0) {
            worst = std::max(worst, std::abs(estimate - frequency(t)));
            alwaysLocked = alwaysLocked && locked;
        }
        if (t >= 6.0) {
            underWay = std::max(underWay, std::abs(estimate - frequency(t)));
        }
    });
    std::printf("[measure] a drift of 0.1 Hz/s: followed within %.1f mHz, %.1f mHz once under way\n",
                worst * 1000.0,
                underWay * 1000.0);
    EXPECT_TRUE(alwaysLocked);
    EXPECT_LT(worst, 0.016);
    EXPECT_LT(underWay, 0.005);
}

TEST(HumTrackerTest, WithNoHumItHoldsAndNeverLocks)
{
    // Silence, noise, and a bass line of short notes in the hum's range:
    // nothing steady at the base, so the estimate stays where it was put,
    // to the last bit, and the tracker never claims a lock.
    std::vector<Signal> inputs;
    inputs.push_back(silence(static_cast<std::size_t>(5.0 * kRate)));
    for (std::uint32_t seed = 1; seed <= 4; ++seed) {
        inputs.push_back(pinkNoise(static_cast<std::size_t>(10.0 * kRate), 0.5f, seed));
    }
    {
        // A note every quarter second, anywhere from 40 to 70 Hz, over noise.
        Signal bass = pinkNoise(static_cast<std::size_t>(10.0 * kRate), 0.1f, 5);
        std::mt19937 random(4);
        std::uniform_real_distribution<double> note(40.0, 70.0);
        const auto length = static_cast<std::size_t>(0.25 * kRate);
        for (std::size_t from = 0; from < bass.size(); from += length) {
            const double hz = note(random);
            for (std::size_t n = from; n < std::min(from + length, bass.size()); ++n) {
                bass[n] +=
                    0.3f * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(n) / kRate));
            }
        }
        inputs.push_back(std::move(bass));
    }
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        HumTracker tracker;
        tracker.prepare(kRate);
        tracker.reset(50.0);
        bool everLocked = false;
        double wander = 0.0;
        track(tracker, inputs[index], [&](double, double estimate, bool locked) {
            everLocked = everLocked || locked;
            wander = std::max(wander, std::abs(estimate - 50.0));
        });
        EXPECT_FALSE(everLocked) << "input " << index;
        EXPECT_EQ(wander, 0.0) << "input " << index << " moved the estimate by " << wander << " Hz";
    }
}

TEST(HumTrackerTest, WhenTheHumStopsItHoldsWhereItWas)
{
    // Hum at 50.4 Hz, heard through quiet noise, for two and a half
    // seconds, then only the noise: from a moment after the hum stops, the
    // estimate does not move at all, and within half a second the tracker
    // says it is holding.
    HumTracker tracker;
    tracker.prepare(kRate);
    tracker.reset(50.0);
    Signal input = hum(static_cast<std::size_t>(8.0 * kRate), steady(50.4), -20.0, 0.5, 3.0);
    const Signal noise = pinkNoise(input.size(), 0.05f, 9);
    for (std::size_t n = 0; n < input.size(); ++n) {
        input[n] += noise[n];
    }
    double jitter = 0.0;
    double atStop = 0.0;
    double moved = 0.0;
    bool lockedAfter = false;
    track(tracker, input, [&](double t, double estimate, bool locked) {
        if (t >= 2.0 && t < 3.0) {
            jitter = std::max(jitter, std::abs(estimate - 50.4));
        }
        // The fundamental's filter takes a few tens of milliseconds to
        // empty; the estimate stops following as it does, and the tracker
        // lets go of the lock once the averaged power has fallen too.
        if (t < 3.15) {
            atStop = estimate;
        } else {
            moved = std::max(moved, std::abs(estimate - atStop));
        }
        if (t >= 3.5) {
            lockedAfter = lockedAfter || locked;
        }
    });
    std::printf(
        "[measure] hum at -20 dB through noise at 0.05: within %.1f mHz while it lasted; "
        "held at %.4f Hz after, moving %.3g Hz\n",
        jitter * 1000.0,
        atStop,
        moved);
    EXPECT_NEAR(atStop, 50.4, 0.05);
    EXPECT_EQ(moved, 0.0);
    EXPECT_FALSE(lockedAfter);
}

TEST(HumTrackerTest, HeardThroughNoiseItsHarmonicsSteadyIt)
{
    // **The cross-check.** A hum at -20 dB through pink noise: the 2nd and
    // 3rd harmonics, where the noise is quieter than at the base, help keep
    // the estimate within 50 mHz once locked.
    HumTracker tracker;
    tracker.prepare(kRate);
    tracker.reset(50.0);
    Signal input = hum(static_cast<std::size_t>(8.0 * kRate), steady(50.3));
    const Signal noise = pinkNoise(input.size(), 0.05f, 9);
    for (std::size_t n = 0; n < input.size(); ++n) {
        input[n] += noise[n];
    }
    double worst = 0.0;
    track(tracker, input, [&](double t, double estimate, bool) {
        if (t >= 3.0) {
            worst = std::max(worst, std::abs(estimate - 50.3));
        }
    });
    std::printf("[measure] hum at -20 dB through pink noise at 0.05: within %.1f mHz\n", worst * 1000.0);
    EXPECT_TRUE(tracker.locked());
    EXPECT_LT(worst, 0.05);
}

TEST(HumTrackerTest, ATonePlayingNextToAHarmonicDoesNotPullIt)
{
    // A tone 25 Hz above the 3rd harmonic, louder than the hum: each
    // harmonic's say is weighted by its power averaged over a tenth of a
    // second, not by the power of the moment, which swells with the tone as
    // its phase turns one way and would pull the estimate towards it.
    HumTracker tracker;
    tracker.prepare(kRate);
    tracker.reset(50.0);
    Signal input = hum(static_cast<std::size_t>(6.0 * kRate), steady(50.3), -30.0);
    const Signal tone = sine(input.size(), 176.1, kRate, 0.03f);
    for (std::size_t n = 0; n < input.size(); ++n) {
        input[n] += tone[n];
    }
    double worst = 0.0;
    track(tracker, input, [&](double t, double estimate, bool) {
        if (t >= 3.0) {
            worst = std::max(worst, std::abs(estimate - 50.3));
        }
    });
    std::printf("[measure] a tone next to the 3rd harmonic: within %.1f mHz\n", worst * 1000.0);
    EXPECT_TRUE(tracker.locked());
    EXPECT_LT(worst, 0.015);
}

TEST(HumTrackerTest, ItLooksOnlyBetween48And62Hz)
{
    for (const double actual : {45.0, 65.0}) {
        HumTracker tracker;
        tracker.prepare(kRate);
        tracker.reset(actual < 50.0 ? 48.5 : 61.5);
        track(tracker, hum(static_cast<std::size_t>(4.0 * kRate), steady(actual)), [&](double, double estimate, bool) {
            EXPECT_GE(estimate, HumTracker::kLowestHz);
            EXPECT_LE(estimate, HumTracker::kHighestHz);
        });
        EXPECT_GE(tracker.loopHz(), HumTracker::kLowestHz);
        EXPECT_LE(tracker.loopHz(), HumTracker::kHighestHz);
    }
}

TEST(HumTrackerTest, AimingItElsewhereGlides)
{
    // Holding at 50, then told to look from 60: the estimate glides there,
    // never jumping more than a small part of the way in one update.
    HumTracker tracker;
    tracker.prepare(kRate);
    tracker.reset(50.0);
    double previous = tracker.estimate();
    tracker.aim(60.0);
    EXPECT_EQ(tracker.estimate(), 50.0) << "aiming moved the estimate before a single sample";
    double biggestStep = 0.0;
    for (int n = 0; n < static_cast<int>(0.3 * kRate); ++n) {
        tracker.push(0.0f);
        biggestStep = std::max(biggestStep, std::abs(tracker.estimate() - previous));
        previous = tracker.estimate();
    }
    EXPECT_NEAR(tracker.estimate(), 60.0, 1.0e-3);
    EXPECT_LT(biggestStep, 0.2) << "the estimate jumped " << biggestStep << " Hz at once";
}

TEST(HumTrackerTest, ItWorksAtEveryRate)
{
    for (const double rate : {44100.0, 96000.0}) {
        HumTracker tracker;
        tracker.prepare(rate);
        tracker.reset(50.0);
        const Signal input = hum(static_cast<std::size_t>(3.0 * rate), steady(50.6), -20.0, 0.0, 1.0e9, rate);
        track(tracker, input, [](double, double, bool) { }, rate);
        EXPECT_TRUE(tracker.locked()) << rate;
        EXPECT_NEAR(tracker.estimate(), 50.6, 0.005) << rate;
    }
}
