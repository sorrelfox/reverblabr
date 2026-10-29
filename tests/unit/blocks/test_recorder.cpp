// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Recorder block: the audio thread's half of a recording. It
// passes the sound on untouched and copies it into its tap, with the rate
// it is at; the file is the writer's (tests/unit/record).

#include "allocationtrap.h"
#include "blocks/recorder.h"
#include "blocksizeinvariance.h"
#include "offlinerunner.h"
#include "rt/audiotap.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <vector>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

[[nodiscard]] MultiSignal twoTones(std::size_t frames)
{
    MultiSignal signal;
    signal.push_back(sine(frames, 997.0, kTestSampleRate, 0.4f));
    signal.push_back(sine(frames, 1500.0, kTestSampleRate, 0.3f));
    return signal;
}

/// Everything in \a tap, as one signal per channel.
[[nodiscard]] MultiSignal drain(rt::AudioTap &tap)
{
    MultiSignal out(tap.channelCount());
    std::vector<float> buffer(1024 * tap.channelCount());
    for (;;) {
        const std::size_t got = tap.read(buffer, 1024);
        if (got == 0) {
            return out;
        }
        for (std::size_t frame = 0; frame < got; ++frame) {
            for (std::uint32_t channel = 0; channel < tap.channelCount(); ++channel) {
                out[channel].push_back(buffer[frame * tap.channelCount() + channel]);
            }
        }
    }
}

/// What the tap holds after the block has heard \a input in cycles of
/// \a shape.
[[nodiscard]] MultiSignal recorded(const MultiSignal &input, const CycleShape &shape)
{
    Recorder block;
    rt::AudioTap tap(2, 96000);
    BlockRunner runner(block, 2, 2);
    runner.setRecording(&tap);
    EXPECT_TRUE(runner.prepare().has_value());
    (void)runner.run(input, shape);
    return drain(tap);
}

} // namespace

TEST(RecorderTest, IsAnOutputThatRecordsAndDelaysNothing)
{
    const Recorder block;
    EXPECT_EQ(block.info().typeId, "output.recorder");
    EXPECT_EQ(block.info().category, BlockCategory::Outputs);
    EXPECT_TRUE(block.info().recordsAudio);
    // Not the audio system's: what it writes goes to a file, and what it
    // passes on goes to the next block.
    EXPECT_FALSE(block.info().inputsFromAudioSystem);
    EXPECT_FALSE(block.info().outputsToAudioSystem);
    EXPECT_EQ(block.info().latencySamples, 0U);
}

TEST(RecorderTest, TheSoundGoesOnSampleForSample)
{
    Recorder block;
    rt::AudioTap tap(2, 96000);
    BlockRunner runner(block, 2, 2);
    runner.setRecording(&tap);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal input = twoTones(8192);
    const MultiSignal out = runner.run(input, CycleShape::random(1, 512, 5));
    EXPECT_EQ(out[0], input[0]);
    EXPECT_EQ(out[1], input[1]);
}

TEST(RecorderTest, WhatItHearsIsInTheTapAfterItsRate)
{
    Recorder block;
    rt::AudioTap tap(2, 96000);
    BlockRunner runner(block, 2, 2);
    runner.setRecording(&tap);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal input = twoTones(4800);
    (void)runner.run(input);

    // The rate first, at the first frame, so the writer knows what the file
    // is before it opens one.
    rt::TapMarker marker;
    ASSERT_TRUE(tap.popMarker(marker));
    EXPECT_EQ(marker.atFrame, 0U);
    EXPECT_EQ(marker.value, static_cast<std::uint32_t>(kTestSampleRate));
    EXPECT_FALSE(tap.popMarker(marker)) << "the rate was said more than once";

    const MultiSignal inTap = drain(tap);
    EXPECT_EQ(inTap[0], input[0]);
    EXPECT_EQ(inTap[1], input[1]);
}

TEST(RecorderTest, NobodyRecordingIsNotAProblem)
{
    Recorder block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal input = twoTones(1024);
    EXPECT_EQ(runner.run(input)[0], input[0]);
}

TEST(RecorderTest, ItSoundsTheSameWhateverTheQuantum)
{
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] { return std::unique_ptr<Block>(std::make_unique<Recorder>()); }, twoTones(8192), options);
    EXPECT_TRUE(report.passed) << report.summary;
}

// And the file gets the same thing, which is what matters about a recorder:
// PipeWire's quantum is not the recording's business.
TEST(RecorderTest, ItRecordsTheSameWhateverTheQuantum)
{
    const MultiSignal input = twoTones(8192);
    const MultiSignal reference = recorded(input, CycleShape::fixed(256));
    ASSERT_EQ(reference[0], input[0]);
    for (const CycleShape &shape : {CycleShape::fixed(1), CycleShape::fixed(8192), CycleShape::random(1, 2048, 11)}) {
        const MultiSignal got = recorded(input, shape);
        EXPECT_EQ(got[0], reference[0]);
        EXPECT_EQ(got[1], reference[1]);
    }
}

TEST(RecorderTest, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    Recorder block;
    rt::AudioTap tap(2, 96000);
    BlockRunner runner(block, 2, 2);
    runner.setRecording(&tap);
    ASSERT_TRUE(runner.prepare().has_value());
    std::size_t allocations = 1;
    const MultiSignal out =
        runner.runWatchingForAllocations(twoTones(9600), CycleShape::random(1, 512, 4), allocations);
    EXPECT_EQ(allocations, 0U);
    EXPECT_EQ(out[0].size(), 9600U);
}

TEST(RecorderTest, AFullTapCostsTheRecordingAndNeverTheSound)
{
    Recorder block;
    rt::AudioTap tap(2, 1000); // nobody is emptying it
    BlockRunner runner(block, 2, 2);
    runner.setRecording(&tap);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal input = twoTones(4096);
    const MultiSignal out = runner.run(input, CycleShape::fixed(256));
    EXPECT_EQ(out[0], input[0]) << "a full tap changed what went on";
    // Three whole cycles fitted; the rest were dropped, whole, and counted.
    EXPECT_EQ(tap.readableFrames(), 768U);
    EXPECT_EQ(tap.droppedFrames(), 4096U - 768U);
}

TEST(RecorderTest, WhatWasChosenIsWhatComesBack)
{
    Recorder block;
    // A file by the session and the date by default, as a high-quality MP3
    // — 24 bits for when a lossless format is chosen —
    // in the default folder.
    EXPECT_EQ(block.param("format").text(), "mp3");
    EXPECT_EQ(block.param("sampleFormat").text(), "int24");
    EXPECT_EQ(block.param("folder").text(), "");
    EXPECT_EQ(block.param("fileName").text(), "%session %date %time");
    EXPECT_EQ(block.param("sampleRate").text(), "session");

    ASSERT_TRUE(block.setParam("format", ParamValue::ofEnum("flac")).has_value());
    ASSERT_TRUE(block.setParam("sampleFormat", ParamValue::ofEnum("int16")).has_value());
    ASSERT_TRUE(block.setParam("folder", ParamValue::ofString("~/Podcasts")).has_value());
    ASSERT_TRUE(block.setParam("fileName", ParamValue::ofString("%session %counter")).has_value());
    ASSERT_TRUE(block.setParam("sampleRate", ParamValue::ofEnum("44100")).has_value());
    ASSERT_TRUE(block.setParam("bitrate", ParamValue::ofInt(96)).has_value());
    ASSERT_TRUE(block.setParam("bitrateMode", ParamValue::ofEnum("cbr")).has_value());
    ASSERT_TRUE(block.setParam("channels", ParamValue::ofEnum("mono")).has_value());
    EXPECT_FALSE(block.setParam("bitrate", ParamValue::ofInt(1000)).has_value()) << "past what any encoder takes";
    EXPECT_FALSE(block.setParam("format", ParamValue::ofEnum("cassette")).has_value());

    Recorder opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    EXPECT_EQ(opened.param("format").text(), "flac");
    EXPECT_EQ(opened.param("sampleFormat").text(), "int16");
    EXPECT_EQ(opened.param("folder").text(), "~/Podcasts");
    EXPECT_EQ(opened.param("fileName").text(), "%session %counter");
    EXPECT_EQ(opened.param("sampleRate").text(), "44100");
    EXPECT_EQ(*opened.param("bitrate").asNumber(), 96.0);
    EXPECT_EQ(opened.param("bitrateMode").text(), "cbr");
    EXPECT_EQ(opened.param("channels").text(), "mono");
}

// **A pause is left out**: with its tap reading pauses from lanes 1
// and 3, a cycle in which neither had audio arriving is not recorded — and
// still passes through — while one in which either did is.
TEST(RecorderTest, ACycleWithNoneOfItsGapLanesPresentIsNotRecorded)
{
    Recorder block;
    rt::AudioTap tap(2, 96000);
    tap.setGapLanes(0b1010);
    PrepareSpec spec;
    spec.sampleRate = kTestSampleRate;
    spec.recording = &tap;
    ASSERT_TRUE(block.prepare(spec).has_value());

    constexpr std::uint32_t kFrames = 64;
    std::array<std::vector<float>, 2> in{std::vector<float>(kFrames, 0.25f), std::vector<float>(kFrames, -0.25f)};
    std::array<std::vector<float>, 2> out{std::vector<float>(kFrames), std::vector<float>(kFrames)};
    std::array<float *, 2> inputs{in[0].data(), in[1].data()};
    std::array<float *, 2> outputs{out[0].data(), out[1].data()};
    const auto cycle = [&](std::uint32_t present) {
        ProcessContext context{AudioBlock{inputs.data(), 2, kFrames}, AudioBlock{outputs.data(), 2, kFrames}, kFrames};
        context.sourcePresent = present;
        block.process(context);
        EXPECT_EQ(out[0][0], 0.25f) << "a pause in the recording changed the sound";
    };
    cycle(0b0010); // lane 1 there
    cycle(0b0101); // lanes 0 and 2, neither of its own: a pause
    cycle(0b1000); // lane 3 there
    cycle(0b0000); // nobody at all
    EXPECT_EQ(tap.readableFrames(), 2U * kFrames);

    // With no gap lanes, every cycle is recorded, whoever is there.
    tap.setGapLanes(0);
    cycle(0);
    EXPECT_EQ(tap.readableFrames(), 3U * kFrames);
}

// The File Limits settings: off out of the box, each with its number
// kept, and none of them a replan — they are the writer's to apply.
TEST(RecorderTest, TheFileLimitsAreOffAndCostNoReplan)
{
    const Recorder block;
    EXPECT_FALSE(*block.param("splitEnabled").asBool());
    EXPECT_EQ(*block.param("splitEvery").asNumber(), 30.0);
    EXPECT_EQ(block.param("splitUnit").text(), "minutes");
    EXPECT_FALSE(*block.param("endEnabled").asBool());
    EXPECT_EQ(*block.param("endAfter").asNumber(), 1.0);
    EXPECT_EQ(block.param("endUnit").text(), "hours");
    EXPECT_FALSE(*block.param("silenceEnabled").asBool());
    EXPECT_EQ(block.param("silenceAction").text(), "remove");
    EXPECT_FLOAT_EQ(static_cast<float>(*block.param("silenceSeconds").asNumber()), 2.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(*block.param("silenceThreshold").asNumber()), -60.0f);
    for (const ParamDescriptor &descriptor : block.params()) {
        EXPECT_TRUE(descriptor.flags.automatable) << descriptor.id;
    }
    // After the ones there were before, so a queued change's index still
    // means what it did.
    EXPECT_EQ(block.params()[Recorder::SampleRate].id, "sampleRate");
    EXPECT_EQ(block.params()[Recorder::SilenceThreshold].id, "silenceThreshold");
}

TEST(RecorderTest, TheTagsAreEmptyTextAndCostNoReplan)
{
    const Recorder block;
    for (const char *id : {"tagTitle",
                           "tagArtist",
                           "tagAlbum",
                           "tagGenre",
                           "tagYear",
                           "tagTrack",
                           "tagTrackCount",
                           "tagDisc",
                           "tagDiscCount",
                           "tagComment"}) {
        EXPECT_EQ(block.param(id).text(), "") << id;
    }
    EXPECT_EQ(block.params()[Recorder::TagTitle].id, "tagTitle");
    EXPECT_EQ(block.params()[Recorder::TagComment].id, "tagComment");
}
