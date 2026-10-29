// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The H.264 encoders: which are tried and in what order, that the probe
// finds the ones that work, that one that will not open gives way to the
// next, and what an encoder hands the muxer. The AAC encoder beside them.

#include "stream/canvas.h"
#include "stream/videoencoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace rb::stream;

namespace
{

[[nodiscard]] VideoEncoderProbe only(std::initializer_list<VideoEncoderKind> kinds)
{
    VideoEncoderProbe probe;
    for (const VideoEncoderKind kind : kinds) {
        switch (kind) {
        case VideoEncoderKind::Vaapi:
            probe.vaapi = true;
            break;
        case VideoEncoderKind::Nvenc:
            probe.nvenc = true;
            break;
        case VideoEncoderKind::X264:
            probe.x264 = true;
            break;
        case VideoEncoderKind::OpenH264:
            probe.openh264 = true;
            break;
        }
    }
    return probe;
}

using Kinds = std::vector<VideoEncoderKind>;

} // namespace

TEST(VideoEncoderTest, IdsAreStable)
{
    for (const VideoEncoderKind kind : allVideoEncoders()) {
        EXPECT_EQ(videoEncoderFromId(videoEncoderId(kind)), kind);
    }
    EXPECT_EQ(videoEncoderId(VideoEncoderKind::Vaapi), "vaapi");
    EXPECT_EQ(videoEncoderId(VideoEncoderKind::Nvenc), "nvenc");
    EXPECT_EQ(videoEncoderId(VideoEncoderKind::X264), "x264");
    EXPECT_EQ(videoEncoderId(VideoEncoderKind::OpenH264), "openh264");
    EXPECT_FALSE(videoEncoderFromId("auto").has_value());
}

// **The hardware first, then x264, then OpenH264**, and only what is here.
TEST(VideoEncoderTest, AnAutomaticChoiceTriesTheHardwareFirst)
{
    EXPECT_EQ(
        videoEncoderOrder(std::nullopt,
                          only({VideoEncoderKind::OpenH264,
                                VideoEncoderKind::X264,
                                VideoEncoderKind::Nvenc,
                                VideoEncoderKind::Vaapi})),
        (Kinds{VideoEncoderKind::Vaapi, VideoEncoderKind::Nvenc, VideoEncoderKind::X264, VideoEncoderKind::OpenH264}));
    EXPECT_EQ(videoEncoderOrder(std::nullopt, only({VideoEncoderKind::OpenH264, VideoEncoderKind::Vaapi})),
              (Kinds{VideoEncoderKind::Vaapi, VideoEncoderKind::OpenH264}));
    EXPECT_TRUE(videoEncoderOrder(std::nullopt, VideoEncoderProbe{}).empty());
}

// **What is asked for goes first**, with the rest behind it to fall back
// on; what is asked for and not here is passed over.
TEST(VideoEncoderTest, TheOneAskedForGoesFirst)
{
    const VideoEncoderProbe probe = only({VideoEncoderKind::Nvenc, VideoEncoderKind::X264, VideoEncoderKind::OpenH264});
    EXPECT_EQ(videoEncoderOrder(VideoEncoderKind::OpenH264, probe),
              (Kinds{VideoEncoderKind::OpenH264, VideoEncoderKind::Nvenc, VideoEncoderKind::X264}));
    EXPECT_EQ(videoEncoderOrder(VideoEncoderKind::Vaapi, probe),
              (Kinds{VideoEncoderKind::Nvenc, VideoEncoderKind::X264, VideoEncoderKind::OpenH264}));
}

// **The probe's word holds**: what it found opens, and what it did not
// does not.
TEST(VideoEncoderTest, TheProbeFindsWhatOpens)
{
    const VideoEncoderProbe &probe = probeVideoEncoders();
    for (const VideoEncoderKind kind : {VideoEncoderKind::Nvenc, VideoEncoderKind::X264, VideoEncoderKind::OpenH264}) {
        VideoEncoder::Spec spec;
        spec.kind = kind;
        spec.width = 320;
        spec.height = 192;
        EXPECT_EQ(VideoEncoder::create(spec).has_value(), probe.has(kind)) << videoEncoderId(kind);
    }
    EXPECT_EQ(probe.vaapi, !probe.vaapiDevice.empty());
    // Asked twice, the same answer, found once.
    EXPECT_EQ(&probeVideoEncoders(), &probe);
}

// **One that will not open gives way to the next**: a computer whose card
// has no H.264 still streams, in software.
TEST(VideoEncoderTest, OneThatWillNotOpenGivesWayToTheNext)
{
    const VideoEncoderProbe &probe = probeVideoEncoders();
    const Kinds software = videoEncoderOrder(std::nullopt, only({VideoEncoderKind::X264, VideoEncoderKind::OpenH264}));
    Kinds order;
    for (const VideoEncoderKind kind : allVideoEncoders()) {
        if (!probe.has(kind)) {
            order.push_back(kind);
        }
    }
    const auto working = std::ranges::find_if(software, [&probe](VideoEncoderKind kind) { return probe.has(kind); });
    if (order.empty() || working == software.end()) {
        GTEST_SKIP() << "needs one encoder that works here and one that does not";
    }
    order.push_back(*working);
    VideoEncoder::Spec spec;
    spec.width = 320;
    spec.height = 192;
    const auto made = VideoEncoder::createFirst(order, spec);
    ASSERT_TRUE(made.has_value());
    EXPECT_EQ((*made)->spec().kind, *working);

    EXPECT_EQ(VideoEncoder::createFirst({}, spec).error_or(StreamError::Closed), StreamError::EncoderMissing);
}

// **Headers apart, frames as packets**, a key frame first and every two
// seconds, as FLV and the services want.
TEST(VideoEncoderTest, HandsTheMuxerHeadersAndFrames)
{
    const VideoEncoderProbe &probe = probeVideoEncoders();
    if (!probe.any()) {
        GTEST_SKIP() << "this system's FFmpeg has no H.264 encoder that works";
    }
    for (const VideoEncoderKind kind : videoEncoderOrder(std::nullopt, probe)) {
        VideoEncoder::Spec spec;
        spec.kind = kind;
        spec.width = 321; // made even
        spec.height = 180;
        spec.fps = 15;
        spec.bitrateKbps = 300;
        auto made = VideoEncoder::create(spec);
        ASSERT_TRUE(made.has_value()) << videoEncoderId(kind);
        VideoEncoder &encoder = **made;
        EXPECT_EQ(encoder.spec().width, 320);
        ASSERT_NE(encoder.context(), nullptr);
        EXPECT_GT(encoder.context()->extradata_size, 0) << videoEncoderId(kind);
        Canvas picture(320, 180);
        std::vector<std::pair<std::int64_t, bool>> packets;
        for (int index = 0; index < 45; ++index) {
            picture.fill(Rgb{static_cast<std::uint8_t>(index * 5), 40, 80});
            ASSERT_TRUE(encoder
                            .encode(picture,
                                    index,
                                    [&packets](AVPacket *packet) {
                                        packets.emplace_back(packet->pts, (packet->flags & AV_PKT_FLAG_KEY) != 0);
                                        return true;
                                    })
                            .has_value());
        }
        ASSERT_GE(packets.size(), 40U) << videoEncoderId(kind);
        EXPECT_TRUE(packets.front().second);
        EXPECT_EQ(packets.front().first, 0);
        // Frame 30 is two seconds in: a key frame.
        EXPECT_TRUE(std::ranges::any_of(packets, [](const auto &packet) {
            return packet.first == 30 && packet.second;
        })) << videoEncoderId(kind);
        // A picture of another size is not this encoder's.
        EXPECT_FALSE(encoder.encode(Canvas(64, 64), 46, [](AVPacket *) { return true; }).has_value());
    }
}

TEST(AacEncoderTest, MakesWholeFramesAndKeepsTheRest)
{
    auto made = AacEncoder::create(48000, 2, 128);
    ASSERT_TRUE(made.has_value());
    AacEncoder &encoder = **made;
    EXPECT_EQ(encoder.sampleRate(), 48000U);
    EXPECT_EQ(encoder.channels(), 2U);
    EXPECT_GT(encoder.context()->extradata_size, 0);
    std::vector<float> second(48000 * 2);
    for (std::size_t frame = 0; frame < 48000; ++frame) {
        second[frame * 2] = second[frame * 2 + 1] =
            static_cast<float>(0.3 * std::sin(2.0 * std::numbers::pi * 440.0 * frame / 48000.0));
    }
    int packets = 0;
    ASSERT_TRUE(encoder.encode(second, [&packets](AVPacket *) { return ++packets, true; }).has_value());
    // 46 whole frames of 1024 in a second, less the encoder's own delay.
    EXPECT_GE(packets, 44);
    EXPECT_LE(packets, 46);
    // A sink that will not take one is a failure.
    EXPECT_FALSE(encoder.encode(second, [](AVPacket *) { return false; }).has_value());
}

// **The software encoders are known without a graphics card**, and the
// whole probe is said to be finished only once it is made.
TEST(VideoEncoderTest, TheSoftwareProbeTouchesNoGraphicsCard)
{
    const VideoEncoderProbe &software = probeSoftwareVideoEncoders();
    EXPECT_FALSE(software.vaapi);
    EXPECT_FALSE(software.nvenc);
    EXPECT_TRUE(software.vaapiDevice.empty());
    const VideoEncoderProbe &whole = probeVideoEncoders();
    EXPECT_EQ(whole.x264, software.x264);
    EXPECT_EQ(whole.openh264, software.openh264);
    const std::optional<VideoEncoderProbe> finished = finishedVideoEncoderProbe();
    ASSERT_TRUE(finished.has_value());
    EXPECT_EQ(finished->vaapi, whole.vaapi);
    EXPECT_EQ(finished->nvenc, whole.nvenc);
}
