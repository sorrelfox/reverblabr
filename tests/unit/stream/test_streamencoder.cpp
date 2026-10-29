// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The stream's encoder: what it makes is what a player reads, in each
// format, and an Ogg stream's new title is a stream chained onto the last.
// Read back through FFmpeg, from memory, as a listener's player would.

#include "stream/streamencoder.h"
#include "streamdecode.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <ostream>
#include <vector>

using namespace rb::stream;
using namespace rb::test;

namespace
{

/// \a seconds of a 440 Hz tone, interleaved, at \a rate.
[[nodiscard]] std::vector<float> tone(double seconds, std::uint32_t rate, std::uint32_t channels)
{
    const auto frames = static_cast<std::size_t>(seconds * rate);
    std::vector<float> samples(frames * channels);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto value =
            static_cast<float>(0.3 * std::sin(2.0 * std::numbers::pi * 440.0 * static_cast<double>(frame) / rate));
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            samples[frame * channels + channel] = value;
        }
    }
    return samples;
}

[[nodiscard]] bool available(StreamFormat format)
{
    return !streamFormatUnavailable(format).has_value();
}

struct Case {
    StreamFormat format;
    const char *codec;
    std::uint32_t rate;
};

// The case by its codec's name, in the test's name and in what ctest lists.
void PrintTo(const Case &wanted, std::ostream *out)
{
    *out << wanted.codec;
}

class StreamEncoderTest : public ::testing::TestWithParam<Case>
{
};

} // namespace

TEST(StreamEncoderRateTest, ASessionAt44Or48KhzStreamsAtItsOwnRateAndOpusAt48)
{
    EXPECT_EQ(streamRateFor(StreamFormat::Mp3, 44100), 44100U);
    EXPECT_EQ(streamRateFor(StreamFormat::Aac, 48000), 48000U);
    EXPECT_EQ(streamRateFor(StreamFormat::Vorbis, 96000), 48000U);
    EXPECT_EQ(streamRateFor(StreamFormat::Opus, 44100), 48000U);
}

TEST_P(StreamEncoderTest, WhatComesOutIsWhatAPlayerReads)
{
    const Case wanted = GetParam();
    if (!available(wanted.format)) {
        GTEST_SKIP() << "this FFmpeg has no " << wanted.codec << " encoder";
    }
    StreamEncoder::Spec spec;
    spec.format = wanted.format;
    spec.sampleRate = wanted.rate;
    spec.channels = 2;
    spec.bitrateKbps = 128;
    spec.track.title = "First";
    auto encoder = StreamEncoder::create(spec);
    ASSERT_TRUE(encoder.has_value());
    std::vector<char> bytes = (*encoder)->take();
    const std::size_t header = bytes.size();
    std::size_t halfway = 0;
    const std::vector<float> samples = tone(3.0, wanted.rate, 2);
    // In cycles of the size a tap gives, as the stream feeds it.
    for (std::size_t at = 0; at < samples.size(); at += 512 * 2) {
        const std::size_t count = std::min<std::size_t>(512 * 2, samples.size() - at);
        ASSERT_TRUE((*encoder)->encode(std::span<const float>(samples).subspan(at, count)).has_value());
        const std::vector<char> more = (*encoder)->take();
        bytes.insert(bytes.end(), more.begin(), more.end());
        if (halfway == 0 && at >= samples.size() / 2) {
            halfway = bytes.size();
        }
    }
    // **Bytes come out as the audio goes in**, not at the end: a stream
    // that held them back would be silent for its listeners.
    EXPECT_GT(halfway, header + 1000) << "nothing came out in the first half";
    EXPECT_GT(bytes.size(), halfway);

    const Decoded decoded = decode(bytes);
    ASSERT_TRUE(decoded.opened) << "FFmpeg could not read the stream";
    EXPECT_EQ(decoded.codec, wanted.codec);
    EXPECT_EQ(decoded.sampleRate, static_cast<int>(wanted.rate));
    EXPECT_EQ(decoded.channels, 2);
    // Most of the three seconds are there; the last frame is still in the
    // encoder.
    EXPECT_GT(decoded.seconds, 2.0);
    EXPECT_GT(decoded.peak, 0.2f) << "the tone did not survive";
}

INSTANTIATE_TEST_SUITE_P(Formats,
                         StreamEncoderTest,
                         ::testing::Values(Case{StreamFormat::Mp3, "mp3", 44100},
                                           Case{StreamFormat::Opus, "opus", 48000},
                                           Case{StreamFormat::Vorbis, "vorbis", 48000},
                                           Case{StreamFormat::Aac, "aac", 48000}),
                         [](const auto &info) { return std::string(info.param.codec); });

TEST(StreamEncoderChainTest, AnOggStreamsNewTitleIsANewChainedStreamWithItInItsHeader)
{
    for (const StreamFormat format : {StreamFormat::Opus, StreamFormat::Vorbis}) {
        if (!available(format)) {
            continue;
        }
        StreamEncoder::Spec spec;
        spec.format = format;
        spec.sampleRate = 48000;
        spec.track.title = "First";
        auto encoder = StreamEncoder::create(spec);
        ASSERT_TRUE(encoder.has_value());
        const std::vector<float> samples = tone(1.0, 48000, 2);
        ASSERT_TRUE((*encoder)->encode(samples).has_value());
        ASSERT_TRUE((*encoder)->restart(Track{.title = "Second"}).has_value());
        ASSERT_TRUE((*encoder)->encode(samples).has_value());
        const std::vector<char> bytes = (*encoder)->take();
        const std::string text(bytes.begin(), bytes.end());
        // Two streams, each starting with its own beginning-of-stream page,
        // and each comment header carrying its title.
        std::size_t pages = 0;
        for (std::size_t at = text.find("OggS"); at != std::string::npos; at = text.find("OggS", at + 4)) {
            // The header type byte: 2 is the first page of a stream.
            if (at + 5 < text.size() && (static_cast<unsigned char>(text[at + 5]) & 0x02) != 0) {
                ++pages;
            }
        }
        EXPECT_EQ(pages, 2U);
        EXPECT_NE(text.find("First"), std::string::npos);
        EXPECT_NE(text.find("Second"), std::string::npos);
        EXPECT_LT(text.find("First"), text.find("Second"));
    }
}

TEST(StreamEncoderChainTest, AnMp3StreamIsNotRestartedForATitle)
{
    if (!available(StreamFormat::Mp3)) {
        GTEST_SKIP() << "this FFmpeg has no MP3 encoder";
    }
    StreamEncoder::Spec spec;
    spec.format = StreamFormat::Mp3;
    spec.sampleRate = 48000;
    auto encoder = StreamEncoder::create(spec);
    ASSERT_TRUE(encoder.has_value());
    ASSERT_TRUE((*encoder)->encode(tone(0.5, 48000, 2)).has_value());
    (void)(*encoder)->take();
    ASSERT_TRUE((*encoder)->restart(Track{.title = "Second"}).has_value());
    EXPECT_TRUE((*encoder)->take().empty()) << "an MP3 stream has no header to send again";
    // And nothing is in front of the frames: no tag a server would pass on.
    ASSERT_TRUE((*encoder)->encode(tone(0.5, 48000, 2)).has_value());
    const std::vector<char> bytes = (*encoder)->take();
    ASSERT_GE(bytes.size(), 2U);
    EXPECT_EQ(static_cast<unsigned char>(bytes[0]), 0xFF);
    EXPECT_EQ(static_cast<unsigned char>(bytes[1]) & 0xE0, 0xE0);
}

TEST(StreamEncoderChainTest, MonoIsOneChannel)
{
    if (!available(StreamFormat::Mp3)) {
        GTEST_SKIP() << "this FFmpeg has no MP3 encoder";
    }
    StreamEncoder::Spec spec;
    spec.format = StreamFormat::Mp3;
    spec.sampleRate = 48000;
    spec.channels = 1;
    auto encoder = StreamEncoder::create(spec);
    ASSERT_TRUE(encoder.has_value());
    ASSERT_TRUE((*encoder)->encode(tone(2.0, 48000, 1)).has_value());
    const Decoded decoded = decode((*encoder)->take());
    ASSERT_TRUE(decoded.opened);
    EXPECT_EQ(decoded.channels, 1);
}

// **Every bit rate the block offers opens**, once brought into the
// format's range for its channels: a choice the panel shows must never be
// one the encoder refuses after the server has been asked.
TEST(StreamEncoderRateTest, EveryOfferedBitRateOpensOnceInItsRange)
{
    for (const StreamFormat format : {StreamFormat::Mp3, StreamFormat::Opus, StreamFormat::Vorbis, StreamFormat::Aac}) {
        if (!available(format)) {
            continue;
        }
        for (const std::uint32_t channels : {1U, 2U}) {
            for (const std::uint32_t kbps : {32U, 48U, 64U, 96U, 128U, 160U, 192U, 256U, 320U}) {
                StreamEncoder::Spec spec;
                spec.format = format;
                spec.channels = channels;
                spec.sampleRate = streamRateFor(format, 44100);
                spec.bitrateKbps = kbps;
                const auto made = StreamEncoder::create(spec);
                EXPECT_TRUE(made.has_value()) << static_cast<int>(format) << " " << channels << " ch " << kbps
                                              << " kbps, clamped to " << clampedBitrate(format, channels, kbps);
            }
        }
    }
    EXPECT_EQ(clampedBitrate(StreamFormat::Vorbis, 2, 32), 48U);
    EXPECT_EQ(clampedBitrate(StreamFormat::Vorbis, 1, 320), 192U);
    EXPECT_EQ(clampedBitrate(StreamFormat::Opus, 1, 320), 256U);
    EXPECT_EQ(clampedBitrate(StreamFormat::Mp3, 2, 320), 320U);
}
