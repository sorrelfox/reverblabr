// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A recording read back for the recordings index: how long it is, at
// what rate, and the outline its waveform is drawn from.

#include "record/audiofile.h"
#include "record/summary.h"
#include "recordfixtures.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>

using namespace rb;
using namespace rb::record;
using namespace rb::recordtest;

namespace
{

/// Writes \a seconds of stereo at 48 kHz: silence for the first half, then
/// a 440 Hz tone whose peaks reach \a level.
void writeHalfSilentHalfTone(const std::filesystem::path &path, FileFormat format, double seconds, float level)
{
    FileSpec spec;
    spec.format = format;
    spec.sampleFormat = SampleFormat::Float32;
    spec.dither = false;
    auto writer = AudioFileWriter::create(path, spec);
    ASSERT_TRUE(writer.has_value());
    const auto frames = static_cast<std::size_t>(seconds * 48000.0);
    std::vector<float> samples(frames * 2, 0.0f);
    for (std::size_t frame = frames / 2; frame < frames; ++frame) {
        const float value = level * static_cast<float>(std::sin(2.0 * std::numbers::pi * 440.0 * frame / 48000.0));
        samples[frame * 2] = value;
        samples[frame * 2 + 1] = value;
    }
    ASSERT_TRUE(writer->write(samples).has_value());
    ASSERT_TRUE(writer->close().has_value());
}

} // namespace

TEST(SummaryTest, AWaveReadsBackItsLengthRateAndShape)
{
    const ScratchFolder folder;
    const std::filesystem::path path = folder / "take.wav";
    writeHalfSilentHalfTone(path, FileFormat::Wav, 2.0, 0.5f);

    const std::optional<FileSummary> summary = summarise(path, 64);
    ASSERT_TRUE(summary.has_value());
    EXPECT_EQ(summary->frames, 96000U);
    EXPECT_EQ(summary->sampleRate, 48000U);
    EXPECT_EQ(summary->channels, 2U);
    ASSERT_EQ(summary->peaks.size(), 64U);
    // The first half silent, the second at the tone's level.
    for (std::size_t bucket = 0; bucket < 31; ++bucket) {
        EXPECT_EQ(summary->peaks[bucket], 0.0f) << "bucket " << bucket;
    }
    for (std::size_t bucket = 33; bucket < 64; ++bucket) {
        EXPECT_NEAR(summary->peaks[bucket], 0.5f, 0.001f) << "bucket " << bucket;
    }
}

TEST(SummaryTest, EveryFormatTheRecorderWritesReadsBack)
{
    const ScratchFolder folder;
    for (const FileFormat format :
         {FileFormat::Wav, FileFormat::Aiff, FileFormat::Flac, FileFormat::Mp3, FileFormat::Opus, FileFormat::Vorbis}) {
        const std::filesystem::path path = folder / ("take." + std::string{fileExtension(format)});
        writeHalfSilentHalfTone(path, format, 1.0, 0.5f);
        const std::optional<FileSummary> summary = summarise(path, 32);
        ASSERT_TRUE(summary.has_value()) << fileFormatId(format);
        // The lossy ones pad and trim at their ends, by a few hundredths of
        // a second at most.
        EXPECT_NEAR(static_cast<double>(summary->frames) / summary->sampleRate, 1.0, 0.06) << fileFormatId(format);
        EXPECT_EQ(summary->channels, 2U) << fileFormatId(format);
        EXPECT_NEAR(*std::ranges::max_element(summary->peaks), 0.5f, 0.1f) << fileFormatId(format);
        EXPECT_LT(summary->peaks.front(), 0.05f) << fileFormatId(format) << " is not silent where it was";
    }
}

TEST(SummaryTest, AFileShorterThanItsBucketsLeavesNoGapsInTheMiddle)
{
    const ScratchFolder folder;
    const std::filesystem::path path = folder / "short.wav";
    writeHalfSilentHalfTone(path, FileFormat::Wav, 0.05, 0.5f); // 2400 frames: three chunks
    const std::optional<FileSummary> summary = summarise(path, 48);
    ASSERT_TRUE(summary.has_value());
    ASSERT_EQ(summary->peaks.size(), 48U);
    EXPECT_GT(summary->peaks.back(), 0.4f);
}

TEST(SummaryTest, WhatIsNotAudioIsNothing)
{
    const ScratchFolder folder;
    const std::filesystem::path path = folder / "notes.wav";
    std::ofstream(path) << "not a sound file";
    EXPECT_FALSE(summarise(path, 16).has_value());
    EXPECT_FALSE(summarise(folder / "missing.wav", 16).has_value());
}
