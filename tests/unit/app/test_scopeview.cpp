// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What a surface draws from the sound it was given: which stretch, in which
// columns, at what height, and how that moves. Real-time and Smooth have to
// differ in numbers before they can differ to the eye, so the two are run
// here against a clock the test winds.

#include "scopeview.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <numeric>
#include <vector>

using namespace rb;
using namespace rb::ui;

namespace
{

constexpr double kRate = 48000.0;
constexpr std::size_t kColumns = 400;
/// A screen's frame, and a slow refresh, in milliseconds.
constexpr double kFrameMs = 1000.0 / 60.0;
constexpr double kRefreshMs = 100.0;

using Sound = std::function<float(double frame)>;

[[nodiscard]] Sound toneOf(double hz, double amplitude = 0.5)
{
    return [=](double frame) {
        return static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * hz * frame / kRate));
    };
}

/// The sound as a window would have copied it, up to \a newest.
[[nodiscard]] ScopeFrame frameUpTo(const Sound &sound, double newest, std::size_t points, std::uint64_t serial)
{
    ScopeFrame frame;
    frame.sampleRate = static_cast<std::uint32_t>(kRate);
    frame.framesPerPoint = 1;
    frame.newestFrame = newest;
    frame.serial = serial;
    frame.cycleMs = 1024.0 / 48.0;
    frame.run = 1;
    frame.points.resize(points);
    for (std::size_t index = 0; index < points; ++index) {
        const double at = newest - static_cast<double>(points - index);
        const float left = sound(at);
        rt::ScopePoint &point = frame.points[index];
        std::ranges::fill(point.low, 0.0f);
        std::ranges::fill(point.high, 0.0f);
        point.low[rt::ScopeLeft] = point.high[rt::ScopeLeft] = left;
        point.low[rt::ScopeRight] = point.high[rt::ScopeRight] = 0.5f * left;
        point.low[rt::ScopeMid] = point.high[rt::ScopeMid] = 0.75f * left;
        point.low[rt::ScopeSide] = point.high[rt::ScopeSide] = 0.25f * left;
    }
    return frame;
}

[[nodiscard]] double framesIn(double ms)
{
    return ms * kRate * 1.0e-3;
}

[[nodiscard]] ScopeSettings settingsOf(double spanMs, bool smooth, double refreshHz = 10.0)
{
    ScopeSettings settings;
    settings.spanMs = spanMs;
    settings.smooth = smooth;
    settings.refreshHz = refreshHz;
    return settings;
}

[[nodiscard]] double highestIn(const ScopeView &view, std::size_t trace = 0)
{
    double highest = -2.0;
    for (const dsp::ScopeColumn &column : view.trace(trace)) {
        if (column.heard) {
            highest = std::max(highest, static_cast<double>(column.high));
        }
    }
    return highest;
}

/// How far the right-hand edge moves at each frame of a 60 Hz screen, with
/// news arriving every kRefreshMs.
[[nodiscard]] std::vector<double> stepsOfTheEdge(const ScopeSettings &settings)
{
    const Sound sound = toneOf(100.0);
    ScopeView view;
    std::vector<double> steps;
    double last = 0.0;
    std::uint64_t serial = 1;
    double nextRefresh = 0.0;
    ScopeFrame frame;
    const auto points = static_cast<std::size_t>(framesIn(settings.spanMs + 400.0));
    constexpr double kStart = 96000.0;
    for (int tick = 0; tick < 300; ++tick) {
        const double now = 1000.0 + tick * kFrameMs;
        if (now >= nextRefresh) {
            frame = frameUpTo(sound, kStart + framesIn(now), points, ++serial);
            nextRefresh = now + kRefreshMs;
        }
        view.update(frame, settings, kColumns, now);
        if (tick > 120) {
            steps.push_back(view.endFrame() - last);
        }
        last = view.endFrame();
    }
    return steps;
}

} // namespace

TEST(ScopeMotion, RealTimeHoldsThenJumpsByARefresh)
{
    const std::vector<double> steps = stepsOfTheEdge(settingsOf(500.0, false));
    const double stood = static_cast<double>(std::ranges::count(steps, 0.0)) / static_cast<double>(steps.size());
    EXPECT_GT(stood, 0.75) << "it should stand still between refreshes";
    EXPECT_NEAR(*std::ranges::max_element(steps), framesIn(kRefreshMs), framesIn(kFrameMs) * 1.5);
}

TEST(ScopeMotion, SmoothMovesTheSameAtEveryFrameOfTheScreen)
{
    const std::vector<double> steps = stepsOfTheEdge(settingsOf(500.0, true));
    const double perFrame = framesIn(kFrameMs);
    for (const double step : steps) {
        EXPECT_NEAR(step, perFrame, perFrame * 0.06) << "the trace speeds up and slows down with the refresh";
    }
}

TEST(ScopeMotion, TheTwoMotionsAreFarApartInNumbers)
{
    // The spread of the steps, as a share of the mean: what the eye reads as
    // a jerk. Ten refreshes a second on a 60 Hz screen.
    const auto jerk = [](const std::vector<double> &steps) {
        const double mean = std::accumulate(steps.begin(), steps.end(), 0.0) / static_cast<double>(steps.size());
        double sum = 0.0;
        for (const double step : steps) {
            sum += (step - mean) * (step - mean);
        }
        return std::sqrt(sum / static_cast<double>(steps.size())) / mean;
    };
    const double real = jerk(stepsOfTheEdge(settingsOf(500.0, false)));
    const double smooth = jerk(stepsOfTheEdge(settingsOf(500.0, true)));
    EXPECT_GT(real, 1.5);
    EXPECT_LT(smooth, 0.05);
}

TEST(ScopeMotion, SmoothStaysBehindTheNewestSoundAndNeverPassesIt)
{
    const Sound sound = toneOf(100.0);
    const ScopeSettings settings = settingsOf(500.0, true);
    ScopeView view;
    constexpr double kStart = 96000.0;
    ScopeFrame frame;
    std::uint64_t serial = 1;
    double nextRefresh = 0.0;
    for (int tick = 0; tick < 600; ++tick) {
        const double now = 1000.0 + tick * kFrameMs;
        if (now >= nextRefresh) {
            frame = frameUpTo(sound, kStart + framesIn(now), static_cast<std::size_t>(framesIn(900.0)), ++serial);
            nextRefresh = now + kRefreshMs;
        }
        view.update(frame, settings, kColumns, now);
        EXPECT_LE(view.endFrame(), frame.newestFrame);
    }
}

TEST(ScopeMotion, ASmoothViewNeverRunsOutOfSoundAtItsOldEnd)
{
    // News comes in lumps, and the clock runs evenly through them, so that
    // just after a lump it is further behind the newest sound than the lag
    // says. What was read has to reach back for that too, or the left of the
    // view opens and closes with every lump.
    const Sound sound = toneOf(100.0);
    struct Case {
        double spanMs;
        double refreshHz;
        double cycleMs;
    };
    for (const Case &asked : {Case{200.0, 30.0, 2048.0 / 48.0},
                              Case{200.0, 60.0, 8192.0 / 48.0},
                              Case{500.0, 10.0, 1024.0 / 48.0},
                              Case{2000.0, 120.0, 256.0 / 48.0}}) {
        const ScopeSettings settings = settingsOf(asked.spanMs, true, asked.refreshHz);
        const auto points = static_cast<std::size_t>(ScopeView::framesWanted(settings, kRate, asked.cycleMs));
        const double lumpMs = std::max(asked.cycleMs, 1000.0 / asked.refreshHz);
        ScopeView view;
        ScopeFrame frame;
        std::uint64_t serial = 1;
        double nextLump = 0.0;
        int empty = 0;
        for (int tick = 0; tick < 300; ++tick) {
            const double now = 1000.0 + tick * kFrameMs;
            if (now >= nextLump) {
                // The sound of a whole number of lumps, as it arrives.
                const double arrived = std::floor(now / lumpMs) * lumpMs;
                frame = frameUpTo(sound, 96000.0 + framesIn(arrived), points, ++serial);
                frame.cycleMs = asked.cycleMs;
                nextLump = arrived + lumpMs;
            }
            view.update(frame, settings, kColumns, now);
            empty += view.trace(0).front().heard ? 0 : 1;
        }
        EXPECT_EQ(empty, 0) << asked.spanMs << " ms at " << asked.refreshHz << " a second, cycle " << asked.cycleMs;
    }
}

TEST(ScopeMotion, AClockFarFromTheSoundGoesThereAtOnce)
{
    ScopeClock clock;
    const double first = clock.advance(100000.0, kRate, 4800.0, 0.0);
    EXPECT_DOUBLE_EQ(first, 95200.0);
    EXPECT_DOUBLE_EQ(clock.advance(10.0e6, kRate, 4800.0, 16.0), 10.0e6 - 4800.0);
}

TEST(ScopeMotion, ARealTimeViewDoesNotWorkWithoutNews)
{
    const ScopeFrame frame = frameUpTo(toneOf(100.0), 96000.0, 12000, 2);
    ScopeView view;
    EXPECT_TRUE(view.update(frame, settingsOf(100.0, false), kColumns, 0.0));
    EXPECT_FALSE(view.update(frame, settingsOf(100.0, false), kColumns, 16.0));
    EXPECT_TRUE(view.update(frame, settingsOf(50.0, false), kColumns, 32.0)) << "a setting changed: another picture";
}

TEST(ScopeMotion, ASmoothShortViewEasesAndARealTimeOneDoesNot)
{
    // A tone that gets louder: one refresh to the next, the picture has to be
    // on its way and not there yet.
    const auto quiet = frameUpTo(toneOf(1000.0, 0.2), 96000.0, 7200, 2);
    const auto loud = frameUpTo(toneOf(1000.0, 0.8), 96000.0 + 480.0, 7200, 3);

    ScopeView smooth;
    const ScopeSettings easing = settingsOf(20.0, true, 30.0);
    smooth.update(quiet, easing, kColumns, 0.0);
    EXPECT_NEAR(highestIn(smooth), 0.2, 0.01);
    smooth.update(loud, easing, kColumns, kFrameMs);
    const double afterOne = highestIn(smooth);
    EXPECT_GT(afterOne, 0.25);
    EXPECT_LT(afterOne, 0.45) << "it jumped: one frame should be a small part of the way";
    double last = afterOne;
    for (int frame = 2; frame < 30; ++frame) {
        smooth.update(loud, easing, kColumns, frame * kFrameMs);
        EXPECT_GE(highestIn(smooth), last - 1.0e-6) << "it should only ever get nearer";
        last = highestIn(smooth);
    }
    EXPECT_NEAR(last, 0.8, 0.02);

    ScopeView real;
    const ScopeSettings plain = settingsOf(20.0, false, 30.0);
    real.update(quiet, plain, kColumns, 0.0);
    real.update(loud, plain, kColumns, kFrameMs);
    EXPECT_NEAR(highestIn(real), 0.8, 0.01) << "Real-time shows the newest sound as it is";
}

TEST(ScopeMotion, EasingIsAMatterOfTimeNotOfFrames)
{
    // The same 100 ms, on a 60 Hz screen and a 240 Hz one.
    const auto quiet = frameUpTo(toneOf(1000.0, 0.2), 96000.0, 7200, 2);
    const auto loud = frameUpTo(toneOf(1000.0, 0.8), 96000.0 + 480.0, 7200, 3);
    const auto heightAfter = [&](double frameMs) {
        ScopeView view;
        const ScopeSettings easing = settingsOf(20.0, true, 30.0);
        view.update(quiet, easing, kColumns, 0.0);
        for (double now = frameMs; now <= 100.0 + 1.0e-9; now += frameMs) {
            view.update(loud, easing, kColumns, now);
        }
        return highestIn(view);
    };
    EXPECT_NEAR(heightAfter(1000.0 / 60.0), heightAfter(1000.0 / 240.0), 0.04);
}

TEST(ScopeMotion, ASmoothShortViewLinesUpOnACrossingUnasked)
{
    // Or two pictures of one tone at different places in its cycle would
    // ease into nothing.
    ScopeView view;
    const ScopeSettings settings = settingsOf(20.0, true, 30.0);
    view.update(frameUpTo(toneOf(1000.0), 96000.0, 7200, 2), settings, kColumns, 0.0);
    EXPECT_TRUE(view.crossed());
    for (int frame = 1; frame < 40; ++frame) {
        view.update(
            frameUpTo(toneOf(1000.0), 96000.0 + 137.0 * frame, 7200, 2 + frame), settings, kColumns, frame * kFrameMs);
    }
    EXPECT_NEAR(highestIn(view), 0.5, 0.01) << "the tone eased away";
}

TEST(ScopeTrigger, ATraceStartsOnTheCrossingAndStandsStill)
{
    ScopeSettings settings = settingsOf(20.0, false, 30.0);
    settings.trigger = true;
    std::vector<std::vector<float>> pictures;
    for (const double newest : {96000.0, 96133.0, 96271.5, 97000.0}) {
        const ScopeFrame frame = frameUpTo(toneOf(441.0), newest, 7200, static_cast<std::uint64_t>(newest));
        ScopeView view;
        view.update(frame, settings, kColumns, 0.0);
        ASSERT_TRUE(view.crossed());
        std::vector<float> heights;
        for (const dsp::ScopeColumn &column : view.trace(0)) {
            ASSERT_TRUE(column.heard);
            heights.push_back(column.high);
        }
        ASSERT_EQ(heights.size(), kColumns);
        EXPECT_NEAR(view.trace(0).front().low, 0.0f, 0.01f) << "the left edge is the crossing";
        EXPECT_GT(heights[8], heights.front()) << "and it is a rising one";
        pictures.push_back(std::move(heights));
    }
    for (std::size_t picture = 1; picture < pictures.size(); ++picture) {
        for (std::size_t column = 0; column < kColumns; ++column) {
            EXPECT_NEAR(pictures[picture][column], pictures[0][column], 0.01f) << column;
        }
    }
}

TEST(ScopeTrigger, WithNothingToStartOnTheNewestSoundShows)
{
    ScopeSettings settings = settingsOf(20.0, false, 30.0);
    settings.trigger = true;
    const ScopeFrame frame = frameUpTo([](double) { return 0.3f; }, 96000.0, 7200, 2);
    ScopeView view;
    view.update(frame, settings, kColumns, 0.0);
    EXPECT_FALSE(view.crossed());
    EXPECT_DOUBLE_EQ(view.endFrame(), 96000.0);
    EXPECT_NEAR(highestIn(view), 0.3, 1.0e-6);
}

TEST(ScopeShape, AColumnKeepsItsSamplesAsItTravels)
{
    // A trace that shimmers as it scrolls is columns cutting the sound in new
    // places at every frame. So a column's heights go by where it is on the
    // sound, and whatever frame shows it, it shows the same.
    const Sound sound = [](double frame) {
        return std::sin(frame * 0.37) * std::sin(frame * 0.0011) > 0.0 ? static_cast<float>(std::sin(frame * 0.91))
                                                                       : 0.0f;
    };
    const ScopeSettings settings = settingsOf(1000.0, true);
    const auto points = static_cast<std::size_t>(framesIn(1600.0));
    const ScopeFrame frame = frameUpTo(sound, 240000.0, points, 2);
    ScopeView view;
    std::vector<float> seen;
    std::uint64_t seenFrom = 0;
    for (int tick = 0; tick < 8; ++tick) {
        view.update(frame, settings, kColumns, 1000.0 + tick * kFrameMs);
        const std::span<const dsp::ScopeColumn> columns = view.trace(0);
        if (tick > 0) {
            const std::uint64_t moved = view.firstColumn() - seenFrom;
            ASSERT_GT(moved, 0U) << "it should be moving";
            for (std::size_t column = 0; column + moved < seen.size() && column + moved < columns.size(); ++column) {
                EXPECT_EQ(columns[column].high, seen[column + moved]) << column;
            }
        }
        seenFrom = view.firstColumn();
        seen.clear();
        for (const dsp::ScopeColumn &column : columns) {
            seen.push_back(column.high);
        }
    }
}

TEST(ScopeShape, TheOffsetBelowAColumnIsAShareOfOne)
{
    ScopeView view;
    const ScopeSettings settings = settingsOf(1000.0, false);
    const ScopeFrame frame = frameUpTo(toneOf(100.0), 240000.0 + 77.0, static_cast<std::size_t>(framesIn(1100.0)), 2);
    view.update(frame, settings, kColumns, 0.0);
    EXPECT_GE(view.shift(), 0.0);
    EXPECT_LT(view.shift(), 1.0);
    EXPECT_EQ(view.trace(0).size(), kColumns + 1) << "one more, for what the shift uncovers";
}

TEST(ScopeShape, EachChannelChoiceShowsItsOwnTraces)
{
    const ScopeFrame frame = frameUpTo(toneOf(1000.0, 0.8), 96000.0, 9600, 2);
    const auto traced = [&](ScopeSettings::Channels channels) {
        ScopeSettings settings = settingsOf(100.0, false);
        settings.channels = channels;
        ScopeView view;
        view.update(frame, settings, kColumns, 0.0);
        std::vector<double> highest;
        for (std::size_t trace = 0; trace < view.traces(); ++trace) {
            highest.push_back(highestIn(view, trace));
        }
        return highest;
    };
    const auto near = [](const std::vector<double> &found, const std::vector<double> &wanted) {
        ASSERT_EQ(found.size(), wanted.size());
        for (std::size_t trace = 0; trace < found.size(); ++trace) {
            EXPECT_NEAR(found[trace], wanted[trace], 0.01) << trace;
        }
    };
    near(traced(ScopeSettings::Channels::Both), {0.8, 0.4});
    near(traced(ScopeSettings::Channels::Stacked), {0.8, 0.4});
    near(traced(ScopeSettings::Channels::Left), {0.8});
    near(traced(ScopeSettings::Channels::Right), {0.4});
    near(traced(ScopeSettings::Channels::Mid), {0.6});
    near(traced(ScopeSettings::Channels::Side), {0.2});
}

TEST(ScopeShape, GainEnlargesAndTheEdgeHolds)
{
    const ScopeFrame frame = frameUpTo(toneOf(1000.0, 0.25), 96000.0, 9600, 2);
    ScopeSettings settings = settingsOf(100.0, false);
    settings.gainDb = 6.0206;
    ScopeView view;
    view.update(frame, settings, kColumns, 0.0);
    EXPECT_NEAR(highestIn(view), 0.5, 0.01);
    settings.gainDb = 24.0;
    view.update(frame, settings, kColumns, 1.0);
    EXPECT_FLOAT_EQ(static_cast<float>(highestIn(view)), 1.0f) << "the trace runs off the top and is pinned there";
}

TEST(ScopeShape, TheDecibelScaleSpreadsTheLastSixtyDecibels)
{
    const auto heightOf = [](double amplitude) {
        const ScopeFrame frame = frameUpTo(toneOf(1000.0, amplitude), 96000.0, 9600, 2);
        ScopeSettings settings = settingsOf(100.0, false);
        settings.decibels = true;
        ScopeView view;
        view.update(frame, settings, kColumns, 0.0);
        return highestIn(view);
    };
    EXPECT_NEAR(heightOf(1.0), 1.0, 0.01);
    EXPECT_NEAR(heightOf(0.1), 2.0 / 3.0, 0.01);
    EXPECT_NEAR(heightOf(0.01), 1.0 / 3.0, 0.01);
    EXPECT_NEAR(heightOf(0.0001), 0.0, 0.01) << "below the floor is the middle line";
}

TEST(ScopeShape, AutoFitFillsAtOnceAndLetsGoSlowly)
{
    ScopeSettings settings = settingsOf(100.0, false);
    settings.autoFit = true;
    ScopeView view;
    view.update(frameUpTo(toneOf(1000.0, 0.01), 96000.0, 9600, 2), settings, kColumns, 0.0);
    EXPECT_NEAR(highestIn(view), ScopeView::kFitTo, 0.02);

    view.update(frameUpTo(toneOf(1000.0, 0.1), 96480.0, 9600, 3), settings, kColumns, 16.0);
    EXPECT_NEAR(highestIn(view), ScopeView::kFitTo, 0.02) << "louder: nothing may go off the edge, not for a frame";

    // Quieter: it lets go over about a second.
    view.update(frameUpTo(toneOf(1000.0, 0.01), 96960.0, 9600, 4), settings, kColumns, 32.0);
    EXPECT_LT(highestIn(view), 0.2);
    view.update(frameUpTo(toneOf(1000.0, 0.01), 97440.0, 9600, 5), settings, kColumns, 5000.0);
    EXPECT_NEAR(highestIn(view), ScopeView::kFitTo, 0.02);
}

TEST(ScopeShape, AutoFitDoesNotEnlargeTheFloor)
{
    ScopeSettings settings = settingsOf(100.0, false);
    settings.autoFit = true;
    ScopeView view;
    view.update(frameUpTo(toneOf(1000.0, 0.5), 96000.0, 9600, 2), settings, kColumns, 0.0);
    const double gain = view.gainDb();
    view.update(frameUpTo(toneOf(1000.0, 1.0e-6), 96480.0, 9600, 3), settings, kColumns, 16.0);
    EXPECT_DOUBLE_EQ(view.gainDb(), gain);
}

TEST(ScopeShape, ABlockThatHasBlownUpLooksIt)
{
    const Sound blown = [](double frame) {
        return std::fmod(frame, 3.0) < 1.0 ? std::numeric_limits<float>::infinity() : std::nanf("");
    };
    ScopeView view;
    view.update(frameUpTo(blown, 96000.0, 9600, 2), settingsOf(100.0, false), kColumns, 0.0);
    EXPECT_EQ(highestIn(view), 1.0);
}

TEST(ScopeShape, WhatTheSoundHasNotReachedIsLeftEmpty)
{
    // A view of a second, a tenth of a second after the slot was given out.
    const ScopeFrame frame = frameUpTo(toneOf(1000.0), 4800.0, 4800, 2);
    ScopeView view;
    view.update(frame, settingsOf(1000.0, false), kColumns, 0.0);
    const std::span<const dsp::ScopeColumn> columns = view.trace(0);
    EXPECT_FALSE(columns[kColumns / 2].heard);
    EXPECT_TRUE(columns[kColumns - 1].heard);
}

TEST(ScopeShape, AReaderReadsNoMoreThanItsSettingsShow)
{
    const ScopeSettings plain = settingsOf(100.0, false);
    EXPECT_DOUBLE_EQ(ScopeView::framesWanted(plain, kRate, 20.0), framesIn(100.0));
    ScopeSettings triggered = plain;
    triggered.trigger = true;
    EXPECT_DOUBLE_EQ(ScopeView::framesWanted(triggered, kRate, 20.0), framesIn(200.0));
    const ScopeSettings flowing = settingsOf(500.0, true, 10.0);
    EXPECT_GT(ScopeView::framesWanted(flowing, kRate, 20.0), framesIn(500.0 + 100.0 + 20.0));
}

TEST(ScopeShape, AnotherSoundStartsThePictureAgain)
{
    const ScopeSettings easing = settingsOf(20.0, true, 30.0);
    ScopeView view;
    view.update(frameUpTo(toneOf(1000.0, 0.2), 96000.0, 7200, 2), easing, kColumns, 0.0);
    ScopeFrame next = frameUpTo(toneOf(1000.0, 0.8), 500.0 + 7200.0, 7200, 1);
    next.run = 2;
    view.update(next, easing, kColumns, kFrameMs);
    EXPECT_NEAR(highestIn(view), 0.8, 0.01) << "it eased in from a sound that has gone";
}
