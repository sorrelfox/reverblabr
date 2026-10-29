// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "rt/audiotap.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <random>
#include <thread>
#include <vector>

using namespace rb::rt;

namespace
{

/// A sample that says which frame and channel it is, exactly: every value
/// here is an integer well inside a float's 24 bits.
[[nodiscard]] float valueOf(std::uint64_t frame, std::uint32_t channel)
{
    return static_cast<float>((frame % 1000000) * 2 + channel);
}

/// Writes \a frames frames starting at \a first, stereo.
bool writeFrames(AudioTap &tap, std::uint64_t first, std::uint32_t frames)
{
    std::vector<float> left(frames);
    std::vector<float> right(frames);
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        left[frame] = valueOf(first + frame, 0);
        right[frame] = valueOf(first + frame, 1);
    }
    const std::array<const float *, 2> channels{left.data(), right.data()};
    return tap.write(channels, frames);
}

} // namespace

TEST(AudioTap, GivesBackWhatItWasGivenInOrderAcrossTheEndOfItsStorage)
{
    AudioTap tap(2, 100);
    std::vector<float> out(2 * 100);
    std::uint64_t written = 0;
    std::uint64_t checked = 0;
    // 37 frames at a time into 100 frames of storage: the third write goes
    // round the end.
    for (int round = 0; round < 10; ++round) {
        ASSERT_TRUE(writeFrames(tap, written, 37));
        written += 37;
        const std::size_t got = tap.read(out, 100);
        ASSERT_EQ(got, 37U);
        for (std::size_t frame = 0; frame < got; ++frame) {
            ASSERT_EQ(out[frame * 2], valueOf(checked, 0));
            ASSERT_EQ(out[frame * 2 + 1], valueOf(checked, 1));
            ++checked;
        }
    }
    EXPECT_EQ(tap.framesWritten(), 370U);
    EXPECT_EQ(tap.framesRead(), 370U);
    EXPECT_EQ(tap.droppedFrames(), 0U);
}

TEST(AudioTap, AChannelWithNoStorageIsSilence)
{
    AudioTap tap(2, 16);
    const std::array<float, 4> left{0.1f, 0.2f, 0.3f, 0.4f};
    const std::array<const float *, 2> channels{left.data(), nullptr};
    ASSERT_TRUE(tap.write(channels, 4));
    std::array<float, 8> out{};
    ASSERT_EQ(tap.read(out, 4), 4U);
    EXPECT_EQ(out, (std::array<float, 8>{0.1f, 0.0f, 0.2f, 0.0f, 0.3f, 0.0f, 0.4f, 0.0f}));
}

TEST(AudioTap, ACycleThatDoesNotFitIsDroppedWholeAndCounted)
{
    AudioTap tap(2, 10);
    ASSERT_TRUE(writeFrames(tap, 0, 8));
    // Four more would need twelve: none of them go in, and nothing that was
    // there is touched.
    EXPECT_FALSE(writeFrames(tap, 8, 4));
    EXPECT_EQ(tap.droppedFrames(), 4U);
    EXPECT_EQ(tap.readableFrames(), 8U);

    std::vector<float> out(20);
    ASSERT_EQ(tap.read(out, 10), 8U);
    EXPECT_EQ(out[14], valueOf(7, 0));
    // Room again: the next cycle goes in.
    EXPECT_TRUE(writeFrames(tap, 12, 4));
}

TEST(AudioTap, ARateIsSaidOnceAndPinnedToTheFrameItStartsAt)
{
    AudioTap tap(2, 1000);
    tap.announceRate(48000);
    ASSERT_TRUE(writeFrames(tap, 0, 10));
    // A block replaced by a fresh one at the same rate says it again, and
    // that must not split anybody's file.
    tap.announceRate(48000);
    ASSERT_TRUE(writeFrames(tap, 10, 10));
    tap.announceRate(44100);

    TapMarker marker;
    ASSERT_TRUE(tap.popMarker(marker));
    EXPECT_EQ(marker.atFrame, 0U);
    EXPECT_EQ(marker.value, 48000U);
    ASSERT_TRUE(tap.popMarker(marker));
    EXPECT_EQ(marker.atFrame, 20U);
    EXPECT_EQ(marker.value, 44100U);
    EXPECT_FALSE(tap.popMarker(marker));
}

TEST(AudioTap, DiscardLetsGoOfFramesWithoutReadingThem)
{
    AudioTap tap(2, 100);
    ASSERT_TRUE(writeFrames(tap, 0, 50));
    EXPECT_EQ(tap.discard(30), 30U);
    std::vector<float> out(200);
    ASSERT_EQ(tap.read(out, 100), 20U);
    EXPECT_EQ(out[0], valueOf(30, 0));
}

// **Two threads, every sample, in order.** The producer is held back only
// when the tap is full — which the audio thread never is, but a test that
// runs faster than real time has to be — so what this checks is that
// nothing is lost, doubled or reordered between them.
TEST(AudioTap, TwoThreadsPassEverySampleInOrder)
{
    constexpr std::uint64_t kFrames = 2'000'000;
    AudioTap tap(2, 4096);

    std::thread producer([&tap] {
        std::mt19937 random(7);
        std::uniform_int_distribution<std::uint32_t> cycle(1, 512);
        std::uint64_t written = 0;
        while (written < kFrames) {
            const auto frames = static_cast<std::uint32_t>(std::min<std::uint64_t>(cycle(random), kFrames - written));
            while (tap.capacityFrames() - tap.readableFrames() < frames) {
                std::this_thread::yield();
            }
            ASSERT_TRUE(writeFrames(tap, written, frames));
            written += frames;
        }
    });

    std::vector<float> out(2 * 1000);
    std::uint64_t checked = 0;
    std::uint64_t wrong = 0;
    while (checked < kFrames) {
        const std::size_t got = tap.read(out, 1000);
        if (got == 0) {
            std::this_thread::yield();
            continue;
        }
        for (std::size_t frame = 0; frame < got; ++frame, ++checked) {
            wrong += out[frame * 2] != valueOf(checked, 0) || out[frame * 2 + 1] != valueOf(checked, 1) ? 1 : 0;
        }
    }
    producer.join();
    EXPECT_EQ(wrong, 0U);
    EXPECT_EQ(tap.droppedFrames(), 0U);
}
