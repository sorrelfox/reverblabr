// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Application source block on its own, with no PipeWire and no
// application: what it does to the channels it is given. Finding the
// application and linking its streams is the engine's half, and is measured
// against a real server in tests/pw.

#include "blocks/applicationsource.h"
#include "blocksizeinvariance.h"
#include "offlinerunner.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <memory>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

/// Runs a signal through a block prepared for one layout, and gives back the
/// stereo pair it produced.
[[nodiscard]] MultiSignal through(ApplicationSource &block, const ChannelLayout &layout, const MultiSignal &input)
{
    BlockRunner runner(block, static_cast<std::uint32_t>(layout.size()), 2);
    runner.setInputLayout(layout);
    EXPECT_TRUE(runner.prepare().has_value());
    return runner.run(input);
}

/// A steady level on every channel of a layout, so that what came out of
/// each one can be read off the result.
[[nodiscard]] MultiSignal constantChannels(std::size_t channels, std::size_t frames, float value)
{
    MultiSignal signal;
    for (std::size_t channel = 0; channel < channels; ++channel) {
        signal.push_back(Signal(frames, value));
    }
    return signal;
}

} // namespace

TEST(ApplicationSourceTest, IsASourceWithAStereoBusOut)
{
    const ApplicationSource block;
    EXPECT_EQ(block.info().typeId, "source.application");
    EXPECT_EQ(block.info().category, BlockCategory::Sources);
    // Its input ports follow whatever the application is playing, up to the
    // widest layout the mapping policy handles.
    EXPECT_EQ(block.info().inputs.minCount, 1U);
    EXPECT_EQ(block.info().inputs.maxCount, kMaxMappedChannels);
    EXPECT_EQ(block.info().outputs.minCount, 2U);
    EXPECT_EQ(block.info().outputs.maxCount, 2U);
    EXPECT_EQ(block.info().latencySamples, 0U);
}

TEST(ApplicationSourceTest, TheMatchRuleIsWhatTheSessionKeeps)
{
    ApplicationSource block;
    ASSERT_TRUE(block.setParam("binary", ParamValue::ofString("firefox")).has_value());
    ASSERT_TRUE(block.setParam("flatpakId", ParamValue::ofString("org.mozilla.firefox")).has_value());
    ASSERT_TRUE(block.setParam("name", ParamValue::ofString("Firefox")).has_value());

    const ParamValueMap saved = block.saveState();
    EXPECT_EQ(saved.at("binary").asString(), "firefox");
    EXPECT_EQ(saved.at("flatpakId").asString(), "org.mozilla.firefox");
    EXPECT_EQ(saved.at("name").asString(), "Firefox");

    // And it comes back the same, because that is what makes a saved session
    // find the application again next week.
    ApplicationSource opened;
    ASSERT_TRUE(opened.loadState(saved).has_value());
    EXPECT_EQ(opened.param("binary").text(), "firefox");
    EXPECT_EQ(opened.param("name").text(), "Firefox");
}

TEST(ApplicationSourceTest, AStereoStreamComesThroughUntouched)
{
    ApplicationSource block;
    MultiSignal input;
    input.push_back(sine(512, 1000.0, kTestSampleRate, 0.5f));
    input.push_back(sine(512, 1000.0, kTestSampleRate, 0.25f));

    const MultiSignal out = through(block, stereoLayout(), input);
    ASSERT_EQ(out.size(), 2U);
    EXPECT_TRUE(nulls(out[0], input[0])) << "the left side was not passed through";
    EXPECT_TRUE(nulls(out[1], input[1])) << "the right side was not passed through";
}

TEST(ApplicationSourceTest, AMonoStreamIsHeardOnBothSides)
{
    ApplicationSource block;
    MultiSignal input;
    input.push_back(sine(512, 1000.0, kTestSampleRate, 0.5f));

    const MultiSignal out = through(block, monoLayout(), input);
    ASSERT_EQ(out.size(), 2U);
    // A mono channel goes to both sides at unity: it is the whole signal,
    // not half of a stereo pair ([ADR-0007] and the mapping policy).
    EXPECT_TRUE(nulls(out[0], input[0]));
    EXPECT_TRUE(nulls(out[1], input[0]));
}

TEST(ApplicationSourceTest, ASurroundStreamIsFoldedByTheDocumentedCoefficients)
{
    ApplicationSource block;
    // FL, FR, FC, LFE, RL, RR, each at a level of its own so the arithmetic
    // can be read off the result.
    const std::size_t frames = 256;
    MultiSignal input;
    for (int channel = 0; channel < 6; ++channel) {
        input.push_back(Signal(frames, 0.1f * static_cast<float>(channel + 1)));
    }

    const MultiSignal out = through(block, surround51Layout(), input);
    ASSERT_EQ(out.size(), 2U);

    // The policy, written out: front left and right at unity, centre and the
    // rears folded at −3 dB into both sides, and the low-frequency channel
    // dropped.
    const float left = 0.1f + kFoldGain * 0.3f + kFoldGain * 0.5f;
    const float right = 0.2f + kFoldGain * 0.3f + kFoldGain * 0.6f;
    EXPECT_NEAR(out[0].front(), left, 1e-5f);
    EXPECT_NEAR(out[1].front(), right, 1e-5f);
    // And each coefficient is the one the policy says it is.
    EXPECT_EQ(downmixContribution(ChannelPosition::LowFrequency).toLeft, 0.0f);
}

TEST(ApplicationSourceTest, TheLowFrequencyChannelIsFoldedInWhenAskedFor)
{
    ApplicationSource block;
    ASSERT_TRUE(block.setParam("includeLowFrequency", ParamValue::ofBool(true)).has_value());

    const std::size_t frames = 256;
    MultiSignal input = constantChannels(6, frames, 0.0f);
    input[3].assign(frames, 0.4f); // LFE alone

    const MultiSignal out = through(block, surround51Layout(), input);
    EXPECT_NEAR(out[0].front(), kFoldGain * 0.4f, 1e-5f);
    EXPECT_NEAR(out[1].front(), kFoldGain * 0.4f, 1e-5f);
}

TEST(ApplicationSourceTest, AnUnknownLayoutIsStillHeard)
{
    // Four AUX channels, which is what a stream on an unlabelled sink looks
    // like: the first two are the stereo pair and the rest are dropped.
    ApplicationSource block;
    const std::size_t frames = 128;
    MultiSignal input = constantChannels(4, frames, 0.0f);
    input[0].assign(frames, 0.5f);
    input[1].assign(frames, 0.25f);
    input[2].assign(frames, 1.0f);

    const MultiSignal out = through(block, auxLayout(4), input);
    EXPECT_NEAR(out[0].front(), 0.5f, 1e-5f);
    EXPECT_NEAR(out[1].front(), 0.25f, 1e-5f);
}

TEST(ApplicationSourceTest, ChangingTheLayoutChangesNothingElse)
{
    // An application that moves from a stereo sink to a 5.1 one is prepared
    // again, and what it carries must be right from the first cycle.
    ApplicationSource block;
    ASSERT_TRUE(block.setParam("binary", ParamValue::ofString("mpv")).has_value());

    MultiSignal stereo;
    stereo.push_back(Signal(128, 0.5f));
    stereo.push_back(Signal(128, 0.5f));
    const MultiSignal first = through(block, stereoLayout(), stereo);
    EXPECT_NEAR(first[0].front(), 0.5f, 1e-5f);
    EXPECT_EQ(block.inputLayout().size(), 2U);

    MultiSignal surround = constantChannels(6, 128, 0.0f);
    surround[2].assign(128, 0.5f); // the centre alone
    const MultiSignal second = through(block, surround51Layout(), surround);
    EXPECT_NEAR(second[0].front(), kFoldGain * 0.5f, 1e-5f);
    EXPECT_EQ(block.inputLayout().size(), 6U);
    // The match rule is not something re-preparing touches.
    EXPECT_EQ(block.param("binary").text(), "mpv");
}

TEST(ApplicationSourceTest, ItSoundsTheSameWhateverTheQuantum)
{
    MultiSignal input;
    input.push_back(sine(4096, 997.0, kTestSampleRate, 0.5f));
    input.push_back(sine(4096, 1500.0, kTestSampleRate, 0.3f));

    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] { return std::unique_ptr<Block>(std::make_unique<ApplicationSource>()); }, input, options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(ApplicationSourceTest, ABlockNobodyPreparedIsSilentRatherThanStale)
{
    // Nothing should run an unprepared block, but a buffer that is not
    // written carries whatever the last user of the memory left there, and
    // that is a noise nobody can explain.
    ApplicationSource block;
    AudioBuffer input(2, 64);
    AudioBuffer output(2, 64);
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        std::ranges::fill(output.channel(channel), 0.75f);
    }
    ProcessContext context{input.block(64), output.block(64), 64};
    block.process(context);

    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        EXPECT_FLOAT_EQ(output.channel(channel)[0], 0.0f) << "channel " << channel;
    }
}

// --- the microphone, the split, and the options that are only stored ---

namespace
{

/// Runs a block that is given two groups of channels on one set of ports:
/// the application's first, then the microphone's.
[[nodiscard]] MultiSignal throughBoth(ApplicationSource &block,
                                      const ChannelLayout &application,
                                      const ChannelLayout &microphone,
                                      const MultiSignal &input)
{
    ChannelLayout both = application;
    both.insert(both.end(), microphone.begin(), microphone.end());

    BlockRunner runner(block, static_cast<std::uint32_t>(both.size()), 2);
    runner.setInputLayout(both);
    runner.setSecondInputFrom(static_cast<std::uint32_t>(application.size()));
    EXPECT_TRUE(runner.prepare().has_value());
    return runner.run(input);
}

} // namespace

TEST(ApplicationSourceTest, TheMicrophonesPortsAreNamedApartFromTheApplications)
{
    // A port is named after its channel, and both groups have a front left.
    // The name is the only thing that keeps the engine from linking the
    // microphone into the port the application arrives at.
    ApplicationSource block;
    BlockRunner runner(block, 4, 2);
    const ChannelLayout stereo = stereoLayout();
    ChannelLayout both = stereo;
    both.insert(both.end(), stereo.begin(), stereo.end());
    runner.setInputLayout(both);
    runner.setSecondInputFrom(2);
    ASSERT_TRUE(runner.prepare().has_value());

    ASSERT_EQ(block.info().inputs.labels.size(), 4U);
    EXPECT_EQ(block.info().inputs.label(0), "in_FL");
    EXPECT_EQ(block.info().inputs.label(1), "in_FR");
    EXPECT_EQ(block.info().inputs.label(2), "in_mic_FL");
    EXPECT_EQ(block.info().inputs.label(3), "in_mic_FR");
    EXPECT_EQ(block.microphoneLayout().size(), 2U);
    EXPECT_EQ(block.inputLayout().size(), 4U);
}

TEST(ApplicationSourceTest, WithoutTheSplitBothSidesOfTheCallAreOnTheBus)
{
    // Include the microphone and say nothing else: one conversation on the
    // stereo bus, not two tracks.
    ApplicationSource block;
    ASSERT_TRUE(block.setParam("includeInput", ParamValue::ofBool(true)).has_value());

    MultiSignal input;
    input.push_back(Signal(128, 0.25f)); // the application, left
    input.push_back(Signal(128, 0.25f)); // the application, right
    input.push_back(Signal(128, 0.5f)); // the microphone, left
    input.push_back(Signal(128, 0.5f)); // the microphone, right

    const MultiSignal out = throughBoth(block, stereoLayout(), stereoLayout(), input);
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        EXPECT_NEAR(out[channel].back(), 0.75f, 1e-5f) << "channel " << channel;
    }
}

TEST(ApplicationSourceTest, TheSplitPutsTheMicrophoneOnTheLeftAndTheApplicationOnTheRight)
{
    ApplicationSource block;
    ASSERT_TRUE(block.setParam("includeInput", ParamValue::ofBool(true)).has_value());
    ASSERT_TRUE(block.setParam("splitChannels", ParamValue::ofBool(true)).has_value());

    // Each side is a stereo pair, and each is folded to mono **here**: a
    // pair going to one channel is half of each, so a pair at 0.4 arrives
    // at 0.4 and not at 0.8.
    MultiSignal input;
    input.push_back(Signal(128, 0.4f));
    input.push_back(Signal(128, 0.4f));
    input.push_back(Signal(128, 0.6f));
    input.push_back(Signal(128, 0.6f));

    const MultiSignal out = throughBoth(block, stereoLayout(), stereoLayout(), input);
    EXPECT_NEAR(out[0].back(), 0.6f, 1e-5f) << "the microphone should be on the left";
    EXPECT_NEAR(out[1].back(), 0.4f, 1e-5f) << "the application should be on the right";
}

TEST(ApplicationSourceTest, TheSplitFoldsAWiderSideByTheSameCoefficientsAsAnythingElse)
{
    // A 5.1 application and a mono microphone: each side goes to the stereo
    // bus by the channel policy and then to one channel, so a centre at 0.5
    // arrives at the same 0.707 it would anywhere else, halved again for
    // the pair.
    ApplicationSource block;
    ASSERT_TRUE(block.setParam("includeInput", ParamValue::ofBool(true)).has_value());
    ASSERT_TRUE(block.setParam("splitChannels", ParamValue::ofBool(true)).has_value());

    MultiSignal input;
    for (std::size_t channel = 0; channel < 6; ++channel) {
        input.push_back(Signal(128, channel == 2 ? 0.5f : 0.0f)); // the centre alone
    }
    input.push_back(Signal(128, 0.8f)); // the microphone, mono

    ChannelLayout mono;
    mono.push_back(ChannelPosition::Mono);
    const MultiSignal out = throughBoth(block, surround51Layout(), mono, input);

    // A mono microphone is on both sides of the bus at its own level, and
    // half of each makes it whole again.
    EXPECT_NEAR(out[0].back(), 0.8f, 1e-5f);
    EXPECT_NEAR(out[1].back(), kFoldGain * 0.5f, 1e-5f);
}

TEST(ApplicationSourceTest, WithTheMicrophoneOffNothingChanges)
{
    // The option is off and the engine gives one group: the block is what
    // it was before the microphone option ever existed.
    ApplicationSource block;
    MultiSignal input;
    input.push_back(Signal(128, 0.25f));
    input.push_back(Signal(128, 0.5f));

    const MultiSignal out = through(block, stereoLayout(), input);
    EXPECT_NEAR(out[0].back(), 0.25f, 1e-6f);
    EXPECT_NEAR(out[1].back(), 0.5f, 1e-6f);
    EXPECT_TRUE(block.microphoneLayout().empty());
    EXPECT_EQ(block.info().inputs.labels.size(), 2U);
}

TEST(ApplicationSourceTest, TheSplitWithNoMicrophoneIsSilentOnTheLeft)
{
    // Honest about it: there is nothing on that side, so it says nothing
    // rather than putting the application on both.
    ApplicationSource block;
    ASSERT_TRUE(block.setParam("includeInput", ParamValue::ofBool(true)).has_value());
    ASSERT_TRUE(block.setParam("splitChannels", ParamValue::ofBool(true)).has_value());

    MultiSignal input;
    input.push_back(Signal(128, 0.4f));
    input.push_back(Signal(128, 0.4f));

    const MultiSignal out = through(block, stereoLayout(), input);
    EXPECT_NEAR(out[0].back(), 0.0f, 1e-6f);
    EXPECT_NEAR(out[1].back(), 0.4f, 1e-6f);
}

TEST(ApplicationSourceTest, ItSoundsTheSameWhateverTheQuantumWithBothSides)
{
    MultiSignal input;
    input.push_back(sine(4096, 997.0, kTestSampleRate, 0.5f));
    input.push_back(sine(4096, 1500.0, kTestSampleRate, 0.3f));
    input.push_back(sine(4096, 311.0, kTestSampleRate, 0.4f));
    input.push_back(sine(4096, 523.0, kTestSampleRate, 0.2f));

    const ChannelLayout stereo = stereoLayout();
    ChannelLayout both = stereo;
    both.insert(both.end(), stereo.begin(), stereo.end());

    InvarianceOptions options;
    options.inputChannels = 4;
    options.outputChannels = 2;
    options.inputLayout = both;
    options.secondInputFrom = 2;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] {
            auto block = std::make_unique<ApplicationSource>();
            EXPECT_TRUE(block->setParam("includeInput", ParamValue::ofBool(true)).has_value());
            EXPECT_TRUE(block->setParam("splitChannels", ParamValue::ofBool(true)).has_value());
            return std::unique_ptr<Block>(std::move(block));
        },
        input,
        options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(ApplicationSourceTest, TheNewOptionsSurviveSaveAndRestore)
{
    ApplicationSource block;
    ASSERT_TRUE(block.setParam("includeInput", ParamValue::ofBool(true)).has_value());
    ASSERT_TRUE(block.setParam("splitChannels", ParamValue::ofBool(true)).has_value());
    ASSERT_TRUE(block.setParam("openUrlOnStart", ParamValue::ofBool(true)).has_value());
    ASSERT_TRUE(block.setParam("url", ParamValue::ofString("https://example.invalid/call")).has_value());
    ASSERT_TRUE(block.setParam("fillGapsWithSilence", ParamValue::ofBool(false)).has_value());

    ApplicationSource opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    EXPECT_TRUE(*opened.param("includeInput").asBool());
    EXPECT_TRUE(*opened.param("splitChannels").asBool());
    EXPECT_TRUE(*opened.param("openUrlOnStart").asBool());
    EXPECT_EQ(opened.param("url").text(), "https://example.invalid/call");
    EXPECT_FALSE(*opened.param("fillGapsWithSilence").asBool());

    // The defaults: nothing is captured or opened that nobody asked for,
    // and a gap in a recording is written out as silence so that what was
    // recorded still lines up with everything beside it.
    const ApplicationSource fresh;
    EXPECT_FALSE(*fresh.param("includeInput").asBool());
    EXPECT_FALSE(*fresh.param("splitChannels").asBool());
    EXPECT_FALSE(*fresh.param("openUrlOnStart").asBool());
    EXPECT_TRUE(fresh.param("url").text().empty());
    EXPECT_TRUE(*fresh.param("fillGapsWithSilence").asBool());
}
