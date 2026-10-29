// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// An impulse response read from a file: every channel apart, at the
// file's rate, anything that is not one refused, and resampled to the
// graph's rate the same length in time.

#include "record/impulsefile.h"
#include "recordfixtures.h"

#include <gtest/gtest.h>
#include <sndfile.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>

using namespace rb;
using namespace rb::record;
using namespace rb::recordtest;

namespace
{

/// \a frames of \a channels at \a rate, channel c's sample n being
/// (c + 1) / 8 on every 100th frame and nothing between.
void writeFile(const std::filesystem::path &path, int format, int rate, int channels, std::size_t frames)
{
    SF_INFO info{};
    info.samplerate = rate;
    info.channels = channels;
    info.format = format;
    SNDFILE *file = sf_open(path.c_str(), SFM_WRITE, &info);
    ASSERT_NE(file, nullptr) << sf_strerror(nullptr);
    std::vector<float> samples(frames * static_cast<std::size_t>(channels), 0.0f);
    for (std::size_t frame = 0; frame < frames; frame += 100) {
        for (int channel = 0; channel < channels; ++channel) {
            samples[frame * static_cast<std::size_t>(channels) + static_cast<std::size_t>(channel)] =
                static_cast<float>(channel + 1) / 8.0f;
        }
    }
    ASSERT_EQ(sf_writef_float(file, samples.data(), static_cast<sf_count_t>(frames)), static_cast<sf_count_t>(frames));
    sf_close(file);
}

} // namespace

TEST(ImpulseFileTest, AFileReadsBackAsItsChannelsAtItsRate)
{
    const ScratchFolder folder;
    const std::filesystem::path path = folder / "room.flac";
    writeFile(path, SF_FORMAT_FLAC | SF_FORMAT_PCM_24, 44100, 4, 1000);

    const std::expected<ImpulseFile, ImpulseFileError> read = readImpulseFile(path);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->sampleRate, 44100U);
    ASSERT_EQ(read->channels.size(), 4U);
    EXPECT_EQ(read->frames(), 1000U);
    for (std::size_t channel = 0; channel < 4; ++channel) {
        EXPECT_NEAR(read->channels[channel][300], static_cast<float>(channel + 1) / 8.0f, 1e-6f);
        EXPECT_EQ(read->channels[channel][301], 0.0f);
    }
}

TEST(ImpulseFileTest, APathUnderTheHomeFolderIsWrittenWithATilde)
{
    const ScratchFolder folder;
    writeFile(folder / "room.wav", SF_FORMAT_WAV | SF_FORMAT_FLOAT, 48000, 2, 500);
    const std::string previous = std::getenv("HOME") != nullptr ? std::getenv("HOME") : "";
    ::setenv("HOME", folder.path().c_str(), 1);
    const std::expected<ImpulseFile, ImpulseFileError> read = readImpulseFile("~/room.wav");
    ::setenv("HOME", previous.c_str(), 1);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->frames(), 500U);
}

TEST(ImpulseFileTest, WhatIsNotAResponseIsRefusedAndSaysWhy)
{
    const ScratchFolder folder;
    EXPECT_EQ(readImpulseFile(folder / "missing.wav").error(), ImpulseFileError::Unreadable);

    const std::filesystem::path text = folder / "notes.wav";
    std::ofstream(text) << "not audio";
    EXPECT_EQ(readImpulseFile(text).error(), ImpulseFileError::Unreadable);

    const std::filesystem::path empty = folder / "empty.wav";
    writeFile(empty, SF_FORMAT_WAV | SF_FORMAT_FLOAT, 48000, 2, 0);
    EXPECT_EQ(readImpulseFile(empty).error(), ImpulseFileError::Empty);

    // A second over the limit, at a low rate to keep the file small.
    const std::filesystem::path song = folder / "song.wav";
    writeFile(song,
              SF_FORMAT_WAV | SF_FORMAT_PCM_16,
              8000,
              1,
              static_cast<std::size_t>((kMaxImpulseFileSeconds + 1.0) * 8000.0));
    EXPECT_EQ(readImpulseFile(song).error(), ImpulseFileError::TooLong);
}

TEST(ImpulseFileTest, ResamplingKeepsTheLengthAndTheTimes)
{
    ImpulseFile impulse;
    impulse.sampleRate = 44100;
    impulse.channels.assign(2, std::vector<float>(4410, 0.0f));
    impulse.channels[0][441] = 1.0f;
    impulse.channels[1][882] = 1.0f;

    EXPECT_EQ(resampleImpulse(impulse, 44100), impulse.channels);

    const std::vector<std::vector<float>> converted = resampleImpulse(impulse, 48000);
    ASSERT_EQ(converted.size(), 2U);
    EXPECT_EQ(converted[0].size(), 4800U);
    auto loudest = [](const std::vector<float> &channel) {
        return static_cast<std::size_t>(std::distance(
            channel.begin(),
            std::ranges::max_element(channel, [](float a, float b) { return std::abs(a) < std::abs(b); })));
    };
    // 10 ms and 20 ms in, at either rate.
    EXPECT_EQ(loudest(converted[0]), 480U);
    EXPECT_EQ(loudest(converted[1]), 960U);
}
