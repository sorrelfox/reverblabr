// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Broadcast block: the audio thread's half of a stream. It passes the
// sound on untouched and copies it into its tap, pauses and all; the
// encoding and the server are the stream's (tests/unit/stream).

#include "allocationtrap.h"
#include "blocks/broadcast.h"
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

} // namespace

TEST(BroadcastTest, IsAnOutputThatStreamsAndDelaysNothing)
{
    const Broadcast block;
    EXPECT_EQ(block.info().typeId, "output.broadcast");
    EXPECT_EQ(block.info().category, BlockCategory::Outputs);
    EXPECT_TRUE(block.info().streamsAudio);
    // **Never taken for a recording**: its tap goes to a stream, not a file.
    EXPECT_FALSE(block.info().recordsAudio);
    EXPECT_FALSE(block.info().inputsFromAudioSystem);
    EXPECT_FALSE(block.info().outputsToAudioSystem);
    EXPECT_EQ(block.info().latencySamples, 0U);
}

TEST(BroadcastTest, TheSoundGoesOnAndIntoTheTapSampleForSample)
{
    Broadcast block;
    rt::AudioTap tap(2, 96000);
    BlockRunner runner(block, 2, 2);
    runner.setRecording(&tap);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal input = twoTones(8192);
    const MultiSignal out = runner.run(input, CycleShape::random(1, 512, 5));
    EXPECT_EQ(out[0], input[0]);
    EXPECT_EQ(out[1], input[1]);

    rt::TapMarker marker;
    ASSERT_TRUE(tap.popMarker(marker));
    EXPECT_EQ(marker.atFrame, 0U);
    EXPECT_EQ(marker.value, static_cast<std::uint32_t>(kTestSampleRate));
    const MultiSignal streamed = drain(tap);
    EXPECT_EQ(streamed[0], input[0]);
    EXPECT_EQ(streamed[1], input[1]);
}

TEST(BroadcastTest, NobodyStreamingIsNotAProblem)
{
    Broadcast block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal input = twoTones(1024);
    EXPECT_EQ(runner.run(input)[0], input[0]);
}

TEST(BroadcastTest, ItSoundsTheSameWhateverTheQuantum)
{
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] { return std::unique_ptr<Block>(std::make_unique<Broadcast>()); }, twoTones(8192), options);
    EXPECT_TRUE(report.passed) << report.summary;
}

// **The audio thread's half takes nothing from the allocator**: the copy
// into the tap is the whole of it, whatever the cycle.
TEST(BroadcastTest, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    Broadcast block;
    rt::AudioTap tap(2, 96000);
    BlockRunner runner(block, 2, 2);
    runner.setRecording(&tap);
    ASSERT_TRUE(runner.prepare().has_value());
    std::size_t allocations = 1;
    const MultiSignal out =
        runner.runWatchingForAllocations(twoTones(9600), CycleShape::random(1, 512, 4), allocations);
    EXPECT_EQ(allocations, 0U);
    EXPECT_EQ(out[0].size(), 9600U);
    EXPECT_EQ(tap.readableFrames(), 9600U);
}

TEST(BroadcastTest, AFullTapCostsTheStreamAndNeverTheSound)
{
    Broadcast block;
    rt::AudioTap tap(2, 1000); // the network is not keeping up
    BlockRunner runner(block, 2, 2);
    runner.setRecording(&tap);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal input = twoTones(4096);
    const MultiSignal out = runner.run(input, CycleShape::fixed(256));
    EXPECT_EQ(out[0], input[0]) << "a full tap changed what went on";
    EXPECT_EQ(tap.readableFrames(), 768U);
    EXPECT_EQ(tap.droppedFrames(), 4096U - 768U);
}

// **A pause in a source is silence on the air**, not a gap: unlike the
// Recorder, the block streams every cycle whatever its tap's gap lanes say.
TEST(BroadcastTest, EveryCycleIsStreamedWhoeverIsThere)
{
    Broadcast block;
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
    for (const std::uint32_t present : {0b0010U, 0b0101U, 0U}) {
        ProcessContext context{AudioBlock{inputs.data(), 2, kFrames}, AudioBlock{outputs.data(), 2, kFrames}, kFrames};
        context.sourcePresent = present;
        block.process(context);
    }
    EXPECT_EQ(tap.readableFrames(), 3U * kFrames);
}

TEST(BroadcastTest, WhatWasChosenIsWhatComesBackAndNoneOfItIsAPassword)
{
    Broadcast block;
    // A local Icecast's defaults, as a high-quality MP3, titled "artist -
    // title", listed nowhere.
    EXPECT_EQ(block.param("server").text(), "icecast");
    EXPECT_EQ(block.param("host").text(), "localhost");
    EXPECT_EQ(*block.param("port").asNumber(), 8000.0);
    EXPECT_FALSE(*block.param("secure").asBool());
    EXPECT_EQ(block.param("mount").text(), "/stream");
    EXPECT_EQ(block.param("user").text(), "source");
    EXPECT_EQ(block.param("format").text(), "mp3");
    EXPECT_EQ(*block.param("bitrate").asNumber(), 128.0);
    EXPECT_EQ(block.param("channels").text(), "stereo");
    EXPECT_FALSE(*block.param("listed").asBool());
    EXPECT_EQ(block.param("titleTemplate").text(), "%artist - %title");
    EXPECT_EQ(block.param("nowPlaying").text(), "none");

    ASSERT_TRUE(block.setParam("server", ParamValue::ofEnum("shoutcast")).has_value());
    ASSERT_TRUE(block.setParam("host", ParamValue::ofString("radio.example.org")).has_value());
    ASSERT_TRUE(block.setParam("port", ParamValue::ofInt(8443)).has_value());
    ASSERT_TRUE(block.setParam("secure", ParamValue::ofBool(true)).has_value());
    ASSERT_TRUE(block.setParam("format", ParamValue::ofEnum("opus")).has_value());
    ASSERT_TRUE(block.setParam("stationName", ParamValue::ofString("Night Shift")).has_value());
    ASSERT_TRUE(block.setParam("nowPlaying", ParamValue::ofEnum("file")).has_value());
    ASSERT_TRUE(block.setParam("nowPlayingFile", ParamValue::ofString("~/now.txt")).has_value());
    EXPECT_EQ(block.param("nowPlayingPlayer").text(), "") << "a player is the session's own until one is named";
    ASSERT_TRUE(block.setParam("nowPlaying", ParamValue::ofEnum("player")).has_value());
    ASSERT_TRUE(block.setParam("nowPlayingPlayer", ParamValue::ofString("vlc")).has_value());
    EXPECT_FALSE(block.setParam("port", ParamValue::ofInt(70000)).has_value());
    EXPECT_FALSE(block.setParam("format", ParamValue::ofEnum("wav")).has_value());
    EXPECT_FALSE(block.setParam("password", ParamValue::ofString("hackme")).has_value())
        << "a password is the secret store's, never a setting";

    Broadcast opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    EXPECT_EQ(opened.param("server").text(), "shoutcast");
    EXPECT_EQ(opened.param("host").text(), "radio.example.org");
    EXPECT_EQ(*opened.param("port").asNumber(), 8443.0);
    EXPECT_TRUE(*opened.param("secure").asBool());
    EXPECT_EQ(opened.param("format").text(), "opus");
    EXPECT_EQ(opened.param("stationName").text(), "Night Shift");
    EXPECT_EQ(opened.param("nowPlayingFile").text(), "~/now.txt");
    EXPECT_EQ(opened.param("nowPlaying").text(), "player");
    EXPECT_EQ(opened.param("nowPlayingPlayer").text(), "vlc");
    for (const auto &[id, value] : block.saveState()) {
        EXPECT_EQ(id.find("password"), std::string::npos) << id;
    }
    for (const ParamDescriptor &descriptor : block.params()) {
        EXPECT_TRUE(descriptor.flags.automatable) << descriptor.id;
    }
    EXPECT_EQ(block.params()[Broadcast::NowPlayingFile].id, "nowPlayingFile");
    EXPECT_EQ(block.params()[Broadcast::NowPlayingPlayer].id, "nowPlayingPlayer");
    EXPECT_EQ(block.params()[Broadcast::Server].id, "server");
}
