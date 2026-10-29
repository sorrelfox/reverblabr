// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/audiobuffer.h"
#include "dsp/delayline.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

using namespace rb::dsp;

namespace
{

/// A ramp 1, 2, 3, ... so that every sample says where it came from.
std::vector<float> counting(std::uint32_t frames, float first = 1.0f)
{
    std::vector<float> samples(frames);
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        samples[frame] = first + static_cast<float>(frame);
    }
    return samples;
}

/// Runs \a input through \a line one cycle of \a cycleFrames at a time, and
/// returns everything that came out.
std::vector<float> runInCycles(DelayLine &line, const std::vector<float> &input, std::uint32_t cycleFrames)
{
    std::vector<float> output;
    output.reserve(input.size());
    AudioBuffer buffer(1, cycleFrames);
    for (std::size_t offset = 0; offset < input.size(); offset += cycleFrames) {
        const auto frames = static_cast<std::uint32_t>(std::min<std::size_t>(cycleFrames, input.size() - offset));
        const std::span<float> channel = buffer.channel(0);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            channel[frame] = input[offset + frame];
        }
        line.process(buffer.block(frames));
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            output.push_back(channel[frame]);
        }
    }
    return output;
}

} // namespace

TEST(DelayLine, DelaysByTheRequestedNumberOfSamples)
{
    constexpr std::uint32_t kDelay = 5;
    DelayLine line;
    line.prepare(1, 32);
    line.setDelaySamples(kDelay);
    EXPECT_EQ(line.delaySamples(), kDelay);

    const std::vector<float> input = counting(20);
    const std::vector<float> output = runInCycles(line, input, 20);

    for (std::uint32_t frame = 0; frame < kDelay; ++frame) {
        EXPECT_EQ(output[frame], 0.0f) << "the line starts empty, so it begins with silence";
    }
    for (std::size_t frame = kDelay; frame < output.size(); ++frame) {
        EXPECT_EQ(output[frame], input[frame - kDelay]) << "at frame " << frame;
    }
}

TEST(DelayLine, AZeroDelayPassesTheSignalThrough)
{
    DelayLine line;
    line.prepare(1, 16);
    line.setDelaySamples(0);
    const std::vector<float> input = counting(10);
    EXPECT_EQ(runInCycles(line, input, 10), input);
}

TEST(DelayLine, TheMaximumDelayWorksAsWell)
{
    constexpr std::uint32_t kMaxDelay = 8;
    DelayLine line;
    line.prepare(1, kMaxDelay);
    EXPECT_EQ(line.delaySamples(), kMaxDelay) << "prepare() starts at the longest delay";

    const std::vector<float> input = counting(24);
    const std::vector<float> output = runInCycles(line, input, 24);
    for (std::size_t frame = kMaxDelay; frame < output.size(); ++frame) {
        EXPECT_EQ(output[frame], input[frame - kMaxDelay]) << "at frame " << frame;
    }
}

TEST(DelayLine, AskingForMoreThanTheMaximumIsClamped)
{
    DelayLine line;
    line.prepare(1, 4);
    line.setDelaySamples(1000);
    EXPECT_EQ(line.delaySamples(), 4u);
}

TEST(DelayLine, TheCycleLengthDoesNotChangeTheResult)
{
    // PipeWire's quantum changes while a session runs, so the same signal has
    // to come out the same however it is chopped up.
    constexpr std::uint32_t kDelay = 37;
    const std::vector<float> input = counting(500);

    DelayLine reference;
    reference.prepare(1, 64);
    reference.setDelaySamples(kDelay);
    const std::vector<float> expected = runInCycles(reference, input, 500);

    for (const std::uint32_t cycleFrames : {1u, 2u, 7u, 64u, 128u, 499u}) {
        DelayLine line;
        line.prepare(1, 64);
        line.setDelaySamples(kDelay);
        EXPECT_EQ(runInCycles(line, input, cycleFrames), expected) << "in cycles of " << cycleFrames;
    }
}

TEST(DelayLine, ACycleLongerThanTheLineStillWorks)
{
    constexpr std::uint32_t kDelay = 3;
    DelayLine line;
    line.prepare(1, kDelay);
    line.setDelaySamples(kDelay);

    const std::vector<float> input = counting(100);
    const std::vector<float> output = runInCycles(line, input, 100);
    for (std::size_t frame = kDelay; frame < output.size(); ++frame) {
        EXPECT_EQ(output[frame], input[frame - kDelay]) << "at frame " << frame;
    }
}

TEST(DelayLine, ChannelsAreDelayedIndependently)
{
    constexpr std::uint32_t kDelay = 4;
    DelayLine line;
    line.prepare(2, 16);
    line.setDelaySamples(kDelay);

    AudioBuffer buffer(2, 12);
    for (std::uint32_t frame = 0; frame < 12; ++frame) {
        buffer.channel(0)[frame] = static_cast<float>(frame) + 1.0f;
        buffer.channel(1)[frame] = -(static_cast<float>(frame) + 1.0f);
    }
    line.process(buffer.block());

    for (std::uint32_t frame = 0; frame < kDelay; ++frame) {
        EXPECT_EQ(buffer.channel(0)[frame], 0.0f);
        EXPECT_EQ(buffer.channel(1)[frame], 0.0f);
    }
    for (std::uint32_t frame = kDelay; frame < 12; ++frame) {
        EXPECT_EQ(buffer.channel(0)[frame], static_cast<float>(frame - kDelay) + 1.0f);
        EXPECT_EQ(buffer.channel(1)[frame], -(static_cast<float>(frame - kDelay) + 1.0f));
    }
}

TEST(DelayLine, AChannelThatFallsSilentDoesNotComeBackLater)
{
    // An unconnected port arrives as a channel with no storage. If the line
    // kept what the channel held before, that audio would be heard again when
    // the port is connected once more.
    constexpr std::uint32_t kDelay = 4;
    DelayLine line;
    line.prepare(1, kDelay);
    line.setDelaySamples(kDelay);

    AudioBuffer buffer(1, 4);
    const std::span<float> channel = buffer.channel(0);
    for (std::uint32_t frame = 0; frame < 4; ++frame) {
        channel[frame] = 1.0f;
    }
    line.process(buffer.block());

    // Four frames with no storage at all: the line must take in silence.
    std::array<float *, 1> silentChannels{nullptr};
    line.process(AudioBlock(silentChannels.data(), 1, 4));

    for (std::uint32_t frame = 0; frame < 4; ++frame) {
        channel[frame] = 0.0f;
    }
    line.process(buffer.block());
    for (std::uint32_t frame = 0; frame < 4; ++frame) {
        EXPECT_EQ(channel[frame], 0.0f) << "at frame " << frame;
    }
}

TEST(DelayLine, ChannelsBeyondTheLineAreLeftAlone)
{
    DelayLine line;
    line.prepare(1, 4);
    line.setDelaySamples(2);

    AudioBuffer buffer(2, 6);
    for (std::uint32_t frame = 0; frame < 6; ++frame) {
        buffer.channel(0)[frame] = 1.0f;
        buffer.channel(1)[frame] = 2.0f;
    }
    line.process(buffer.block());

    EXPECT_EQ(buffer.channel(0)[0], 0.0f) << "the prepared channel is delayed";
    for (std::uint32_t frame = 0; frame < 6; ++frame) {
        EXPECT_EQ(buffer.channel(1)[frame], 2.0f) << "the extra channel passes through untouched";
    }
}

TEST(DelayLine, ResetSilencesWhatIsStillInTheLine)
{
    DelayLine line;
    line.prepare(1, 8);
    line.setDelaySamples(8);

    AudioBuffer buffer(1, 8);
    for (float &sample : buffer.channel(0)) {
        sample = 1.0f;
    }
    line.process(buffer.block());

    line.reset();
    for (float &sample : buffer.channel(0)) {
        sample = 0.0f;
    }
    line.process(buffer.block());
    for (const float sample : buffer.channel(0)) {
        EXPECT_EQ(sample, 0.0f);
    }
}

TEST(DelayLine, ChannelAtATimeMatchesTheWholeBlock)
{
    constexpr std::uint32_t kDelay = 6;
    const std::vector<float> input = counting(40);

    DelayLine whole;
    whole.prepare(1, 16);
    whole.setDelaySamples(kDelay);
    const std::vector<float> expected = runInCycles(whole, input, 8);

    DelayLine perChannel;
    perChannel.prepare(1, 16);
    perChannel.setDelaySamples(kDelay);
    std::vector<float> output;
    output.reserve(input.size());
    std::array<float, 8> cycle{};
    for (std::size_t offset = 0; offset < input.size(); offset += cycle.size()) {
        std::copy_n(input.begin() + static_cast<std::ptrdiff_t>(offset), cycle.size(), cycle.begin());
        perChannel.processChannel(0, cycle);
        perChannel.endFrames(static_cast<std::uint32_t>(cycle.size()));
        output.insert(output.end(), cycle.begin(), cycle.end());
    }
    EXPECT_EQ(output, expected);
}

TEST(DelayLine, AnUnpreparedLineDoesNothing)
{
    DelayLine line;
    AudioBuffer buffer(1, 4);
    for (float &sample : buffer.channel(0)) {
        sample = 1.0f;
    }
    line.process(buffer.block());
    for (const float sample : buffer.channel(0)) {
        EXPECT_EQ(sample, 1.0f) << "with no storage the block has to come through untouched";
    }
}

TEST(DelayLine, TwoHeadsReadTwoDelaysOfTheSameSignal)
{
    // The old delay and the new one read at once, for as long as a
    // fade between them lasts.
    DelayLine line;
    line.prepare(1, 16);
    // **Written into room made beforehand**, because the mix is called from
    // an RB_NONBLOCKING function and has to be nonblocking too: a
    // push_back() here is an allocation Clang refuses to build.
    std::array<float, 40> first{};
    std::array<float, 40> second{};
    std::size_t heard = 0;
    const std::vector<float> input = counting(40);
    for (std::uint32_t start = 0; start < 40; start += 8) {
        std::vector<float> cycle(input.begin() + start, input.begin() + start + 8);
        line.processChannelTwoTaps(0, cycle, 3, 10, [&](float a, float b) noexcept RB_NONBLOCKING {
            first[heard] = a;
            second[heard] = b;
            ++heard;
            return a - b;
        });
        for (std::uint32_t frame = 0; frame < 8; ++frame) {
            EXPECT_EQ(cycle[frame], first[start + frame] - second[start + frame]) << "the mix is not what came out";
        }
        line.endFrames(8);
    }
    for (std::uint32_t frame = 0; frame < 40; ++frame) {
        EXPECT_EQ(first[frame], frame >= 3 ? input[frame - 3] : 0.0f) << "head A at " << frame;
        EXPECT_EQ(second[frame], frame >= 10 ? input[frame - 10] : 0.0f) << "head B at " << frame;
    }
}

TEST(DelayLine, TwoHeadsAtZeroAndPastTheMaximum)
{
    DelayLine line;
    line.prepare(1, 4);
    std::vector<float> cycle = counting(12);
    std::array<float, 12> heads{};
    std::size_t heard = 0;
    line.processChannelTwoTaps(0, cycle, 0, 100, [&](float a, float b) noexcept RB_NONBLOCKING {
        heads[heard++] = b;
        return a;
    });
    // A delay of zero is the sample itself; one past the maximum is the
    // maximum.
    EXPECT_EQ(cycle, counting(12));
    for (std::uint32_t frame = 4; frame < 12; ++frame) {
        EXPECT_EQ(heads[frame], static_cast<float>(frame - 4 + 1));
    }
    EXPECT_EQ(line.delaySamples(), 4U) << "the line's own delay was changed";
}
