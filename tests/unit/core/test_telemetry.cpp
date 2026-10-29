// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The sampler with nothing to sample, and the shape of what it publishes.
// What it makes of a running session is measured against a real one
// in tests/pw/test_engine.cpp, where there is sound to measure.

#include "core/telemetry.h"

#include <QCoreApplication>
#include <gtest/gtest.h>

#include <array>
#include <type_traits>

using namespace rb::core;

TEST(TelemetryTest, ASamplerWithNothingToWatchPublishesSilence)
{
    TelemetrySampler sampler;
    sampler.sample();

    const TelemetrySnapshot snapshot = sampler.slot().load();
    EXPECT_FALSE(snapshot.running);
    EXPECT_EQ(snapshot.blockCount, 0U);
    EXPECT_EQ(snapshot.xruns, 0U);
    EXPECT_GT(snapshot.nanos, 0U) << "a snapshot should say when it was taken";
}

TEST(TelemetryTest, EverySnapshotIsNumbered)
{
    // A reader tells a new snapshot from the one it has already drawn by the
    // number and nothing else, so the number has to count up whatever is
    // happening — including nothing.
    TelemetrySampler sampler;
    sampler.sample();
    const std::uint64_t first = sampler.slot().load().frame;
    sampler.sample();
    const std::uint64_t second = sampler.slot().load().frame;

    EXPECT_EQ(second, first + 1);
    EXPECT_GT(first, 0U) << "a frame number of zero is what a reader starts at";
}

TEST(TelemetryTest, StoppingTakesOneLastSnapshot)
{
    // A timer needs a thread with an event dispatcher on it, which is what
    // an application object gives a test that has no event loop of its own.
    int argc = 1;
    char name[] = "telemetry";
    std::array<char *, 2> argv{name, nullptr};
    const QCoreApplication app(argc, argv.data());

    TelemetrySampler sampler;
    sampler.start();
    EXPECT_TRUE(sampler.isRunning());
    const std::uint64_t before = sampler.slot().load().frame;

    sampler.stop();
    EXPECT_FALSE(sampler.isRunning());
    // The last snapshot is what takes the meters on the screen down, rather
    // than leaving them where the music stopped.
    EXPECT_GT(sampler.slot().load().frame, before);
    EXPECT_FALSE(sampler.slot().load().running);
}

TEST(TelemetryTest, TheSnapshotIsSomethingThatCanBeCopiedByteForByte)
{
    // It travels through a seqlock today and through a shared-memory region
    // when the engine moves into a daemon; neither can carry anything that
    // owns anything.
    EXPECT_TRUE(std::is_trivially_copyable_v<TelemetrySnapshot>);
    EXPECT_TRUE(std::is_trivially_copyable_v<BlockTelemetry>);
}

TEST(TelemetryTest, TheRateLeavesRoomForTheHundredMillisecondsTheInterfaceIsAllowed)
{
    // Activity flags have to follow the sound within 100 ms. A snapshot every
    // 16 ms leaves the interface the rest of that budget to draw in.
    EXPECT_LE(TelemetrySampler::kPeriod.count(), 33);
}

namespace
{

[[nodiscard]] rb::rt::MeterReading measuredAt(float peak)
{
    rb::rt::MeterReading reading;
    reading.peak = peak;
    reading.rms = peak * 0.7071f;
    reading.frames = 1024;
    return reading;
}

constexpr std::uint64_t kHold = 40'000'000; // 40 ms

} // namespace

TEST(TelemetryTest, ABlockIsNotShownUntilEveryChannelHasBeenMeasured)
{
    // The audio thread measures a block's channels one after another
    // and the sampler takes them one after another, so a tick can find the
    // right-hand side measured and the left not yet. Published as it was,
    // the block was active with a left side at nothing, and the meter flicker
    // test read −120 dB under a light that said sound.
    rb::core::HeldLevels held;
    rb::core::BlockTelemetry first;
    const std::array<rb::rt::MeterReading, 2> onlyRight{rb::rt::MeterReading{}, measuredAt(0.25f)};
    rb::core::foldReadings(held, onlyRight, 1'000'000, kHold, first);
    EXPECT_FALSE(first.active) << "a block was shown active with one side not measured yet";
    EXPECT_FLOAT_EQ(first.peak[0], 0.0f);
    EXPECT_FLOAT_EQ(first.peak[1], 0.0f);

    // The next tick has the left: both sides, and active.
    rb::core::BlockTelemetry second;
    const std::array<rb::rt::MeterReading, 2> onlyLeft{measuredAt(0.25f), rb::rt::MeterReading{}};
    rb::core::foldReadings(held, onlyLeft, 17'000'000, kHold, second);
    EXPECT_TRUE(second.active);
    EXPECT_FLOAT_EQ(second.peak[0], 0.25f);
    EXPECT_FLOAT_EQ(second.peak[1], 0.25f) << "the right side's level was not carried forward";

    // From then on a tick that finds one side taken already is no news: the
    // other side's last level is carried, as it always was.
    rb::core::BlockTelemetry third;
    rb::core::foldReadings(held, onlyRight, 33'000'000, kHold, third);
    EXPECT_TRUE(third.active);
    EXPECT_FLOAT_EQ(third.peak[0], 0.25f);

    // Gone quiet for real — nothing measured for longer than the hold — and
    // started again: the same wait for every channel.
    rb::core::BlockTelemetry stale;
    rb::core::foldReadings(held, std::array<rb::rt::MeterReading, 2>{}, 200'000'000, kHold, stale);
    EXPECT_FALSE(stale.active);
    rb::core::BlockTelemetry restarted;
    rb::core::foldReadings(held, onlyRight, 216'000'000, kHold, restarted);
    EXPECT_FALSE(restarted.active) << "a block starting again was shown with one side at nothing";
    EXPECT_FLOAT_EQ(restarted.peak[0], 0.0f);
}
