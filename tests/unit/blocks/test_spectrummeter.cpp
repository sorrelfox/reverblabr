// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Spectrum: the sound passes bit for bit at every cycle length, whatever is
// shown; a slot holds every sample, which is what makes a spectrum true up to
// the top of the range; the settings are kept and touch nothing on the audio
// thread; and with nobody looking it costs next to nothing.

#include "allocationtrap.h"
#include "blocks/spectrummeter.h"
#include "blocksizeinvariance.h"
#include "dsp/barspectrum.h"
#include "dsp/block.h"
#include "dsp/paramvalue.h"
#include "offlinerunner.h"
#include "rt/meter.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <numbers>
#include <vector>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr std::size_t kSecond = 48000;

/// A slot as a memfd gives it, and a meter that has it.
struct Drawn {
    Drawn()
        : slot(static_cast<rt::ScopeSlot *>(std::aligned_alloc(alignof(rt::ScopeSlot), sizeof(rt::ScopeSlot))))
    {
        std::memset(static_cast<void *>(slot), 0, sizeof(rt::ScopeSlot));
        rt::claimScopeSlot(*slot);
        meter.setScope(slot);
    }
    ~Drawn()
    {
        std::free(slot);
    }
    Drawn(const Drawn &) = delete;
    Drawn &operator=(const Drawn &) = delete;

    /// What ring \a level holds of trace \a trace, as a spectrum would read it.
    [[nodiscard]] std::vector<float> samples(std::size_t level, std::size_t trace) const
    {
        std::vector<rt::ScopePoint> room(rt::kScopePoints);
        room.resize(rt::readScope(*slot, level, room).points);
        std::vector<float> out(room.size());
        std::ranges::transform(room, out.begin(), [trace](const rt::ScopePoint &point) { return point.low[trace]; });
        return out;
    }

    rt::ScopeSlot *slot;
    rt::BlockMeter meter;
};

[[nodiscard]] Signal tone(double hz, std::size_t frames, float peak = 1.0f)
{
    Signal samples(frames);
    for (std::size_t n = 0; n < frames; ++n) {
        samples[n] =
            peak * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(n) / 48000.0));
    }
    return samples;
}

[[nodiscard]] float levelAt(const std::vector<float> &samples, double hz, std::size_t frameSize)
{
    FrameSpectrum spectrum;
    spectrum.prepare(frameSize);
    spectrum.add(std::span<const float>(samples).last(spectrum.samplesNeeded()));
    BarMap map;
    map.prepare(64, BarScale::Logarithmic, 48000.0, spectrum.transformSize(), 0.0);
    std::vector<float> levels(64);
    map.read(spectrum.power(), levels);
    return levels[map.barOf(hz)];
}

} // namespace

TEST(SpectrumMeter, PassesTheSoundBitForBitWithNoDelay)
{
    SpectrumMeter block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal left = whiteNoise(kSecond, 0.5f, 1201);
    const Signal right = whiteNoise(kSecond, 0.25f, 1202);
    const MultiSignal out = runner.run(MultiSignal{left, right}, CycleShape::random(1, 2048, 1209));
    EXPECT_EQ(out[0], left);
    EXPECT_EQ(out[1], right);
    EXPECT_EQ(block.info().latencySamples, 0U);
}

TEST(SpectrumMeter, BeingDrawnChangesNothingAboutTheSound)
{
    Drawn drawn;
    SpectrumMeter block;
    BlockRunner runner(block, 2, 2);
    runner.setMeter(&drawn.meter);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal left = whiteNoise(kSecond, 0.5f, 1203);
    const Signal right = whiteNoise(kSecond, 0.25f, 1204);
    const MultiSignal out = runner.run(MultiSignal{left, right}, CycleShape::fixed(128));
    EXPECT_EQ(out[0], left);
    EXPECT_EQ(out[1], right);
}

TEST(SpectrumMeter, TheSoundIsTheSameAtEveryCycleLength)
{
    const MultiSignal input{whiteNoise(kSecond / 2, 0.5f, 1205), whiteNoise(kSecond / 2, 0.5f, 1208)};
    const InvarianceReport report = checkBlockSizeInvariance([] { return std::make_unique<SpectrumMeter>(); }, input);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(SpectrumMeter, TheSlotHoldsEverySampleSoTheSpectrumIsTrueToTheTop)
{
    // Up here a ring of every eighth sample's extremes has nothing left of
    // the sound, which the second half shows: the same tone read from there
    // is not the tone at all.
    for (const double hz : {1000.0, 12000.0, 19900.0}) {
        Drawn drawn;
        SpectrumMeter block;
        BlockRunner runner(block, 2, 2);
        runner.setMeter(&drawn.meter);
        ASSERT_TRUE(runner.prepare().has_value());
        const Signal sound = tone(hz, kSecond);
        (void)runner.run(MultiSignal{sound, sound}, CycleShape::fixed(256));
        for (const std::size_t frameSize : {1024U, 2048U, 4096U, 8192U}) {
            EXPECT_NEAR(levelAt(drawn.samples(0, rt::ScopeMid), hz, frameSize), 0.0f, FrameSpectrum::kAccuracyDb)
                << hz << " Hz, frame " << frameSize;
        }
        if (hz > 10000.0) {
            std::vector<float> coarse = drawn.samples(1, rt::ScopeMid);
            EXPECT_LT(levelAt(coarse, hz, 1024), -6.0f) << "a coarser ring seemed to do";
        }
    }
}

TEST(SpectrumMeter, MixLeftAndRightAreEachTheirOwnSound)
{
    Drawn drawn;
    SpectrumMeter block;
    BlockRunner runner(block, 2, 2);
    runner.setMeter(&drawn.meter);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal left = tone(1000.0, kSecond);
    const Signal right = tone(5000.0, kSecond, 0.5f);
    (void)runner.run(MultiSignal{left, right}, CycleShape::fixed(256));
    EXPECT_NEAR(levelAt(drawn.samples(0, rt::ScopeLeft), 1000.0, 4096), 0.0f, FrameSpectrum::kAccuracyDb);
    EXPECT_LT(levelAt(drawn.samples(0, rt::ScopeLeft), 5000.0, 4096), -60.0f);
    EXPECT_NEAR(levelAt(drawn.samples(0, rt::ScopeRight), 5000.0, 4096), -6.02f, FrameSpectrum::kAccuracyDb);
    EXPECT_NEAR(levelAt(drawn.samples(0, rt::ScopeMid), 1000.0, 4096), -6.02f, FrameSpectrum::kAccuracyDb)
        << "half of the two added";
    EXPECT_NEAR(levelAt(drawn.samples(0, rt::ScopeMid), 5000.0, 4096), -12.04f, FrameSpectrum::kAccuracyDb);
}

TEST(SpectrumMeter, ItsSettingsAreKeptAndAPresetLeavesTheHoldAlone)
{
    SpectrumMeter block;
    ASSERT_TRUE(block.setParam("bars", ParamValue::ofEnum("bars128")).has_value());
    ASSERT_TRUE(block.setParam("fftSize", ParamValue::ofEnum("fft8192")).has_value());
    ASSERT_TRUE(block.setParam("floor", ParamValue::ofEnum("db120")).has_value());
    ASSERT_TRUE(block.setParam("tilt", ParamValue::ofEnum("tilt4p5")).has_value());
    ASSERT_TRUE(block.setParam("style", ParamValue::ofEnum("segmented")).has_value());
    ASSERT_TRUE(block.setParam("hold", ParamValue::ofBool(true)).has_value());
    EXPECT_FALSE(block.setParam("bars", ParamValue::ofEnum("bars3")).has_value());

    SpectrumMeter other;
    ASSERT_TRUE(other.loadState(block.saveState()).has_value());
    EXPECT_EQ(other.param("bars"), ParamValue::ofEnum("bars128"));
    EXPECT_EQ(other.param("fftSize"), ParamValue::ofEnum("fft8192"));
    EXPECT_EQ(other.param("floor"), ParamValue::ofEnum("db120"));
    EXPECT_EQ(other.param("tilt"), ParamValue::ofEnum("tilt4p5"));
    EXPECT_EQ(other.param("style"), ParamValue::ofEnum("segmented"));
    EXPECT_EQ(other.param("hold"), ParamValue::ofBool(true)) << "a session keeps it";

    const auto descriptors = block.params();
    const auto hold = std::ranges::find(descriptors, std::string{"hold"}, &ParamDescriptor::id);
    ASSERT_NE(hold, descriptors.end());
    EXPECT_FALSE(hold->flags.inPresets);
}

TEST(SpectrumMeter, ItStartsWhereAWaveformDoes)
{
    SpectrumMeter block;
    EXPECT_EQ(block.param("refresh"), ParamValue::ofEnum("hz30"));
    EXPECT_EQ(block.param("motion"), ParamValue::ofEnum("realTime"));
    EXPECT_EQ(block.param("scale"), ParamValue::ofEnum("logarithmic"));
    EXPECT_EQ(block.param("tilt"), ParamValue::ofEnum("tilt0"));
}

TEST(SpectrumMeter, AllocatesNothingBeingDrawnOrNot)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "no allocation trap in this build";
    }
    const Signal noise = whiteNoise(kSecond, 0.5f, 1206);
    {
        Drawn drawn;
        SpectrumMeter block;
        BlockRunner runner(block, 2, 2);
        runner.setMeter(&drawn.meter);
        ASSERT_TRUE(runner.prepare().has_value());
        std::size_t allocations = 0;
        (void)runner.runWatchingForAllocations(MultiSignal{noise, noise}, CycleShape::fixed(256), allocations);
        EXPECT_EQ(allocations, 0U);
    }
    {
        rt::BlockMeter meter;
        SpectrumMeter block;
        BlockRunner runner(block, 2, 2);
        runner.setMeter(&meter);
        ASSERT_TRUE(runner.prepare().has_value());
        std::size_t allocations = 0;
        (void)runner.runWatchingForAllocations(MultiSignal{noise, noise}, CycleShape::fixed(256), allocations);
        EXPECT_EQ(allocations, 0U);
    }
}

TEST(SpectrumMeter, CostsAlmostNothingUnwatchedAndLittleWatched)
{
    const Signal noise = whiteNoise(kSecond * 60, 0.1f, 1207);
    const auto shareOfACore = [&](rt::BlockMeter &meter) {
        SpectrumMeter block;
        BlockRunner runner(block, 2, 2);
        runner.setMeter(&meter);
        EXPECT_TRUE(runner.prepare().has_value());
        const auto started = std::chrono::steady_clock::now();
        (void)runner.run(MultiSignal{noise, noise}, CycleShape::fixed(256));
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() / 60.0;
    };
    Drawn drawn;
    rt::BlockMeter nobody;
    const double unwatched = shareOfACore(nobody);
    const double watched = shareOfACore(drawn.meter);
    std::cerr << "spectrum: a minute of stereo at " << unwatched * 100.0 << " % of a core unwatched, "
              << watched * 100.0 << " % watched\n";
#ifdef NDEBUG
    EXPECT_LT(unwatched, 0.002);
    EXPECT_LT(watched, 0.005);
#endif
}
