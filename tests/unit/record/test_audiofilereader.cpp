// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A recording read back frame by frame: what a preview plays, from
// where it is asked to start.

#include "record/audiofile.h"
#include "record/audiofilereader.h"
#include "record/m4afile.h"
#include "recordfixtures.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numbers>

using namespace rb;
using namespace rb::record;
using namespace rb::recordtest;

namespace
{

/// A ramp a frame at a time, the same on both channels, so a sample says
/// which frame it is.
void writeRamp(const std::filesystem::path &path, std::size_t frames, std::uint32_t rate)
{
    FileSpec spec;
    spec.sampleFormat = SampleFormat::Float32;
    spec.sampleRate = rate;
    spec.dither = false;
    auto writer = AudioFileWriter::create(path, spec);
    ASSERT_TRUE(writer.has_value());
    std::vector<float> samples(frames * 2);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        samples[frame * 2] = static_cast<float>(frame) / static_cast<float>(frames);
        samples[frame * 2 + 1] = -samples[frame * 2];
    }
    ASSERT_TRUE(writer->write(samples).has_value());
    ASSERT_TRUE(writer->close().has_value());
}

} // namespace

TEST(AudioFileReaderTest, ItReadsTheFileBackFrameForFrame)
{
    const ScratchFolder folder;
    const std::filesystem::path path = folder / "ramp.wav";
    writeRamp(path, 10000, 44100);

    std::optional<AudioFileReader> reader = AudioFileReader::open(path);
    ASSERT_TRUE(reader.has_value());
    EXPECT_EQ(reader->sampleRate(), 44100U);
    EXPECT_EQ(reader->channels(), 2U);
    EXPECT_EQ(reader->frames(), 10000U);

    std::vector<float> out(4096 * 2);
    std::size_t total = 0;
    for (;;) {
        const std::size_t got = reader->read(out, 4096);
        if (got == 0) {
            break;
        }
        for (std::size_t frame = 0; frame < got; ++frame) {
            ASSERT_FLOAT_EQ(out[frame * 2], static_cast<float>(total + frame) / 10000.0f);
            ASSERT_FLOAT_EQ(out[frame * 2 + 1], -out[frame * 2]);
        }
        total += got;
    }
    EXPECT_EQ(total, 10000U);
}

TEST(AudioFileReaderTest, ASeekStartsTheNextReadAtThatFrame)
{
    const ScratchFolder folder;
    const std::filesystem::path path = folder / "ramp.flac";
    writeRamp(path, 48000, 48000);
    std::optional<AudioFileReader> reader = AudioFileReader::open(path);
    ASSERT_TRUE(reader.has_value());

    EXPECT_EQ(reader->seek(24000), 24000U);
    std::vector<float> out(2);
    ASSERT_EQ(reader->read(out, 1), 1U);
    EXPECT_NEAR(out[0], 0.5f, 1e-4f) << "FLAC stores 24 bits";
    EXPECT_EQ(reader->seek(999999), 48000U) << "past the end is the end";
    EXPECT_EQ(reader->read(out, 1), 0U);
}

TEST(AudioFileReaderTest, WhatIsNotAudioDoesNotOpen)
{
    const ScratchFolder folder;
    std::ofstream(folder / "notes.wav") << "not a sound file";
    EXPECT_FALSE(AudioFileReader::open(folder / "notes.wav").has_value());
    EXPECT_FALSE(AudioFileReader::open(folder / "missing.wav").has_value());
}

namespace
{

/// A two-second stereo M4A at 48 kHz: a silent second, then a tone whose
/// level swells and falls, so no two stretches of it look alike and a
/// read that is out by some frames cannot pass for one that is not.
[[nodiscard]] std::vector<float> writeM4a(const std::filesystem::path &path, FileFormat format)
{
    FileSpec spec;
    spec.format = format;
    spec.dither = false;
    auto writer = AudioFileWriter::create(path, spec);
    EXPECT_TRUE(writer.has_value());
    std::vector<float> samples(96000 * 2, 0.0f);
    for (std::size_t frame = 48000; frame < 96000; ++frame) {
        const double swell = (1.0 + 0.5 * std::sin(static_cast<double>(frame) / 300.0)) / 1.5;
        const auto value = static_cast<float>(0.5 * swell * std::sin(2.0 * std::numbers::pi * 441.0 * frame / 48000.0));
        samples[frame * 2] = value;
        samples[frame * 2 + 1] = value;
    }
    if (writer) {
        EXPECT_TRUE(writer->write(samples).has_value());
        EXPECT_TRUE(writer->close().has_value());
    }
    return samples;
}

/// How many frames what \a reader gives after a seek to \a frame is off
/// from what was written there: 0 when the seek landed on the frame.
[[nodiscard]] int lagAfterSeek(AudioFileReader &reader, const std::vector<float> &written, std::uint64_t frame)
{
    reader.seek(frame);
    std::vector<float> got(2048 * 2);
    const std::size_t read = reader.read(got, 2048);
    int best = 0;
    double bestError = 1e30;
    for (int lag = -2048; lag <= 2048; ++lag) {
        double error = 0.0;
        for (std::size_t index = 0; index < read; ++index) {
            const long at = static_cast<long>(frame) + static_cast<long>(index) + lag;
            const float expected = at >= 0 && at < 96000 ? written[static_cast<std::size_t>(at) * 2] : 0.0f;
            error += static_cast<double>(got[index * 2] - expected) * (got[index * 2] - expected);
        }
        if (error < bestError) {
            bestError = error;
            best = lag;
        }
    }
    return best;
}

} // namespace

TEST(AudioFileReaderTest, AnM4aIsReadThroughFfmpegWithItsLengthAndItsSound)
{
    // **M4A is what libsndfile does not read**: AAC and Apple
    // Lossless, the two the Recorder writes where FFmpeg encodes them.
    for (const FileFormat format : {FileFormat::AacM4a, FileFormat::AlacM4a}) {
        SCOPED_TRACE(std::string{fileFormatId(format)});
        if (formatUnavailable(format).has_value()) {
            std::cout << "this FFmpeg does not encode " << fileFormatId(format) << "; not checked\n";
            continue;
        }
        const ScratchFolder folder;
        const std::filesystem::path path = folder / ("tone." + std::string{fileExtension(format)});
        const std::vector<float> written = writeM4a(path, format);

        std::optional<AudioFileReader> reader = AudioFileReader::open(path);
        ASSERT_TRUE(reader.has_value()) << "FFmpeg did not open what the Recorder wrote";
        EXPECT_EQ(reader->sampleRate(), 48000U);
        EXPECT_EQ(reader->channels(), 2U);
        EXPECT_NEAR(static_cast<double>(reader->frames()) / 48000.0, 2.0, 0.05);

        std::vector<float> decoded;
        std::vector<float> buffer(4096 * 2);
        for (std::size_t got = 0; (got = reader->read(buffer, 4096)) > 0;) {
            decoded.insert(decoded.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(got * 2));
        }
        const std::size_t frames = decoded.size() / 2;
        EXPECT_NEAR(static_cast<double>(frames) / 48000.0, 2.0, 0.05) << frames << " frames decoded";
        float silent = 0.0f;
        float tone = 0.0f;
        for (std::size_t frame = 0; frame < frames; ++frame) {
            if (frame < 46000) {
                silent = std::max(silent, std::abs(decoded[frame * 2]));
            } else if (frame > 50000) {
                tone = std::max(tone, std::abs(decoded[frame * 2]));
            }
        }
        EXPECT_LT(silent, 0.01f) << "the silent second is not silent";
        EXPECT_NEAR(tone, 0.5f, 0.05f) << "the tone is not at its level";

        // **A seek lands on the frame**, which is what "accurate to about
        // 10 ms" asks and more: what is read after it lines up with what
        // was written there, not a packet (1024 frames, 21 ms) either side.
        for (const std::uint64_t frame : {50000U, 72000U, 90001U}) {
            EXPECT_EQ(lagAfterSeek(*reader, written, frame), 0) << "after a seek to frame " << frame;
        }
    }
}
