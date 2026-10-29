// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The accumulators the audio thread writes and the sampler empties.
// What they have to get right is a single sample: a click that a meter
// misses is a meter nobody can trust.

#include "rt/meter.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <thread>
#include <vector>

using namespace rb::rt;

namespace
{

/// A block of silence with one sample in it, which is the thing a meter is
/// most likely to lose.
[[nodiscard]] std::vector<float> click(std::size_t frames, std::size_t at, float amplitude)
{
    std::vector<float> samples(frames, 0.0f);
    samples[at] = amplitude;
    return samples;
}

} // namespace

TEST(MeterTest, NothingMeasuredIsNotSilenceButNothing)
{
    ChannelMeter meter;
    const MeterReading reading = meter.take();
    EXPECT_EQ(reading.frames, 0U);
    EXPECT_FLOAT_EQ(reading.peak, 0.0f);
    EXPECT_FLOAT_EQ(reading.rms, 0.0f);
    EXPECT_FALSE(reading.clipped);
    EXPECT_FALSE(reading.isActive());
}

TEST(MeterTest, ASingleSampleSurvivesAWholeCycle)
{
    ChannelMeter meter;
    // −6 dBFS, one sample in two thousand.
    const std::vector<float> samples = click(2048, 1000, 0.5f);
    meter.measure(samples);

    const MeterReading reading = meter.take();
    EXPECT_FLOAT_EQ(reading.peak, 0.5f);
    EXPECT_EQ(reading.frames, 2048U);
    EXPECT_TRUE(reading.isActive());
    // The RMS of one sample among two thousand is tiny, and that is right:
    // the bar barely moves and the peak line jumps.
    EXPECT_NEAR(reading.rms, std::sqrt(0.25f / 2048.0f), 1e-6f);
}

TEST(MeterTest, TakingItEmptiesIt)
{
    ChannelMeter meter;
    meter.measure(click(64, 0, 0.5f));
    EXPECT_FLOAT_EQ(meter.take().peak, 0.5f);

    const MeterReading second = meter.take();
    EXPECT_FLOAT_EQ(second.peak, 0.0f);
    EXPECT_EQ(second.frames, 0U);
}

TEST(MeterTest, ThePeakIsTheLoudestOfEveryCycleSinceTheLastLook)
{
    ChannelMeter meter;
    meter.measure(click(64, 0, 0.1f));
    meter.measure(click(64, 0, 0.7f));
    meter.measure(click(64, 0, 0.2f));

    const MeterReading reading = meter.take();
    EXPECT_FLOAT_EQ(reading.peak, 0.7f) << "a quiet cycle after a loud one pulled the peak down";
    EXPECT_EQ(reading.frames, 192U);
}

TEST(MeterTest, TheRmsIsOverEverythingMeasuredNotOverTheLastCycle)
{
    ChannelMeter meter;
    // Full scale for a hundred frames, silence for three hundred.
    meter.measure(std::vector<float>(100, 1.0f));
    meter.measure(std::vector<float>(300, 0.0f));

    const MeterReading reading = meter.take();
    EXPECT_EQ(reading.frames, 400U);
    EXPECT_NEAR(reading.rms, std::sqrt(100.0f / 400.0f), 1e-6f);
}

TEST(MeterTest, AMagnitudeIsWhatIsMeasuredNotASign)
{
    ChannelMeter meter;
    meter.measure(click(64, 0, -0.8f));
    EXPECT_FLOAT_EQ(meter.take().peak, 0.8f);
}

TEST(MeterTest, FullScaleLatchesAClipUntilItIsRead)
{
    ChannelMeter meter;
    meter.measure(click(64, 3, 1.0f));
    meter.measure(std::vector<float>(64, 0.0f));
    meter.measure(std::vector<float>(64, 0.0f));

    EXPECT_TRUE(meter.take().clipped) << "a clip three cycles ago was forgotten";
    EXPECT_FALSE(meter.take().clipped) << "the clip stayed latched after it had been read";
}

TEST(MeterTest, SomethingBelowEightyDecibelsIsNotASignal)
{
    ChannelMeter meter;
    // −90 dBFS: something is there, but nothing a wire should light up for.
    meter.measure(std::vector<float>(64, 0.00003f));
    EXPECT_FALSE(meter.take().isActive());

    // −60 dBFS is quiet and is still a signal.
    meter.measure(std::vector<float>(64, 0.001f));
    EXPECT_TRUE(meter.take().isActive());
}

TEST(MeterTest, ABlockThatGoesWrongShowsFullScaleRatherThanSilence)
{
    // A NaN compares false against everything, so a meter that took the
    // comparisons at face value would show a block that had blown up as
    // perfectly quiet.
    ChannelMeter meter;
    std::vector<float> samples(64, 0.0f);
    samples[10] = std::nanf("");
    meter.measure(samples);

    const MeterReading reading = meter.take();
    EXPECT_FLOAT_EQ(reading.peak, 1.0f);
    EXPECT_TRUE(reading.clipped);
}

TEST(MeterTest, EachChannelOfABlockIsItsOwn)
{
    BlockMeter block;
    block.setChannelCount(2);
    block.channel(0).measure(click(64, 0, 0.5f));
    block.channel(1).measure(click(64, 0, 0.25f));

    EXPECT_EQ(block.channelCount(), 2U);
    EXPECT_FLOAT_EQ(block.channel(0).take().peak, 0.5f);
    EXPECT_FLOAT_EQ(block.channel(1).take().peak, 0.25f);
}

TEST(MeterTest, AChannelPastTheEndIsNotAWriteIntoNowhere)
{
    // A block with more outputs than there is room for still runs; asking
    // for a channel there is no room for gives back the last one rather than
    // stepping off the end of the array.
    BlockMeter block;
    block.setChannelCount(kMaxMeterChannels + 4);
    EXPECT_EQ(block.channelCount(), kMaxMeterChannels);
    block.channel(kMaxMeterChannels + 100).measure(click(64, 0, 0.5f));
    EXPECT_FLOAT_EQ(block.channel(kMaxMeterChannels - 1).take().peak, 0.5f);
}

TEST(MeterTest, ABlockPublishesItsOwnNumbersAlongsideItsLevels)
{
    BlockMeter block;
    EXPECT_FLOAT_EQ(block.value(0), 0.0f);
    block.publish(0, -3.5f); // a gain reduction, say
    EXPECT_FLOAT_EQ(block.value(0), -3.5f);
    // A block that publishes more than there is room for is not a crash.
    block.publish(kMaxMeterValues + 2, 1.0f);
    EXPECT_FLOAT_EQ(block.value(kMaxMeterValues + 2), 0.0f);
}

TEST(MeterTest, ClearingForgetsEverything)
{
    BlockMeter block;
    block.setChannelCount(2);
    block.channel(0).measure(std::vector<float>(64, 1.0f));
    block.publish(0, 12.0f);

    block.clear();
    const MeterReading reading = block.channel(0).take();
    EXPECT_FLOAT_EQ(reading.peak, 0.0f);
    EXPECT_FALSE(reading.clipped);
    EXPECT_FLOAT_EQ(block.value(0), 0.0f);
}

TEST(MeterTest, AWriterAndAReaderNeverLoseAPeakBetweenThem)
{
    // Not a proof of thread safety — ThreadSanitizer does that — but it does
    // catch a peak dropped by the compare and swap, which a single-threaded
    // test never would.
    ChannelMeter meter;
    constexpr int kCycles = 20000;
    std::atomic<bool> go{false};
    float loudest = 0.0f;

    std::thread reader([&] {
        while (!go.load(std::memory_order_acquire)) { }
        for (int read = 0; read < kCycles; ++read) {
            loudest = std::max(loudest, meter.take().peak);
        }
    });

    go.store(true, std::memory_order_release);
    for (int cycle = 0; cycle < kCycles; ++cycle) {
        meter.measure(click(32, 0, 0.5f));
    }
    reader.join();
    loudest = std::max(loudest, meter.take().peak);
    EXPECT_FLOAT_EQ(loudest, 0.5f) << "every reading was silence, so the peak went missing";
}

TEST(MeterTest, AReadingIsNeverHalfOfOneCycleAndHalfOfAnother)
{
    // The peak, the sum and the frame count were three atomics, taken
    // one after another. A cycle added between the first and the last
    // landed in two readings: one with its frames counted and no peak — a
    // block measured, active on its other side and reading silence on this
    // one, which is what the meter flicker test caught once under asan — and
    // the next with the peak and no frames, thrown away as nothing measured.
    //
    // So: every cycle is the same, a steady 0.5, and every reading that says
    // it measured anything has to say 0.5 for both the peak and the RMS, and
    // between them the readings have to account for every frame. A writer
    // against a reader, as hard as they go, for long enough that the
    // interleaving comes up many times over.
    ChannelMeter meter;
    constexpr std::uint32_t kFrames = 64;
    constexpr int kCycles = 400000;
    const std::vector<float> steady(kFrames, 0.5f);
    std::atomic<bool> done{false};
    std::atomic<bool> go{false};

    std::uint64_t counted = 0;
    int torn = 0;
    int readings = 0;
    std::thread reader([&] {
        while (!go.load(std::memory_order_acquire)) { }
        while (true) {
            const bool last = done.load(std::memory_order_acquire);
            const MeterReading reading = meter.take();
            if (reading.frames > 0) {
                ++readings;
                counted += reading.frames;
                const bool consistent = reading.peak == 0.5f && std::abs(reading.rms - 0.5f) < 1e-3f;
                torn += consistent ? 0 : 1;
            }
            if (last) {
                break;
            }
        }
    });

    go.store(true, std::memory_order_release);
    for (int cycle = 0; cycle < kCycles; ++cycle) {
        meter.measure(steady);
    }
    done.store(true, std::memory_order_release);
    reader.join();

    EXPECT_EQ(torn, 0) << torn << " of " << readings << " readings mixed two cycles";
    EXPECT_EQ(counted, static_cast<std::uint64_t>(kCycles) * kFrames) << "frames were lost between readings";
}
