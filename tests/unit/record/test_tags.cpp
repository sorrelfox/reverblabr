// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A recording's tags: written into every format by TagLib, read back
// by TagLib, Cyrillic and CJK and all — and the audio left exactly as it
// was.

#include "record/audiofile.h"
#include "record/tags.h"
#include "recordfixtures.h"

#include <gtest/gtest.h>
#include <infotag.h>
#include <wavfile.h>

#include <chrono>
#include <string>

using namespace rb::record;
using namespace rb::recordtest;
using namespace std::chrono;

namespace
{

/// Every field set, in three scripts.
[[nodiscard]] Tags everyField()
{
    Tags tags;
    tags.title = "Выпуск 12 — 東京からの手紙";
    tags.artist = "Анна и 李明";
    tags.album = "Podcast, season 3";
    tags.genre = "Speech";
    tags.year = "2026";
    tags.track = "12";
    tags.trackCount = "40";
    tags.disc = "1";
    tags.discCount = "2";
    tags.comment = "Recorded live. 録音 — запись.";
    return tags;
}

[[nodiscard]] std::filesystem::path fileIn(const ScratchFolder &folder, FileFormat format, SampleFormat sample)
{
    const std::filesystem::path path = folder / std::format("tagged.{}", fileExtension(format));
    FileSpec spec;
    spec.format = format;
    spec.sampleFormat = sample;
    spec.dither = false;
    std::expected<AudioFileWriter, FileError> writer = AudioFileWriter::create(path, spec);
    EXPECT_TRUE(writer.has_value());
    EXPECT_TRUE(writer->write(stereoSine(48000, 1000.0, 0.5f)).has_value());
    EXPECT_TRUE(writer->close().has_value());
    return path;
}

} // namespace

TEST(Tags, EveryFieldRoundTripsThroughEveryFormatInEveryScript)
{
    for (const FileFormat format :
         {FileFormat::Wav, FileFormat::Aiff, FileFormat::Flac, FileFormat::Mp3, FileFormat::Opus, FileFormat::Vorbis}) {
        SCOPED_TRACE(fileFormatId(format));
        ScratchFolder folder;
        const std::filesystem::path path = fileIn(folder, format, SampleFormat::Float32);
        const ReadBack before = readBack(path);

        ASSERT_TRUE(writeTags(path, format, everyField()));
        const std::optional<Tags> read = readTags(path, format);
        ASSERT_TRUE(read.has_value());
        EXPECT_EQ(*read, everyField());

        // **The audio is not touched**: the same frames, and for the formats
        // that keep samples, the same samples.
        const ReadBack after = readBack(path);
        ASSERT_TRUE(after.opened) << "the tagged file no longer opens";
        EXPECT_EQ(after.sampleRate, before.sampleRate);
        EXPECT_EQ(after.frames(), before.frames());
        if (format == FileFormat::Wav || format == FileFormat::Aiff || format == FileFormat::Flac) {
            EXPECT_EQ(after.samples, before.samples);
        }
    }
}

TEST(Tags, OnlyWhatIsSetIsWrittenAndNothingAtAllTouchesNoFile)
{
    ScratchFolder folder;
    const std::filesystem::path path = fileIn(folder, FileFormat::Flac, SampleFormat::Int24);
    const auto size = std::filesystem::file_size(path);
    EXPECT_TRUE(writeTags(path, FileFormat::Flac, Tags{}));
    EXPECT_EQ(std::filesystem::file_size(path), size);

    Tags titleOnly;
    titleOnly.title = "Only a title";
    ASSERT_TRUE(writeTags(path, FileFormat::Flac, titleOnly));
    EXPECT_EQ(readTags(path, FileFormat::Flac), titleOnly);
}

TEST(Tags, TokensAreFilledInForEachFile)
{
    Tags patterns;
    patterns.title = "%session, part %counter";
    patterns.year = "%year";
    patterns.track = "%counter";
    patterns.comment = "  Recorded %date at %time by %block  ";
    FileNameValues values;
    values.session = "Подкаст";
    values.block = "rec";
    values.when = local_days{year{2026} / September / 25} + hours{14} + minutes{3} + seconds{22};
    values.counter = 7;
    const Tags tags = renderTags(patterns, values);
    EXPECT_EQ(tags.title, "Подкаст, part 007");
    EXPECT_EQ(tags.year, "2026");
    EXPECT_EQ(tags.track, "007");
    EXPECT_EQ(tags.comment, "Recorded 2026-09-25 at 14-03-22 by rec");
    EXPECT_TRUE(tags.artist.empty());
}

TEST(Tags, AFileThatIsNotWhatItSaysIsRefusedNotBroken)
{
    ScratchFolder folder;
    const std::filesystem::path path = fileIn(folder, FileFormat::Flac, SampleFormat::Int24);
    const auto size = std::filesystem::file_size(path);
    EXPECT_FALSE(writeTags(path, FileFormat::Mp3, everyField()));
    EXPECT_FALSE(writeTags(folder / "missing.mp3", FileFormat::Mp3, everyField()));
    EXPECT_EQ(std::filesystem::file_size(path), size);
}

// The INFO list is what most players read from a WAV: the fields it has a
// place for are there too, in UTF-8.
TEST(Tags, AWavsInfoListHoldsWhatItHasRoomFor)
{
    ScratchFolder folder;
    const std::filesystem::path path = fileIn(folder, FileFormat::Wav, SampleFormat::Int24);
    ASSERT_TRUE(writeTags(path, FileFormat::Wav, everyField()));
    TagLib::RIFF::WAV::File file(path.c_str(), false);
    ASSERT_TRUE(file.hasInfoTag());
    EXPECT_EQ(file.InfoTag()->title().to8Bit(true), everyField().title);
    EXPECT_EQ(file.InfoTag()->artist().to8Bit(true), everyField().artist);
    EXPECT_EQ(file.InfoTag()->album().to8Bit(true), everyField().album);
    EXPECT_EQ(file.InfoTag()->comment().to8Bit(true), everyField().comment);
}
