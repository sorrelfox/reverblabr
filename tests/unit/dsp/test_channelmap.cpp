// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "analysis.h"
#include "dsp/audiobuffer.h"
#include "dsp/channelmap.h"
#include "dsp/decibel.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace rb::dsp;
using rb::test::peak;
using rb::test::sine;

namespace
{

/// What one channel of a layout, at full scale on its own, comes out as on
/// each side of the stereo bus.
///
/// The signal is a sine rather than a constant, so that a mistake which
/// happens to preserve the peak of a flat signal still shows up.
StereoContribution
measureDownmix(const ChannelLayout &layout, std::uint32_t channel, const DownmixOptions &options = {})
{
    const std::expected<ChannelMapper, ChannelMapError> mapper = ChannelMapper::toStereo(layout, options);
    EXPECT_TRUE(mapper.has_value());
    if (!mapper) {
        return {};
    }

    constexpr std::uint32_t kFrames = 480;
    AudioBuffer input(static_cast<std::uint32_t>(layout.size()), kFrames);
    AudioBuffer output(2, kFrames);
    const rb::test::Signal tone = sine(kFrames, 1000.0);
    const std::span<float> fed = input.channel(channel);
    for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
        fed[frame] = tone[frame];
    }

    mapper->process(input.block(), output.block(), kFrames);
    return StereoContribution{peak(output.channel(0)), peak(output.channel(1))};
}

/// The same, the other way round: the stereo bus at full scale on one side,
/// measured at every channel of the sink's layout.
std::vector<float>
measureUpmix(const ChannelLayout &layout, std::uint32_t stereoChannel, const UpmixOptions &options = {})
{
    const std::expected<ChannelMapper, ChannelMapError> mapper = ChannelMapper::fromStereo(layout, options);
    EXPECT_TRUE(mapper.has_value());
    if (!mapper) {
        return {};
    }

    constexpr std::uint32_t kFrames = 480;
    AudioBuffer input(2, kFrames);
    AudioBuffer output(static_cast<std::uint32_t>(layout.size()), kFrames);
    const rb::test::Signal tone = sine(kFrames, 1000.0);
    const std::span<float> fed = input.channel(stereoChannel);
    for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
        fed[frame] = tone[frame];
    }

    mapper->process(input.block(), output.block(), kFrames);
    std::vector<float> levels;
    for (std::uint32_t channel = 0; channel < layout.size(); ++channel) {
        levels.push_back(peak(output.channel(channel)));
    }
    return levels;
}

/// Asserts a measured gain is within a tenth of a decibel of what it should
/// be, which is what the acceptance criteria ask for.
void expectGain(float measured, float expected, std::string_view what)
{
    if (expected == 0.0f) {
        EXPECT_LT(measured, 1.0e-6f) << what << " should be silent";
        return;
    }
    EXPECT_NEAR(gainToDb(measured), gainToDb(expected), 0.1f)
        << what << ": measured " << measured << ", expected " << expected;
}

} // namespace

TEST(ChannelPosition, NamesMatchWhatPipeWireCalls)
{
    // These are the values of the `audio.channel` property a port arrives
    // carrying, so they are not ours to choose.
    EXPECT_EQ(channelPositionName(ChannelPosition::Mono), "MONO");
    EXPECT_EQ(channelPositionName(ChannelPosition::FrontLeft), "FL");
    EXPECT_EQ(channelPositionName(ChannelPosition::FrontRight), "FR");
    EXPECT_EQ(channelPositionName(ChannelPosition::FrontCentre), "FC");
    EXPECT_EQ(channelPositionName(ChannelPosition::LowFrequency), "LFE");
    EXPECT_EQ(channelPositionName(ChannelPosition::SideLeft), "SL");
    EXPECT_EQ(channelPositionName(ChannelPosition::RearRight), "RR");
    EXPECT_EQ(channelPositionName(ChannelPosition::Aux3), "AUX3");
    EXPECT_EQ(channelPositionName(ChannelPosition::Unknown), "UNK");

    EXPECT_EQ(channelPositionFromName("FL"), ChannelPosition::FrontLeft);
    EXPECT_EQ(channelPositionFromName("LFE"), ChannelPosition::LowFrequency);
    EXPECT_EQ(channelPositionFromName("AUX7"), ChannelPosition::Aux7);
    EXPECT_EQ(channelPositionFromName("SOMETHING"), ChannelPosition::Unknown);
    EXPECT_EQ(channelPositionFromName(""), ChannelPosition::Unknown);
}

TEST(ChannelPosition, AuxChannelsAreNumberedAndRunOut)
{
    EXPECT_EQ(auxChannel(0), ChannelPosition::Aux0);
    EXPECT_EQ(auxChannel(7), ChannelPosition::Aux7);
    EXPECT_EQ(auxChannel(8), ChannelPosition::Unknown);
}

TEST(ChannelLayout, TheWellKnownOnesAreWhatTheySay)
{
    EXPECT_EQ(monoLayout().size(), 1u);
    EXPECT_EQ(stereoLayout(), (ChannelLayout{ChannelPosition::FrontLeft, ChannelPosition::FrontRight}));
    EXPECT_EQ(surround51Layout().size(), 6u);
    EXPECT_EQ(surround71Layout().size(), 8u);
    EXPECT_EQ(auxLayout(4).size(), 4u);
    EXPECT_EQ(auxLayout(4).back(), ChannelPosition::Aux3);
}

TEST(ChannelMapper, RefusesALayoutItCannotMap)
{
    EXPECT_EQ(ChannelMapper::toStereo({}).error(), ChannelMapError::EmptyLayout);
    EXPECT_EQ(ChannelMapper::fromStereo({}).error(), ChannelMapError::EmptyLayout);

    const ChannelLayout tooMany(kMaxMappedChannels + 1, ChannelPosition::Unknown);
    EXPECT_EQ(ChannelMapper::toStereo(tooMany).error(), ChannelMapError::TooManyChannels);

    const ChannelLayout twice{ChannelPosition::FrontLeft, ChannelPosition::FrontLeft};
    EXPECT_EQ(ChannelMapper::toStereo(twice).error(), ChannelMapError::DuplicatePosition);
    EXPECT_EQ(channelMapErrorName(ChannelMapError::DuplicatePosition), "duplicatePosition");
}

// --- into the stereo bus ---

TEST(Downmix, AMonoInputReachesBothSidesAtTheLevelItWentInAt)
{
    const StereoContribution measured = measureDownmix(monoLayout(), 0);
    expectGain(measured.toLeft, 1.0f, "mono to left");
    expectGain(measured.toRight, 1.0f, "mono to right");
    EXPECT_FLOAT_EQ(measured.toLeft, measured.toRight) << "and at the same level on both";
}

TEST(Downmix, StereoPassesStraightThrough)
{
    const ChannelLayout layout = stereoLayout();
    const StereoContribution left = measureDownmix(layout, 0);
    expectGain(left.toLeft, 1.0f, "FL to left");
    expectGain(left.toRight, 0.0f, "FL to right");

    const StereoContribution right = measureDownmix(layout, 1);
    expectGain(right.toLeft, 0.0f, "FR to left");
    expectGain(right.toRight, 1.0f, "FR to right");
}

TEST(Downmix, FiveOneUsesTheDocumentedCoefficients)
{
    // FL, FR, FC, LFE, RL, RR — the table in channelmap.h, measured.
    const ChannelLayout layout = surround51Layout();
    struct Expected {
        std::uint32_t channel;
        const char *name;
        float toLeft;
        float toRight;
    };
    const Expected table[] = {
        {0, "FL", 1.0f, 0.0f},
        {1, "FR", 0.0f, 1.0f},
        {2, "FC", kFoldGain, kFoldGain},
        {3, "LFE", 0.0f, 0.0f},
        {4, "RL", kFoldGain, 0.0f},
        {5, "RR", 0.0f, kFoldGain},
    };
    for (const Expected &row : table) {
        const StereoContribution measured = measureDownmix(layout, row.channel);
        expectGain(measured.toLeft, row.toLeft, std::string(row.name) + " to left");
        expectGain(measured.toRight, row.toRight, std::string(row.name) + " to right");
    }
}

TEST(Downmix, SevenOneAddsTheSidesAtTheSameFold)
{
    const ChannelLayout layout = surround71Layout();
    const StereoContribution sideLeft = measureDownmix(layout, 6);
    expectGain(sideLeft.toLeft, kFoldGain, "SL to left");
    expectGain(sideLeft.toRight, 0.0f, "SL to right");

    const StereoContribution sideRight = measureDownmix(layout, 7);
    expectGain(sideRight.toLeft, 0.0f, "SR to left");
    expectGain(sideRight.toRight, kFoldGain, "SR to right");
}

TEST(Downmix, TheLowFrequencyChannelIsDroppedUnlessItIsAskedFor)
{
    const ChannelLayout layout = surround51Layout();
    EXPECT_EQ(measureDownmix(layout, 3), (StereoContribution{0.0f, 0.0f}));

    DownmixOptions options;
    options.includeLowFrequency = true;
    const StereoContribution included = measureDownmix(layout, 3, options);
    expectGain(included.toLeft, kFoldGain, "LFE to left when included");
    expectGain(included.toRight, kFoldGain, "LFE to right when included");
}

TEST(Downmix, AuxChannelsGoWhereTheBlockSaysTheyDo)
{
    const ChannelLayout layout = auxLayout(4);
    // The default: AUX0 left, AUX1 right, the rest dropped.
    expectGain(measureDownmix(layout, 0).toLeft, 1.0f, "AUX0 to left");
    expectGain(measureDownmix(layout, 0).toRight, 0.0f, "AUX0 to right");
    expectGain(measureDownmix(layout, 1).toRight, 1.0f, "AUX1 to right");
    EXPECT_EQ(measureDownmix(layout, 2), (StereoContribution{0.0f, 0.0f}));

    DownmixOptions options;
    options.aux[2] = AuxSide::Both;
    options.aux[3] = AuxSide::Left;
    const StereoContribution both = measureDownmix(layout, 2, options);
    expectGain(both.toLeft, kFoldGain, "AUX2 folded to left");
    expectGain(both.toRight, kFoldGain, "AUX2 folded to right");
    expectGain(measureDownmix(layout, 3, options).toLeft, 1.0f, "AUX3 sent left");
}

TEST(Downmix, AChannelWithNoKnownPositionCarriesNothing)
{
    const ChannelLayout layout{ChannelPosition::FrontLeft, ChannelPosition::Unknown};
    EXPECT_EQ(measureDownmix(layout, 1), (StereoContribution{0.0f, 0.0f}));
}

// --- out of the stereo bus ---

TEST(Upmix, StereoToStereoPassesStraightThrough)
{
    const std::vector<float> fromLeft = measureUpmix(stereoLayout(), 0);
    ASSERT_EQ(fromLeft.size(), 2u);
    expectGain(fromLeft[0], 1.0f, "left to FL");
    expectGain(fromLeft[1], 0.0f, "left to FR");
}

TEST(Upmix, CorrelatedFullScaleStereoIntoAMonoSinkStaysAtFullScale)
{
    // The thing PipeWire's summing gets wrong: linking L and R into one mono
    // port would come out 6 dB hot. Half of each is what keeps it in range.
    const std::expected<ChannelMapper, ChannelMapError> mapper = ChannelMapper::fromStereo(monoLayout());
    ASSERT_TRUE(mapper.has_value());
    EXPECT_FLOAT_EQ(mapper->coefficient(0, 0), kStereoToMonoGain);
    EXPECT_FLOAT_EQ(mapper->coefficient(1, 0), kStereoToMonoGain);

    constexpr std::uint32_t kFrames = 480;
    AudioBuffer input(2, kFrames);
    AudioBuffer output(1, kFrames);
    const rb::test::Signal tone = sine(kFrames, 1000.0, 48000.0, 1.0f);
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        const std::span<float> fed = input.channel(channel);
        for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
            fed[frame] = tone[frame]; // the same signal on both: fully correlated
        }
    }

    mapper->process(input.block(), output.block(), kFrames);
    const float level = peak(output.channel(0));
    EXPECT_LE(gainToDb(level), 0.0f) << "it came out at " << gainToDb(level) << " dBFS";
    expectGain(level, 1.0f, "correlated stereo into mono");
}

TEST(Upmix, UncorrelatedStereoIntoAMonoSinkLosesTheDifference)
{
    // Two signals in antiphase cancel, which is what summing to mono means
    // and is not something the mapper should be hiding.
    const std::expected<ChannelMapper, ChannelMapError> mapper = ChannelMapper::fromStereo(monoLayout());
    ASSERT_TRUE(mapper.has_value());

    constexpr std::uint32_t kFrames = 480;
    AudioBuffer input(2, kFrames);
    AudioBuffer output(1, kFrames);
    const rb::test::Signal tone = sine(kFrames, 1000.0);
    for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
        input.channel(0)[frame] = tone[frame];
        input.channel(1)[frame] = -tone[frame];
    }

    mapper->process(input.block(), output.block(), kFrames);
    EXPECT_LT(peak(output.channel(0)), 1.0e-6f);
}

TEST(Upmix, ASurroundSinkGetsTheFrontPairAndNothingInvented)
{
    const ChannelLayout layout = surround51Layout();
    const std::vector<float> fromLeft = measureUpmix(layout, 0);
    ASSERT_EQ(fromLeft.size(), 6u);
    expectGain(fromLeft[0], 1.0f, "left to FL");
    for (std::size_t channel = 1; channel < fromLeft.size(); ++channel) {
        expectGain(fromLeft[channel], 0.0f, "left to channel " + std::to_string(channel));
    }

    const std::vector<float> fromRight = measureUpmix(layout, 1);
    expectGain(fromRight[1], 1.0f, "right to FR");
    expectGain(fromRight[2], 0.0f, "right to FC");
}

TEST(Upmix, AnAuxSinkTakesWhatTheBlockAssignsIt)
{
    const ChannelLayout layout = auxLayout(3);
    const std::vector<float> fromLeft = measureUpmix(layout, 0);
    ASSERT_EQ(fromLeft.size(), 3u);
    expectGain(fromLeft[0], 1.0f, "left to AUX0");
    expectGain(fromLeft[1], 0.0f, "left to AUX1");
    expectGain(fromLeft[2], 0.0f, "left to AUX2");

    UpmixOptions options;
    options.aux[2] = AuxSide::Both;
    const std::vector<float> folded = measureUpmix(layout, 0, options);
    expectGain(folded[2], kFoldGain, "left folded to AUX2");
}

// --- the mapper itself ---

TEST(ChannelMapper, ReportsTheCoefficientsItWasBuiltWith)
{
    const std::expected<ChannelMapper, ChannelMapError> mapper = ChannelMapper::toStereo(surround51Layout());
    ASSERT_TRUE(mapper.has_value());
    EXPECT_EQ(mapper->inputChannels(), 6u);
    EXPECT_EQ(mapper->outputChannels(), 2u);
    EXPECT_FLOAT_EQ(mapper->coefficient(0, 0), 1.0f);
    EXPECT_FLOAT_EQ(mapper->coefficient(2, 0), kFoldGain);
    EXPECT_FLOAT_EQ(mapper->coefficient(3, 0), 0.0f) << "the LFE is dropped";
    EXPECT_FLOAT_EQ(mapper->coefficient(99, 0), 0.0f) << "and out of range is nothing";
    EXPECT_FLOAT_EQ(mapper->coefficient(0, 99), 0.0f);
}

TEST(ChannelMapper, AnInputPortWithNoStorageIsSilence)
{
    // An unconnected port arrives with no buffer. It must not be read, and
    // the output still has to be written.
    const std::expected<ChannelMapper, ChannelMapError> mapper = ChannelMapper::toStereo(stereoLayout());
    ASSERT_TRUE(mapper.has_value());

    constexpr std::uint32_t kFrames = 64;
    AudioBuffer right(1, kFrames);
    for (float &sample : right.channel(0)) {
        sample = 0.5f;
    }
    std::array<float *, 2> channels{nullptr, right.channel(0).data()};
    const AudioBlock input(channels.data(), 2, kFrames);

    AudioBuffer output(2, kFrames);
    for (float &sample : output.channel(0)) {
        sample = 99.0f; // must be overwritten, not left
    }
    mapper->process(input, output.block(), kFrames);

    EXPECT_LT(peak(output.channel(0)), 1.0e-6f);
    EXPECT_NEAR(peak(output.channel(1)), 0.5f, 1.0e-6f);
}

TEST(ChannelMapper, MapsEveryCycleLengthTheSameWay)
{
    const std::expected<ChannelMapper, ChannelMapError> mapper = ChannelMapper::toStereo(surround51Layout());
    ASSERT_TRUE(mapper.has_value());

    constexpr std::uint32_t kFrames = 256;
    AudioBuffer input(6, kFrames);
    AudioBuffer output(2, kFrames);
    const rb::test::Signal tone = sine(kFrames, 500.0);
    for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
        input.channel(2)[frame] = tone[frame]; // the centre channel alone
    }

    for (const std::uint32_t frames : {1u, 7u, 64u, kFrames}) {
        output.clear();
        mapper->process(input.block(), output.block(), frames);
        const std::span<const float> heard = output.channel(0).first(frames);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            EXPECT_NEAR(heard[frame], tone[frame] * kFoldGain, 1.0e-6f) << "in cycles of " << frames;
        }
    }
}
