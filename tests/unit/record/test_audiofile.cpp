// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The files a Recorder writes: every format comes back through
// libsndfile at the rate and the length it was written with, and with the
// sound it was given.

#include "record/audiofile.h"
#include "recordfixtures.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>

using namespace rb::record;
using namespace rb::recordtest;

namespace
{

constexpr std::size_t kFrames = 72000; // 1.5 s at 48 kHz
constexpr float kAmplitude = 0.5f; // -6.02 dBFS peak, -9.03 dB RMS

[[nodiscard]] std::expected<std::uint64_t, FileError>
writeFile(const std::filesystem::path &path, const FileSpec &spec, const std::vector<float> &samples)
{
    std::expected<AudioFileWriter, FileError> writer = AudioFileWriter::create(path, spec);
    if (!writer) {
        return std::unexpected(writer.error());
    }
    // In pieces, the way the writer thread hands it over.
    for (std::size_t at = 0; at < samples.size(); at += 4096 * 2) {
        const std::size_t count = std::min<std::size_t>(4096 * 2, samples.size() - at);
        if (const auto written = writer->write(std::span<const float>{samples}.subspan(at, count)); !written) {
            return std::unexpected(written.error());
        }
    }
    const std::uint64_t frames = writer->framesWritten();
    if (const auto closed = writer->close(); !closed) {
        return std::unexpected(closed.error());
    }
    return frames;
}

struct Lossless {
    FileFormat format;
    SampleFormat sample;
    int major;
    int subtype;
    /// How far a sample may come back from where it went, in full scale:
    /// rounding and dither, one step either side and a half.
    float tolerance;
};

} // namespace

TEST(AudioFile, EveryLosslessFormatComesBackAtItsRateAndLengthWithItsSound)
{
    const std::vector<float> samples = stereoSine(kFrames, 1000.0, kAmplitude);
    ScratchFolder folder;
    const float step16 = 1.0f / 32768.0f;
    const float step24 = 1.0f / 8388608.0f;
    for (const Lossless &kind : {
             // A WAV under 4 GB is closed as a WAV, not RF64: the extensible
             // kind, which says which speaker each channel is for.
             Lossless{FileFormat::Wav, SampleFormat::Int16, SF_FORMAT_WAVEX, SF_FORMAT_PCM_16, 1.6f * step16},
             Lossless{FileFormat::Wav, SampleFormat::Int24, SF_FORMAT_WAVEX, SF_FORMAT_PCM_24, 1.6f * step24},
             Lossless{FileFormat::Wav, SampleFormat::Float32, SF_FORMAT_WAVEX, SF_FORMAT_FLOAT, 0.0f},
             Lossless{FileFormat::Aiff, SampleFormat::Int16, SF_FORMAT_AIFF, SF_FORMAT_PCM_16, 1.6f * step16},
             Lossless{FileFormat::Aiff, SampleFormat::Int24, SF_FORMAT_AIFF, SF_FORMAT_PCM_24, 1.6f * step24},
             Lossless{FileFormat::Aiff, SampleFormat::Float32, SF_FORMAT_AIFF, SF_FORMAT_FLOAT, 0.0f},
             Lossless{FileFormat::Flac, SampleFormat::Int16, SF_FORMAT_FLAC, SF_FORMAT_PCM_16, 1.6f * step16},
             Lossless{FileFormat::Flac, SampleFormat::Int24, SF_FORMAT_FLAC, SF_FORMAT_PCM_24, 1.6f * step24},
             // FLAC has no floating point: 24 bits is the most it keeps.
             Lossless{FileFormat::Flac, SampleFormat::Float32, SF_FORMAT_FLAC, SF_FORMAT_PCM_24, 1.6f * step24},
         }) {
        const std::string name = std::format(
            "{}-{}.{}", fileFormatId(kind.format), static_cast<int>(kind.sample), fileExtension(kind.format));
        SCOPED_TRACE(name);
        FileSpec spec;
        spec.format = kind.format;
        spec.sampleFormat = kind.sample;
        const std::expected<std::uint64_t, FileError> written = writeFile(folder / name, spec, samples);
        ASSERT_TRUE(written.has_value()) << fileErrorName(written.error());
        EXPECT_EQ(*written, kFrames);

        const ReadBack file = readBack(folder / name);
        ASSERT_TRUE(file.opened);
        EXPECT_EQ(file.format & SF_FORMAT_TYPEMASK, kind.major);
        EXPECT_EQ(file.format & SF_FORMAT_SUBMASK, kind.subtype);
        EXPECT_EQ(file.sampleRate, 48000);
        EXPECT_EQ(file.channels, 2);
        EXPECT_EQ(file.headerFrames, static_cast<std::int64_t>(kFrames));
        ASSERT_EQ(file.frames(), kFrames);
        float worst = 0.0f;
        for (std::size_t index = 0; index < samples.size(); ++index) {
            worst = std::max(worst, std::fabs(file.samples[index] - samples[index]));
        }
        EXPECT_LE(worst, kind.tolerance);
    }
}

// The lossy ones are judged the way a listener would judge them: the rate,
// a length within an encoder's padding, and the tone at its level.
TEST(AudioFile, EveryLossyFormatComesBackAtItsRateAndLengthWithItsSound)
{
    const std::vector<float> samples = stereoSine(kFrames, 1000.0, kAmplitude);
    ScratchFolder folder;
    for (const FileFormat format : {FileFormat::Mp3, FileFormat::Opus, FileFormat::Vorbis}) {
        const std::string name = std::format("lossy.{}", fileExtension(format));
        SCOPED_TRACE(name);
        FileSpec spec;
        spec.format = format;
        const std::expected<std::uint64_t, FileError> written = writeFile(folder / name, spec, samples);
        ASSERT_TRUE(written.has_value()) << fileErrorName(written.error());

        const ReadBack file = readBack(folder / name);
        ASSERT_TRUE(file.opened);
        EXPECT_EQ(file.sampleRate, 48000);
        EXPECT_EQ(file.channels, 2);
        // An encoder pads to whole frames of its own, and an MP3 decoder
        // says its padding out loud; a few thousand frames is 60 ms.
        EXPECT_NEAR(static_cast<double>(file.frames()), static_cast<double>(kFrames), 3000.0);
        // Away from both ends, where an encoder is settling, and within a
        // decibel: an encoder at its default quality spends its bits where a
        // listener would notice, not on the level of a test tone.
        EXPECT_NEAR(rmsDb(file.samples, 2, 0, 12000, 60000), -9.03, 1.0);
        EXPECT_NEAR(rmsDb(file.samples, 2, 1, 12000, 60000), -9.03, 1.0);
    }
}

TEST(AudioFile, FewerBitsAreDitheredOneStepEitherSide)
{
    ScratchFolder folder;
    // Silence at 16 bits comes back as a noise one step either side of
    // nothing and never more: triangular, as wide as two steps.
    FileSpec spec;
    spec.format = FileFormat::Wav;
    spec.sampleFormat = SampleFormat::Int16;
    const std::vector<float> silence(48000 * 2, 0.0f);
    ASSERT_TRUE(writeFile(folder / "silence.wav", spec, silence).has_value());
    const ReadBack file = readBack(folder / "silence.wav");
    ASSERT_EQ(file.frames(), 48000U);
    int nonZero = 0;
    for (const float sample : file.samples) {
        const float steps = sample * 32768.0f;
        EXPECT_LE(std::fabs(steps), 1.0f);
        nonZero += steps != 0.0f ? 1 : 0;
    }
    // Two uniform draws sum to more than half a step (and so round away
    // from zero) a quarter of the time.
    EXPECT_NEAR(static_cast<double>(nonZero) / static_cast<double>(file.samples.size()), 0.25, 0.02);

    // **A tone quieter than one step is still there**, as a signal under the
    // noise, rather than rounded away to nothing.
    const std::vector<float> faint = stereoSine(48000, 1000.0, 0.4f / 32768.0f);
    ASSERT_TRUE(writeFile(folder / "faint.wav", spec, faint).has_value());
    const ReadBack quiet = readBack(folder / "faint.wav");
    double correlation = 0.0;
    for (std::size_t index = 0; index < faint.size(); index += 2) {
        correlation += static_cast<double>(quiet.samples[index]) * static_cast<double>(faint[index]);
    }
    EXPECT_GT(correlation, 0.0);

    // And with dither off, a signal already on the steps comes back exactly.
    spec.dither = false;
    std::vector<float> steps(1000 * 2);
    for (std::size_t index = 0; index < steps.size(); ++index) {
        steps[index] = static_cast<float>(static_cast<int>(index % 2001) - 1000) / 32768.0f;
    }
    ASSERT_TRUE(writeFile(folder / "steps.wav", spec, steps).has_value());
    EXPECT_EQ(readBack(folder / "steps.wav").samples, steps);
}

TEST(AudioFile, AFileThatIsThereIsNeverOverwritten)
{
    ScratchFolder folder;
    {
        std::ofstream existing(folder / "taken.wav");
        existing << "somebody's recording";
    }
    const std::expected<AudioFileWriter, FileError> writer = AudioFileWriter::create(folder / "taken.wav", FileSpec{});
    ASSERT_FALSE(writer.has_value());
    EXPECT_EQ(writer.error(), FileError::FileExists);
    std::ifstream kept(folder / "taken.wav");
    std::string text;
    std::getline(kept, text);
    EXPECT_EQ(text, "somebody's recording");
}

TEST(AudioFile, AFolderThatIsNotThereIsMade)
{
    ScratchFolder folder;
    const std::filesystem::path path = folder / "Reverblabr/2026/take.flac";
    FileSpec spec;
    spec.format = FileFormat::Flac;
    ASSERT_TRUE(writeFile(path, spec, stereoSine(4800, 440.0, 0.1f)).has_value());
    EXPECT_TRUE(std::filesystem::exists(path));
}

TEST(AudioFile, WhatCannotBeMadeSaysWhy)
{
    ScratchFolder folder;
    // A folder where a file is.
    {
        std::ofstream file(folder / "notAFolder");
    }
    std::expected<AudioFileWriter, FileError> writer =
        AudioFileWriter::create(folder / "notAFolder/take.wav", FileSpec{});
    ASSERT_FALSE(writer.has_value());
    EXPECT_EQ(writer.error(), FileError::FolderNotCreated);

    // Opus is made at 48 kHz and the rates below it, never at 44.1.
    FileSpec opus;
    opus.format = FileFormat::Opus;
    opus.sampleRate = 44100;
    writer = AudioFileWriter::create(folder / "take.opus", opus);
    ASSERT_FALSE(writer.has_value());
    EXPECT_EQ(writer.error(), FileError::UnsupportedFormat);
    EXPECT_FALSE(std::filesystem::exists(folder / "take.opus")) << "a file that could not be made was left behind";
}

TEST(AudioFile, FormatsAreKnownByTheIdsASessionHolds)
{
    for (const FileFormat format :
         {FileFormat::Wav, FileFormat::Aiff, FileFormat::Flac, FileFormat::Mp3, FileFormat::Opus, FileFormat::Vorbis}) {
        EXPECT_EQ(fileFormatFromId(fileFormatId(format)), format);
    }
    EXPECT_EQ(fileExtension(FileFormat::Vorbis), "ogg");
    EXPECT_FALSE(fileFormatFromId("cassette").has_value());
    EXPECT_EQ(sampleFormatFromId("int16"), SampleFormat::Int16);
    EXPECT_EQ(sampleFormatFromId("float32"), SampleFormat::Float32);
}

// **A lossy format's bit rate is what it was asked for**, within what
// an encoder does with it: more kilobits make a bigger file, and a constant
// rate lands near the number.
TEST(AudioFile, ALossyFormatsBitRateFollowsWhatWasAskedFor)
{
    ScratchFolder folder;
    const std::vector<float> noise = [] {
        std::vector<float> samples(48000 * 2 * 4);
        std::uint32_t state = 57;
        for (float &sample : samples) {
            state = state * 1664525U + 1013904223U;
            sample = (static_cast<float>(state >> 8) / 16777216.0f - 0.5f) * 0.5f;
        }
        return samples;
    }();
    for (const FileFormat format : {FileFormat::Mp3, FileFormat::Opus, FileFormat::Vorbis}) {
        SCOPED_TRACE(fileFormatId(format));
        std::uintmax_t previous = 0;
        for (const std::uint32_t kbps : {64U, 128U, 256U}) {
            FileSpec spec;
            spec.format = format;
            spec.bitrateKbps = kbps;
            spec.variableBitrate = false;
            const std::filesystem::path path =
                folder / std::format("{}-{}.{}", fileFormatId(format), kbps, fileExtension(format));
            ASSERT_TRUE(writeFile(path, spec, noise).has_value());
            const std::uintmax_t size = std::filesystem::file_size(path);
            const double actualKbps = static_cast<double>(size) * 8.0 / 4.0 / 1000.0;
            std::cout << fileFormatId(format) << " at " << kbps << " kbps: " << actualKbps << " kbps\n";
            EXPECT_GT(size, previous) << "asking for " << kbps << " kbps made a file no bigger";
            previous = size;
            if (format == FileFormat::Mp3) {
                EXPECT_NEAR(actualKbps, kbps, kbps * 0.25);
            }
        }
    }
}
