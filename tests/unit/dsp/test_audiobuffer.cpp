// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/audioblock.h"
#include "dsp/audiobuffer.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

using namespace rb::dsp;

TEST(AudioBuffer, StartsZeroedAndKeepsItsShape)
{
    AudioBuffer buffer(2, 256);
    EXPECT_EQ(buffer.channelCount(), 2u);
    EXPECT_EQ(buffer.frames(), 256u);
    for (std::uint32_t channel = 0; channel < buffer.channelCount(); ++channel) {
        for (const float sample : buffer.channel(channel)) {
            EXPECT_EQ(sample, 0.0f);
        }
    }
}

TEST(AudioBuffer, ChannelsDoNotOverlap)
{
    AudioBuffer buffer(3, 64);
    for (std::uint32_t channel = 0; channel < 3; ++channel) {
        for (float &sample : buffer.channel(channel)) {
            sample = static_cast<float>(channel) + 1.0f;
        }
    }
    for (std::uint32_t channel = 0; channel < 3; ++channel) {
        for (const float sample : buffer.channel(channel)) {
            EXPECT_EQ(sample, static_cast<float>(channel) + 1.0f) << "in channel " << channel;
        }
    }
}

TEST(AudioBuffer, ChannelOutOfRangeIsEmpty)
{
    AudioBuffer buffer(1, 8);
    EXPECT_TRUE(buffer.channel(1).empty());
    EXPECT_TRUE(buffer.channel(99).empty());
}

TEST(AudioBuffer, ClearZeroesEveryChannel)
{
    AudioBuffer buffer(2, 16);
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        for (float &sample : buffer.channel(channel)) {
            sample = 0.5f;
        }
    }
    buffer.clear();
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        for (const float sample : buffer.channel(channel)) {
            EXPECT_EQ(sample, 0.0f);
        }
    }
}

TEST(AudioBuffer, ResizeKeepsTheViewsConsistent)
{
    AudioBuffer buffer;
    EXPECT_EQ(buffer.channelCount(), 0u);
    EXPECT_TRUE(buffer.block().isEmpty());

    buffer.resize(4, 128);
    const AudioBlock block = buffer.block();
    EXPECT_EQ(block.channelCount(), 4u);
    EXPECT_EQ(block.frames(), 128u);
    for (std::uint32_t channel = 0; channel < 4; ++channel) {
        EXPECT_FALSE(block.isSilent(channel));
        EXPECT_EQ(block.channel(channel).data(), buffer.channel(channel).data());
    }
}

TEST(AudioBlock, WholeRangeOfCycleLengthsWorks)
{
    // The engine promises every cycle length from kMinBlockFrames to
    // kMaxBlockFrames, so the extremes are the ones worth pinning down.
    AudioBuffer buffer(2, kMaxBlockFrames);
    for (const std::uint32_t frames : {kMinBlockFrames, 2u, 127u, 1024u, kMaxBlockFrames}) {
        const AudioBlock block = buffer.block(frames);
        EXPECT_EQ(block.frames(), frames);
        EXPECT_EQ(block.channel(0).size(), frames);
        EXPECT_FALSE(block.isEmpty());
    }
}

TEST(AudioBlock, WithFramesNeverGrowsTheView)
{
    AudioBuffer buffer(1, 32);
    EXPECT_EQ(buffer.block().withFrames(64).frames(), 32u);
    EXPECT_EQ(buffer.block().withFrames(8).frames(), 8u);
}

TEST(AudioBlock, ANullChannelPointerReadsAsSilence)
{
    // This is how an unconnected input port arrives: the channel is there,
    // but it has no storage.
    std::array<float, 4> right{};
    const std::array<float *, 2> channels{nullptr, right.data()};
    const AudioBlock block(channels.data(), 2, 4);

    EXPECT_TRUE(block.isSilent(0));
    EXPECT_TRUE(block.channel(0).empty());
    EXPECT_FALSE(block.isSilent(1));
    EXPECT_EQ(block.channel(1).size(), 4u);
}

TEST(AudioBlock, AChannelBeyondTheCountIsSilent)
{
    AudioBuffer buffer(1, 4);
    const AudioBlock block = buffer.block();
    EXPECT_TRUE(block.isSilent(1));
    EXPECT_TRUE(block.channel(1).empty());
}

TEST(AudioBlock, DefaultViewIsEmpty)
{
    const AudioBlock block;
    EXPECT_TRUE(block.isEmpty());
    EXPECT_EQ(block.channelCount(), 0u);
    EXPECT_EQ(block.frames(), 0u);
    EXPECT_TRUE(block.isSilent(0));
}
