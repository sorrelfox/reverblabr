// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The bars read what a level meter would: a full-scale sine is 0 dBFS in the
// bar that holds it, at every frame size, every bar count and either scale;
// no bar is left empty where bins are fewer than bars; and tilt turns about
// 1 kHz. The spectrum is real up to the top of the range, because it is taken
// from every sample.

#include "dsp/barspectrum.h"
#include "rt/scope.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>
#include <vector>

using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr double kRate = 48000.0;
constexpr std::size_t kFrameSizes[] = {1024, 2048, 4096, 8192};
constexpr std::size_t kBarCounts[] = {16, 32, 64, 96, 128};

[[nodiscard]] std::vector<float> sine(double hz, double rate, std::size_t count, float peak = 1.0f, double phase = 0.3)
{
    std::vector<float> samples(count);
    for (std::size_t n = 0; n < count; ++n) {
        samples[n] =
            peak * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(n) / rate + phase));
    }
    return samples;
}

struct Reading {
    std::vector<float> levels;
    BarMap map;
};

[[nodiscard]] Reading read(std::span<const float> samples,
                           std::size_t frameSize,
                           std::size_t bars,
                           BarScale scale,
                           double rate = kRate,
                           double tilt = 0.0)
{
    FrameSpectrum spectrum;
    spectrum.prepare(frameSize);
    spectrum.add(samples.subspan(samples.size() - spectrum.samplesNeeded()));
    Reading reading;
    reading.map.prepare(bars, scale, rate, spectrum.transformSize(), tilt);
    reading.levels.resize(bars);
    reading.map.read(spectrum.power(), reading.levels);
    return reading;
}

void expectReadsAtItsBar(double hz, double rate, std::size_t frameSize, std::size_t bars, BarScale scale)
{
    SCOPED_TRACE(testing::Message() << hz << " Hz at " << rate << ", frame " << frameSize << ", " << bars << " bars");
    const std::vector<float> samples = sine(hz, rate, frameSize);
    const Reading reading = read(samples, frameSize, bars, scale, rate);
    const std::size_t bar = reading.map.barOf(hz);
    EXPECT_NEAR(reading.levels[bar], 0.0f, FrameSpectrum::kAccuracyDb);
    for (const float level : reading.levels) {
        EXPECT_LE(level, FrameSpectrum::kAccuracyDb) << "nothing reads over the sine";
    }
    // Where the peak falls right on an edge the next bar can read the same,
    // to within what the steps of the spectrum allow, and no more than that.
    const auto highest = std::ranges::max_element(reading.levels);
    const auto at = static_cast<std::size_t>(highest - reading.levels.begin());
    EXPECT_LE(*highest - reading.levels[bar], 0.1f);
    EXPECT_LE(std::max(at, bar) - std::min(at, bar), 1U);
}

} // namespace

TEST(BarSpectrum, AFullScaleSineReadsNoughtInItsBarAtEverySizeAndCount)
{
    for (const std::size_t frameSize : kFrameSizes) {
        const double bin = kRate / static_cast<double>(frameSize);
        for (const std::size_t bars : kBarCounts) {
            for (const BarScale scale : {BarScale::Logarithmic, BarScale::Linear}) {
                // Spread to fall anywhere between two bins and between two
                // bars, from the lowest a frame can tell to the top.
                for (double hz = std::max(FrameSpectrum::kNearestBins * bin, BarMap::kLowestHz);
                     hz < BarMap::kHighestHz;
                     hz *= 1.2732) {
                    expectReadsAtItsBar(hz, kRate, frameSize, bars, scale);
                }
            }
        }
    }
}

TEST(BarSpectrum, ASineHalfWayBetweenTwoBinsLosesNoMoreThanItIsSaidTo)
{
    for (const std::size_t frameSize : kFrameSizes) {
        const double bin = kRate / static_cast<double>(frameSize);
        // Half way between the transform's bins is the worst place to be.
        const double padding =
            static_cast<double>(std::min(frameSize * FrameSpectrum::kPadding, FrameSpectrum::kLargestTransform))
            / static_cast<double>(frameSize);
        const double hz = (200.0 + 0.5 / padding) * bin;
        expectReadsAtItsBar(hz, kRate, frameSize, 64, BarScale::Linear);
    }
}

TEST(BarSpectrum, TheTopOfTheRangeIsRealAtEveryRate)
{
    // A spectrum of a coarser ring's extremes has lost everything up here, so
    // this is what would catch one.
    for (const double rate : {44100.0, 48000.0, 96000.0, 192000.0}) {
        for (const std::size_t frameSize : kFrameSizes) {
            for (const double hz : {15000.0, 19000.0, 19900.0}) {
                expectReadsAtItsBar(hz, rate, frameSize, 64, BarScale::Logarithmic);
                expectReadsAtItsBar(hz, rate, frameSize, 128, BarScale::Linear);
            }
        }
    }
}

TEST(BarSpectrum, ABarMeasuresTheSineNotTheBar)
{
    // 16 bars are many octaves of bins wide and read the same as 128.
    const std::vector<float> samples = sine(3000.0, kRate, 8192, 0.1f);
    for (const std::size_t bars : kBarCounts) {
        const Reading reading = read(samples, 4096, bars, BarScale::Logarithmic);
        EXPECT_NEAR(reading.levels[reading.map.barOf(3000.0)], -20.0f, FrameSpectrum::kAccuracyDb) << bars;
    }
}

TEST(BarSpectrum, TheLargestFrameFitsInWhatARingGives)
{
    FrameSpectrum spectrum;
    spectrum.prepare(FrameSpectrum::kLargestFrame);
    EXPECT_EQ(spectrum.samplesNeeded(), rb::rt::kScopePoints - 1U);
}

TEST(BarSpectrum, EveryBarHasSomethingWhereBinsAreFewerThanBars)
{
    // A click holds every frequency, equally. With 1024 samples at 48 kHz the
    // bars below some hundred hertz are far narrower than a bin, and each one
    // must still read it, and read it the same.
    for (const std::size_t frameSize : kFrameSizes) {
        std::vector<float> samples(frameSize, 0.0f);
        samples[frameSize / 2] = 1.0f;
        for (const std::size_t bars : kBarCounts) {
            const Reading reading = read(samples, frameSize, bars, BarScale::Logarithmic);
            const auto [lowest, highest] = std::ranges::minmax_element(reading.levels);
            EXPECT_GT(*lowest, -90.0f) << bars << " bars of " << frameSize;
            EXPECT_LT(*highest - *lowest, 0.01f) << bars << " bars of " << frameSize;
        }
    }
}

TEST(BarSpectrum, ALowSineDrawsItsHumpWithNoHoleInIt)
{
    // Every bar at the bottom of the scale shares two or three bins here.
    const std::vector<float> samples = sine(200.0, kRate, 1024);
    const Reading reading = read(samples, 1024, 128, BarScale::Logarithmic);
    const std::size_t peak = reading.map.barOf(200.0);
    EXPECT_NEAR(reading.levels[peak], 0.0f, FrameSpectrum::kAccuracyDb);
    for (std::size_t bar = peak; bar + 1 < reading.levels.size() && reading.levels[bar] > -30.0f; ++bar) {
        EXPECT_LE(reading.levels[bar + 1], reading.levels[bar] + 0.1f) << "a hole above the peak, bar " << bar;
    }
    for (std::size_t bar = peak; bar > 0 && reading.levels[bar] > -30.0f; --bar) {
        EXPECT_LE(reading.levels[bar - 1], reading.levels[bar] + 0.1f) << "a hole below the peak, bar " << bar;
    }
    // Neighbours step no more than a line between two bins can.
    const std::size_t oneOctaveBelow = reading.map.barOf(100.0);
    for (std::size_t bar = oneOctaveBelow; bar < peak; ++bar) {
        EXPECT_LT(reading.levels[bar + 1] - reading.levels[bar], 12.0f);
    }
}

TEST(BarSpectrum, TiltTurnsAboutOneKilohertz)
{
    for (const double tilt : {3.0, 4.5}) {
        const std::vector<float> tone = sine(1000.0, kRate, 4096);
        const Reading pivot = read(tone, 4096, 96, BarScale::Logarithmic, kRate, tilt);
        // The middle of the bar, which is what the tilt is worked out for.
        EXPECT_NEAR(pivot.levels[pivot.map.barOf(1000.0)], 0.0f, 0.8f);

        const std::vector<float> high = sine(8000.0, kRate, 4096);
        const Reading above = read(high, 4096, 96, BarScale::Logarithmic, kRate, tilt);
        EXPECT_NEAR(above.levels[above.map.barOf(8000.0)], 3.0 * tilt, 0.8) << "three octaves up";

        const std::vector<float> low = sine(250.0, kRate, 4096);
        const Reading below = read(low, 4096, 96, BarScale::Logarithmic, kRate, tilt);
        EXPECT_NEAR(below.levels[below.map.barOf(250.0)], -2.0 * tilt, 0.8) << "two octaves down";
    }
}

TEST(BarSpectrum, ThreeDecibelsOfTiltLevelsPinkNoise)
{
    const Signal noise = pinkNoise(48000, 0.25f, 77);
    std::vector<float> samples(noise.begin(), noise.end());
    FrameSpectrum spectrum;
    spectrum.prepare(4096);
    BarMap map;
    map.prepare(16, BarScale::Logarithmic, kRate, spectrum.transformSize(), 3.0);
    // The highest of many frames, which is steady enough to read a slope off.
    for (std::size_t start = 0; start + spectrum.samplesNeeded() <= samples.size(); start += spectrum.hop()) {
        spectrum.add(std::span<const float>(samples).subspan(start, spectrum.samplesNeeded()));
    }
    std::vector<float> levels(16);
    map.read(spectrum.power(), levels);
    // Above the bars a frame of 4096 cannot tell from one another.
    const auto [lowest, highest] = std::ranges::minmax_element(levels.begin() + 4, levels.end());
    EXPECT_LT(*highest - *lowest, 6.0f);
}

TEST(BarSpectrum, SilenceReadsAsNothing)
{
    const std::vector<float> samples(8192, 0.0f);
    const Reading reading = read(samples, 4096, 64, BarScale::Logarithmic);
    for (const float level : reading.levels) {
        EXPECT_LE(level, BarMap::kSilenceDb + 1.0f);
    }
}

TEST(BarSpectrum, ASoundThatIsNoNumberLooksAsBadAsItIs)
{
    for (const float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 1.0e30f}) {
        std::vector<float> samples(4096, 0.0f);
        samples[2048] = bad;
        const Reading reading = read(samples, 4096, 64, BarScale::Logarithmic);
        for (const float level : reading.levels) {
            EXPECT_TRUE(std::isfinite(level));
            EXPECT_GT(level, -40.0f) << bad;
        }
    }
}

TEST(BarSpectrum, AClickStaysHoweverManyFramesAreAddedAroundIt)
{
    FrameSpectrum spectrum;
    spectrum.prepare(1024);
    const std::vector<float> quiet(spectrum.samplesNeeded(), 0.0f);
    std::vector<float> click = quiet;
    click[512] = 1.0f;
    spectrum.add(quiet);
    spectrum.add(click);
    spectrum.add(quiet);
    const float kept = spectrum.power()[100];
    EXPECT_GT(kept, 0.0f);
    spectrum.clear();
    spectrum.add(quiet);
    EXPECT_EQ(spectrum.power()[100], 0.0f);
}

TEST(BarSpectrum, TheRangeStopsAtHalfTheRate)
{
    EXPECT_DOUBLE_EQ(BarMap::highestHz(192000.0), BarMap::kHighestHz);
    EXPECT_DOUBLE_EQ(BarMap::highestHz(48000.0), BarMap::kHighestHz);
    EXPECT_DOUBLE_EQ(BarMap::highestHz(32000.0), 16000.0);
    const std::vector<float> samples = sine(6000.0, 16000.0, 2048);
    const Reading reading = read(samples, 2048, 64, BarScale::Logarithmic, 16000.0);
    EXPECT_DOUBLE_EQ(reading.map.edgeHz(64), 8000.0);
    EXPECT_NEAR(reading.levels[reading.map.barOf(6000.0)], 0.0f, FrameSpectrum::kAccuracyDb);

    // As near the top as a sine can be told from its mirror image.
    const double hz = 8000.0 - FrameSpectrum::kNearestBins * 16000.0 / 2048.0;
    const std::vector<float> top = sine(hz, 16000.0, 2048);
    const Reading atTheTop = read(top, 2048, 64, BarScale::Logarithmic, 16000.0);
    EXPECT_NEAR(atTheTop.levels[atTheTop.map.barOf(hz)], 0.0f, FrameSpectrum::kAccuracyDb);
}

TEST(BarSpectrum, EachOctaveIsAsWideAsTheNextOnALogarithmicScale)
{
    BarMap map;
    map.prepare(30, BarScale::Logarithmic, kRate, 16384, 0.0);
    EXPECT_NEAR(map.edgeHz(0), BarMap::kLowestHz, 1.0e-9);
    EXPECT_NEAR(map.edgeHz(30), BarMap::kHighestHz, 1.0e-6);
    EXPECT_NEAR(map.edgeHz(11) / map.edgeHz(10), map.edgeHz(21) / map.edgeHz(20), 1.0e-9);
    EXPECT_EQ(map.barOf(1.0), 0U);
    EXPECT_EQ(map.barOf(1.0e6), 29U);
    map.prepare(30, BarScale::Linear, kRate, 16384, 0.0);
    EXPECT_NEAR(map.edgeHz(11) - map.edgeHz(10), map.edgeHz(21) - map.edgeHz(20), 1.0e-9);
}
