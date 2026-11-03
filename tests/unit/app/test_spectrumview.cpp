// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What one look at a slot becomes: a spectrum worked out once, from every
// sample, with nothing lost between looks; and bars that Real-time shows as
// they are and Smooth moves at the screen's rate, a quick rise and an eased
// fall, on a clock of the test's own.

#include "dsp/barspectrum.h"
#include "scopeview.h"
#include "spectrumview.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>
#include <vector>

using namespace rb;
using namespace rb::ui;

namespace
{

constexpr double kRate = 48000.0;

/// A slot's worth of sound, as the feed would hand it over.
class Sound
{
public:
    /// Adds \a frames of a sine of \a hz, or of silence at \a peak nought.
    void hear(double hz, float peak, std::size_t frames)
    {
        for (std::size_t n = 0; n < frames; ++n) {
            const auto value = peak * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * m_heard / kRate));
            m_heard += 1.0;
            push(value);
        }
        publish();
    }

    /// Adds one sample of \a value.
    void click(float value)
    {
        m_heard += 1.0;
        push(value);
        publish();
    }

    /// As a block handed over to another: what comes is another sound.
    void anotherSound()
    {
        m_frame.points.clear();
        ++m_frame.run;
    }

    [[nodiscard]] const ScopeFrame &frame() const noexcept
    {
        return m_frame;
    }

private:
    void push(float value)
    {
        m_frame.points.push_back(rt::ScopePoint{{value, 0.5f * value, 0.75f * value, 0.25f * value},
                                                {value, 0.5f * value, 0.75f * value, 0.25f * value}});
        if (m_frame.points.size() > rt::kScopePoints - 1) {
            m_frame.points.erase(m_frame.points.begin());
        }
    }

    void publish()
    {
        m_frame.newestFrame = m_heard;
        m_frame.framesPerPoint = 1;
        m_frame.sampleRate = static_cast<std::uint32_t>(kRate);
        ++m_frame.serial;
    }

    ScopeFrame m_frame;
    double m_heard = 0.0;
};

/// A view on its own clock, working at the screen's rate when asked.
class Viewed
{
public:
    explicit Viewed(bool smooth)
    {
        settings.smooth = smooth;
    }

    void look(const Sound &sound)
    {
        analysis.update(sound.frame(), settings.frameSize, settings.trace);
        view.update(analysis, settings, now);
    }

    /// Runs the view for \a ms at sixty frames a second, the sound not moving.
    void run(const Sound &sound, double ms)
    {
        for (double gone = 0.0; gone < ms; gone += 1000.0 / 60.0) {
            now += 1000.0 / 60.0;
            look(sound);
        }
    }

    [[nodiscard]] float levelAt(double hz) const
    {
        return view.levels()[view.map().barOf(hz)];
    }

    [[nodiscard]] float peakAt(double hz) const
    {
        return view.peaks()[view.map().barOf(hz)];
    }

    SpectrumSettings settings;
    SpectrumAnalysis analysis;
    SpectrumView view;
    double now = 0.0;
};

} // namespace

TEST(SpectrumAnalysis, ASineReadsNoughtFromAFrameThatIsAFullRing)
{
    Sound sound;
    sound.hear(1000.0, 1.0f, rt::kScopePoints);
    for (const std::size_t size : {1024U, 2048U, 4096U, 8192U}) {
        Viewed viewed(false);
        viewed.settings.frameSize = size;
        viewed.settings.trace = rt::ScopeLeft;
        viewed.look(sound);
        EXPECT_NEAR(viewed.levelAt(1000.0), 0.0f, dsp::FrameSpectrum::kAccuracyDb) << size;
    }
}

TEST(SpectrumAnalysis, EachTraceIsReadAsItself)
{
    Sound sound;
    sound.hear(1000.0, 1.0f, 8000);
    Viewed viewed(false);
    viewed.settings.trace = rt::ScopeLeft;
    viewed.look(sound);
    EXPECT_NEAR(viewed.levelAt(1000.0), 0.0f, 0.4f);
    viewed.settings.trace = rt::ScopeMid;
    viewed.look(sound);
    EXPECT_NEAR(viewed.levelAt(1000.0), -2.5f, 0.4f) << "mid is three quarters of it here";
    viewed.settings.trace = rt::ScopeRight;
    viewed.look(sound);
    EXPECT_NEAR(viewed.levelAt(1000.0), -6.02f, 0.4f);
}

TEST(SpectrumAnalysis, NothingIsShownUntilAFrameIsFull)
{
    Sound sound;
    sound.hear(1000.0, 1.0f, 2000);
    Viewed viewed(false);
    viewed.look(sound);
    EXPECT_FALSE(viewed.analysis.isReady());
    EXPECT_TRUE(viewed.view.levels().empty());
    sound.hear(1000.0, 1.0f, 2200);
    viewed.look(sound);
    EXPECT_TRUE(viewed.analysis.isReady());
    EXPECT_FALSE(viewed.view.levels().empty());
}

TEST(SpectrumAnalysis, ALookWithNothingNewIsNoTransform)
{
    Sound sound;
    sound.hear(1000.0, 1.0f, 8000);
    SpectrumAnalysis analysis;
    analysis.update(sound.frame(), 4096, rt::ScopeLeft);
    const std::uint64_t transforms = analysis.transforms();
    analysis.update(sound.frame(), 4096, rt::ScopeLeft);
    analysis.update(sound.frame(), 4096, rt::ScopeLeft);
    EXPECT_EQ(analysis.transforms(), transforms) << "the second surface of a block costs nothing";
}

TEST(SpectrumAnalysis, AClickBetweenTwoRefreshesIsNotLost)
{
    // 1024 samples are 21 ms, and at 10 refreshes a second a look has to
    // cover 100 ms: one frame would see a fifth of it.
    Sound sound;
    sound.hear(1000.0, 0.0f, 8000);
    Viewed viewed(false);
    viewed.settings.frameSize = 1024;
    viewed.settings.trace = rt::ScopeLeft;
    viewed.look(sound);
    sound.hear(1000.0, 0.0f, 2000);
    sound.click(1.0f);
    sound.hear(1000.0, 0.0f, 2800);
    const std::uint64_t transforms = viewed.analysis.transforms();
    viewed.look(sound);
    EXPECT_GT(viewed.levelAt(1000.0), -60.0f) << "the click went by unseen";
    // It takes as many as cover what went by, and no more.
    EXPECT_EQ(viewed.analysis.transforms() - transforms, 10U);
}

TEST(SpectrumAnalysis, TheWorkPerSecondDoesNotGrowWithTheRefreshRate)
{
    const auto transformsPerSecond = [](double refreshHz) {
        Sound sound;
        sound.hear(1000.0, 0.5f, 8000);
        SpectrumAnalysis analysis;
        analysis.update(sound.frame(), 1024, rt::ScopeLeft);
        const std::uint64_t before = analysis.transforms();
        const auto step = static_cast<std::size_t>(kRate / refreshHz);
        for (int look = 0; look < static_cast<int>(refreshHz); ++look) {
            sound.hear(1000.0, 0.5f, step);
            analysis.update(sound.frame(), 1024, rt::ScopeLeft);
        }
        return analysis.transforms() - before;
    };
    // Half a frame is 512 samples, so about 94 a second, and part of a hop
    // rounds up to a whole one at each look.
    EXPECT_LE(transformsPerSecond(10.0), 94U + 10U);
    EXPECT_LE(transformsPerSecond(30.0), 94U + 30U);
    EXPECT_EQ(transformsPerSecond(120.0), 120U) << "faster than half a frame a look, one a look";
}

TEST(SpectrumAnalysis, ACoarserRingIsNeverTakenForSound)
{
    Sound sound;
    sound.hear(1000.0, 1.0f, 8000);
    ScopeFrame coarse = sound.frame();
    coarse.framesPerPoint = 8;
    SpectrumAnalysis analysis;
    analysis.update(coarse, 4096, rt::ScopeLeft);
    EXPECT_FALSE(analysis.isReady());
}

TEST(SpectrumAnalysis, TheFrameSizeCanChangeOnSoundAlreadyHere)
{
    // What Hold counts on: the whole ring is kept, whatever size is in use.
    Sound sound;
    sound.hear(1000.0, 1.0f, rt::kScopePoints);
    Viewed viewed(false);
    viewed.settings.frameSize = 1024;
    viewed.look(sound);
    const std::uint64_t serial = viewed.analysis.serial();
    viewed.settings.frameSize = 8192;
    viewed.look(sound);
    EXPECT_GT(viewed.analysis.serial(), serial);
    EXPECT_EQ(viewed.analysis.transformSize(), 16384U);
}

TEST(SpectrumAnalysis, ARingShortOfAFullFrameIsPutOffAndNotMadeUp)
{
    // The writer overtaking the copy leaves a few samples fewer than asked.
    Sound sound;
    sound.hear(1000.0, 1.0f, rt::kScopePoints);
    SpectrumAnalysis analysis;
    analysis.update(sound.frame(), 8192, rt::ScopeLeft);
    ASSERT_TRUE(analysis.isReady());
    const std::uint64_t serial = analysis.serial();
    ScopeFrame shortened = sound.frame();
    shortened.points.resize(8000);
    ++shortened.serial;
    analysis.update(shortened, 8192, rt::ScopeLeft);
    EXPECT_EQ(analysis.serial(), serial);
    EXPECT_TRUE(analysis.isReady());
    sound.hear(1000.0, 1.0f, 100);
    sound.hear(1000.0, 1.0f, 100);
    analysis.update(sound.frame(), 8192, rt::ScopeLeft);
    EXPECT_GT(analysis.serial(), serial);
}

TEST(SpectrumView, RealTimeShowsEachRefreshAsItIsAndHoldsIt)
{
    Sound sound;
    sound.hear(1000.0, 1.0f, 8000);
    Viewed viewed(false);
    viewed.look(sound);
    const float loud = viewed.levelAt(1000.0);

    sound.hear(1000.0, 0.01f, 8000);
    viewed.now += 1.0;
    // Before the look it has not moved, however long it has been.
    EXPECT_EQ(viewed.levelAt(1000.0), loud);
    viewed.look(sound);
    EXPECT_NEAR(viewed.levelAt(1000.0), loud - 40.0f, 1.0f) << "all the way at the next refresh";

    const float quiet = viewed.levelAt(1000.0);
    viewed.run(sound, 500.0);
    EXPECT_EQ(viewed.levelAt(1000.0), quiet) << "and nothing between refreshes";
}

TEST(SpectrumView, SmoothRisesQuicklyAndEasesDown)
{
    Sound sound;
    sound.hear(1000.0, 0.0f, 8000);
    Viewed viewed(true);
    viewed.settings.trace = rt::ScopeLeft;
    viewed.look(sound);
    EXPECT_FLOAT_EQ(viewed.levelAt(1000.0), SpectrumView::kLowestDb);

    sound.hear(1000.0, 1.0f, 8000);
    viewed.now += 1000.0 / 60.0;
    viewed.look(sound);
    const float firstFrame = viewed.levelAt(1000.0);
    EXPECT_GT(firstFrame, SpectrumView::kLowestDb + 1.0f) << "it starts at once";
    EXPECT_LT(firstFrame, -1.0f) << "and it does not jump";
    viewed.run(sound, 100.0);
    EXPECT_GT(viewed.levelAt(1000.0), -6.0f) << "a rise is over in a tenth of a second";

    viewed.run(sound, 400.0);
    // More than the ring holds, so that nothing of the tone is left.
    sound.hear(1000.0, 0.0f, rt::kScopePoints);
    float last = viewed.levelAt(1000.0);
    viewed.run(sound, 100.0);
    const float tenthOfASecond = viewed.levelAt(1000.0);
    EXPECT_LT(tenthOfASecond, last);
    EXPECT_GT(tenthOfASecond, -60.0f) << "a fall takes its time";
    last = tenthOfASecond;
    for (int step = 0; step < 30; ++step) {
        viewed.run(sound, 20.0);
        EXPECT_LE(viewed.levelAt(1000.0), last) << "and keeps going down";
        last = viewed.levelAt(1000.0);
    }
    viewed.run(sound, 3000.0);
    EXPECT_LT(viewed.levelAt(1000.0), -115.0f) << "all the way";
}

TEST(SpectrumView, SmoothMovesBetweenRefreshesAndRealTimeDoesNot)
{
    Sound sound;
    sound.hear(1000.0, 1.0f, 8000);
    Viewed realTime(false);
    Viewed smooth(true);
    realTime.look(sound);
    smooth.look(sound);
    sound.hear(1000.0, 0.01f, 8000);
    realTime.look(sound);
    smooth.look(sound);
    const float before = realTime.levelAt(1000.0);
    const float smoothBefore = smooth.levelAt(1000.0);
    // Three frames of the screen, and no new look at the sound.
    int smoothPictures = 0;
    for (int frame = 0; frame < 3; ++frame) {
        realTime.now += 16.0;
        smooth.now += 16.0;
        EXPECT_FALSE(realTime.view.update(realTime.analysis, realTime.settings, realTime.now));
        smoothPictures += smooth.view.update(smooth.analysis, smooth.settings, smooth.now) ? 1 : 0;
    }
    EXPECT_EQ(smoothPictures, 3);
    EXPECT_EQ(realTime.levelAt(1000.0), before);
    EXPECT_LT(smooth.levelAt(1000.0), smoothBefore);
}

TEST(SpectrumView, TheTwoMotionsAreClearlyDifferentOnTheSameSound)
{
    Sound sound;
    sound.hear(1000.0, 1.0f, 8000);
    Viewed realTime(false);
    Viewed smooth(true);
    realTime.look(sound);
    smooth.look(sound);
    sound.hear(1000.0, 0.001f, 8000);
    realTime.now += 33.0;
    smooth.now += 33.0;
    realTime.look(sound);
    smooth.look(sound);
    EXPECT_GT(smooth.levelAt(1000.0) - realTime.levelAt(1000.0), 40.0f);
}

TEST(SpectrumView, ASlowerScreenTakesBiggerStepsOfTheSameCurve)
{
    const auto afterAQuarterSecond = [](double frameMs) {
        Sound sound;
        sound.hear(1000.0, 1.0f, 8000);
        Viewed viewed(true);
        viewed.look(sound);
        sound.hear(1000.0, 0.0f, 8000);
        for (double gone = 0.0; gone < 250.0; gone += frameMs) {
            viewed.now += frameMs;
            viewed.look(sound);
        }
        return viewed.levelAt(1000.0);
    };
    EXPECT_NEAR(afterAQuarterSecond(1000.0 / 144.0), afterAQuarterSecond(1000.0 / 30.0), 2.0f);
}

TEST(SpectrumView, APeakMarkStaysThenFalls)
{
    Sound sound;
    sound.hear(1000.0, 1.0f, 8000);
    Viewed viewed(true);
    viewed.settings.trace = rt::ScopeLeft;
    viewed.look(sound);
    const float peak = viewed.peakAt(1000.0);
    EXPECT_NEAR(peak, 0.0f, 0.5f);

    sound.hear(1000.0, 0.0f, 8000);
    viewed.run(sound, SpectrumView::kPeakHoldMs * 0.9);
    EXPECT_FLOAT_EQ(viewed.peakAt(1000.0), peak) << "it stays";
    EXPECT_LT(viewed.levelAt(1000.0), peak - 10.0f);

    viewed.run(sound, SpectrumView::kPeakHoldMs * 0.1 + 1000.0);
    EXPECT_NEAR(viewed.peakAt(1000.0), peak - SpectrumView::kPeakFallDbPerSecond, 3.0f) << "then falls, at its rate";
    EXPECT_GE(viewed.peakAt(1000.0), viewed.levelAt(1000.0));
}

TEST(SpectrumView, APeakMarkIsPushedUpAtOnceByWhatIsLouder)
{
    Sound sound;
    sound.hear(1000.0, 0.01f, 8000);
    Viewed viewed(false);
    viewed.look(sound);
    const float before = viewed.peakAt(1000.0);
    sound.hear(1000.0, 1.0f, 8000);
    viewed.now += 33.0;
    viewed.look(sound);
    EXPECT_NEAR(viewed.peakAt(1000.0) - before, 40.0f, 1.0f);
}

TEST(SpectrumView, AnotherSoundStartsAgainWithNoEasingFromTheLast)
{
    Sound sound;
    sound.hear(1000.0, 1.0f, 8000);
    Viewed viewed(true);
    viewed.look(sound);
    sound.anotherSound();
    viewed.look(sound);
    EXPECT_TRUE(viewed.view.levels().empty());
    sound.hear(5000.0, 1.0f, 8000);
    viewed.look(sound);
    EXPECT_LT(viewed.levelAt(1000.0), -60.0f);
    EXPECT_GT(viewed.levelAt(5000.0), -6.0f);
}

TEST(SpectrumView, ChangingTheShapeAppliesAtOnce)
{
    Sound sound;
    sound.hear(1000.0, 1.0f, 8000);
    Viewed viewed(true);
    viewed.look(sound);
    viewed.settings.bars = 16;
    viewed.now += 10.0;
    viewed.look(sound);
    EXPECT_EQ(viewed.view.bars(), 16U);
    viewed.settings.tiltDb = 3.0;
    viewed.now += 10.0;
    viewed.look(sound);
    sound.hear(8000.0, 1.0f, 8000);
    viewed.settings.tiltDb = 4.5;
    viewed.now += 10.0;
    viewed.look(sound);
    EXPECT_GT(viewed.levelAt(8000.0), 5.0f) << "above the top, for the drawing to pin";
}

TEST(SpectrumView, HeightsAreMeasuredFromTheFloorToTheTop)
{
    EXPECT_FLOAT_EQ(SpectrumView::heightOf(0.0f, -80.0), 1.0f);
    EXPECT_FLOAT_EQ(SpectrumView::heightOf(-80.0f, -80.0), 0.0f);
    EXPECT_FLOAT_EQ(SpectrumView::heightOf(-40.0f, -80.0), 0.5f);
    EXPECT_FLOAT_EQ(SpectrumView::heightOf(-30.0f, -60.0), 0.5f);
    EXPECT_FLOAT_EQ(SpectrumView::heightOf(6.0f, -80.0), 1.0f) << "tilted over the top";
    EXPECT_FLOAT_EQ(SpectrumView::heightOf(-100.0f, -80.0), 0.0f);
}

TEST(SpectrumView, CostsLittleOfACore)
{
    // What a surface at the defaults does each second, and at the largest
    // settings: the transforms, the bars and the copy out of the ring are
    // the whole of the work in the window but for drawing.
    Sound sound;
    sound.hear(1000.0, 0.5f, rt::kScopePoints);
    const auto oneSecond = [&](std::size_t frameSize, int looks) {
        Viewed viewed(false);
        viewed.settings.frameSize = frameSize;
        viewed.settings.bars = 128;
        ScopeFrame frame = sound.frame();
        const auto started = std::chrono::steady_clock::now();
        for (int look = 0; look < looks; ++look) {
            ++frame.serial;
            viewed.now += 1000.0 / looks;
            viewed.analysis.update(frame, frameSize, viewed.settings.trace);
            viewed.view.update(viewed.analysis, viewed.settings, viewed.now);
        }
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    };
    const double usual = oneSecond(4096, 30);
    const double most = oneSecond(8192, 120);
    std::cerr << "spectrum: " << usual * 100.0 << " % of a core at 4096 and 30 a second, " << most * 100.0
              << " % at 8192 and 120\n";
#ifdef NDEBUG
    EXPECT_LT(usual, 0.02);
    EXPECT_LT(most, 0.08);
#endif
}
