// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Live Stream block: the audio thread's half of a video stream, which is
// Broadcast's half. It passes the sound on untouched and copies it into its
// tap; the picture, the encoders and the server are the stream's
// (tests/unit/stream).

#include "allocationtrap.h"
#include "blocks/broadcast.h"
#include "blocks/livestream.h"
#include "offlinerunner.h"
#include "rt/audiotap.h"
#include "signals.h"

#include <gtest/gtest.h>

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

} // namespace

TEST(LiveStreamBlockTest, IsAnOutputThatStreamsAndDelaysNothing)
{
    const LiveStream block;
    EXPECT_EQ(block.info().typeId, "output.livestream");
    EXPECT_EQ(block.info().category, BlockCategory::Outputs);
    EXPECT_TRUE(block.info().streamsAudio);
    EXPECT_FALSE(block.info().recordsAudio);
    EXPECT_EQ(block.info().latencySamples, 0U);
}

TEST(LiveStreamBlockTest, TheSoundGoesOnAndIntoTheTapSampleForSample)
{
    LiveStream block;
    rt::AudioTap tap(2, 96000);
    BlockRunner runner(block, 2, 2);
    runner.setRecording(&tap);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal input = twoTones(4096);
    const MultiSignal out = runner.run(input, CycleShape::random(1, 512, 5));
    EXPECT_EQ(out[0], input[0]);
    EXPECT_EQ(out[1], input[1]);
    rt::TapMarker marker;
    ASSERT_TRUE(tap.popMarker(marker));
    EXPECT_EQ(marker.value, static_cast<std::uint32_t>(kTestSampleRate));
    EXPECT_EQ(tap.readableFrames(), 4096U);
}

// **The audio thread's half takes nothing from the allocator**, whatever
// the cycle: everything a live stream costs is on its own thread.
TEST(LiveStreamBlockTest, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    LiveStream block;
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

TEST(LiveStreamBlockTest, NobodyStreamingIsNotAProblem)
{
    LiveStream block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal input = twoTones(1024);
    EXPECT_EQ(runner.run(input)[0], input[0]);
}

// **Its now-playing choices are Broadcast's**, by the same ids, so a source
// added to one is offered by both and followed alike.
TEST(LiveStreamBlockTest, TakesWhatIsPlayingFromWhereBroadcastDoes)
{
    const LiveStream live;
    const Broadcast broadcast;
    const auto choicesOf = [](const Block &block, std::string_view id) {
        for (const ParamDescriptor &descriptor : block.params()) {
            if (descriptor.id == id) {
                return descriptor.choices;
            }
        }
        return std::vector<std::string>{};
    };
    EXPECT_EQ(choicesOf(live, "nowPlaying"), choicesOf(broadcast, "nowPlaying"));
    EXPECT_EQ(choicesOf(live, "nowPlaying"), (std::vector<std::string>{"none", "file", "player"}));
    EXPECT_EQ(live.param("titleTemplate").text(), broadcast.param("titleTemplate").text());
    EXPECT_EQ(live.param("nowPlayingPlayer").text(), "");
}

TEST(LiveStreamBlockTest, WhatWasChosenIsWhatComesBackAndTheKeyIsNotOfIt)
{
    LiveStream block;
    EXPECT_EQ(block.param("url").text(), "") << "nothing goes anywhere until a service is named";
    EXPECT_EQ(block.param("quality").text(), "higher");
    EXPECT_EQ(block.param("encoder").text(), "auto");
    EXPECT_EQ(block.param("title").text(), "%session");
    EXPECT_EQ(block.param("visualisation").text(), "waveform");
    EXPECT_EQ(block.param("background").text(), "#16181d");

    ASSERT_TRUE(block.setParam("url", ParamValue::ofString("rtmps://live.example.com/app")).has_value());
    ASSERT_TRUE(block.setParam("quality", ParamValue::ofEnum("best")).has_value());
    ASSERT_TRUE(block.setParam("encoder", ParamValue::ofEnum("openh264")).has_value());
    ASSERT_TRUE(block.setParam("visualisation", ParamValue::ofEnum("spectrum")).has_value());
    ASSERT_TRUE(block.setParam("logo", ParamValue::ofString("~/logo.png")).has_value());
    EXPECT_FALSE(block.setParam("quality", ParamValue::ofEnum("4k")).has_value());
    EXPECT_FALSE(block.setParam("encoder", ParamValue::ofEnum("libfdk")).has_value());
    EXPECT_FALSE(block.setParam("key", ParamValue::ofString("live_123")).has_value())
        << "the stream key is never a setting";

    LiveStream copy;
    ASSERT_TRUE(copy.loadState(block.saveState()).has_value());
    EXPECT_EQ(copy.param("url").text(), "rtmps://live.example.com/app");
    EXPECT_EQ(copy.param("quality").text(), "best");
    EXPECT_EQ(copy.param("encoder").text(), "openh264");
    for (const auto &[id, value] : block.saveState()) {
        EXPECT_NE(id, "key");
    }
}
