// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A recording a crash cut off: it plays as it is, and the next start
// gives it back everything that reached the disk.
//
// **The crash is real**: a child process writes the file and is killed
// with SIGKILL in the middle, so nothing of ours or libsndfile's gets to
// tidy up — which is what a crash, an out-of-memory kill or a power button
// leaves behind.

#include "record/audiofile.h"
#include "record/recordingwriter.h"
#include "record/repair.h"
#include "recordfixtures.h"
#include "rt/audiotap.h"

#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <csignal>
#include <fstream>
#include <memory>
#include <thread>

using namespace rb;
using namespace rb::record;
using namespace rb::recordtest;

namespace
{

constexpr std::size_t kRefreshed = 48000; // what the header was last told
constexpr std::size_t kTotal = 72000; // what reached the disk

/// Runs \a work in a child process, waits for it to say it is done, and
/// kills it where it stands. False if the child could not do its work.
template <typename Work>
[[nodiscard]] bool killAfter(Work work)
{
    std::array<int, 2> pipe{};
    if (::pipe(pipe.data()) != 0) {
        return false;
    }
    const pid_t child = ::fork();
    if (child < 0) {
        return false;
    }
    if (child == 0) {
        ::close(pipe[0]);
        const char done = work() ? 'y' : 'n';
        (void)!::write(pipe[1], &done, 1);
        for (;;) {
            ::pause(); // until the parent kills it
        }
    }
    ::close(pipe[1]);
    char done = 0;
    const bool said = ::read(pipe[0], &done, 1) == 1;
    ::close(pipe[0]);
    ::kill(child, SIGKILL);
    int status = 0;
    ::waitpid(child, &status, 0);
    return said && done == 'y' && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL;
}

/// Writes 1 s, brings the header up to date, writes another half second,
/// and says it is done — to be killed before it can close.
[[nodiscard]] bool writeWithoutClosing(const std::filesystem::path &path, const FileSpec &spec)
{
    const std::vector<float> samples = stereoSine(kTotal, 1000.0, 0.5f);
    std::expected<AudioFileWriter, FileError> writer = AudioFileWriter::create(path, spec);
    if (!writer || !RecordingJournal(path.parent_path() / "journal").add(path)) {
        return false;
    }
    const bool first = writer->write(std::span<const float>{samples}.first(kRefreshed * 2)).has_value();
    writer->refreshHeader();
    const bool second = writer->write(std::span<const float>{samples}.subspan(kRefreshed * 2)).has_value();
    // What libsndfile has written is the kernel's now, and outlives the
    // process; the writer object never gets to close, because the process
    // does not get to leave this function.
    static std::optional<AudioFileWriter> leaked;
    leaked.emplace(std::move(*writer));
    return first && second;
}

} // namespace

TEST(Repair, AKilledWavPlaysToItsLastUpdateAndIsWholeAfterTheNextStart)
{
    ScratchFolder folder;
    const std::filesystem::path path = folder / "take.wav";
    FileSpec spec;
    spec.sampleFormat = SampleFormat::Float32; // so the sound can be checked exactly
    ASSERT_TRUE(killAfter([&] { return writeWithoutClosing(path, spec); }));

    // **Playable as it is**, up to the last time its header was brought up
    // to date.
    const ReadBack killed = readBack(path);
    ASSERT_TRUE(killed.opened);
    EXPECT_EQ(killed.frames(), kRefreshed);

    const std::vector<RepairReport> reports = repairOrphans(folder / "journal");
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_EQ(reports[0].file, path);
    ASSERT_TRUE(reports[0].result.has_value()) << repairErrorName(reports[0].result.error());
    EXPECT_TRUE(reports[0].result->changed);
    EXPECT_EQ(reports[0].result->frames, kTotal);

    // Everything that reached the disk, exactly as it was written.
    const ReadBack repaired = readBack(path);
    EXPECT_EQ(repaired.headerFrames, static_cast<std::int64_t>(kTotal));
    EXPECT_EQ(repaired.samples, stereoSine(kTotal, 1000.0, 0.5f));
    // And the journal has forgotten it: the next start does nothing.
    EXPECT_TRUE(repairOrphans(folder / "journal").empty());
}

TEST(Repair, AKilledAiffIsWholeAfterTheNextStart)
{
    ScratchFolder folder;
    const std::filesystem::path path = folder / "take.aiff";
    FileSpec spec;
    spec.format = FileFormat::Aiff;
    spec.sampleFormat = SampleFormat::Float32;
    ASSERT_TRUE(killAfter([&] { return writeWithoutClosing(path, spec); }));
    EXPECT_EQ(readBack(path).frames(), kRefreshed);

    const std::vector<RepairReport> reports = repairOrphans(folder / "journal");
    ASSERT_EQ(reports.size(), 1U);
    ASSERT_TRUE(reports[0].result.has_value()) << repairErrorName(reports[0].result.error());
    EXPECT_EQ(reports[0].result->frames, kTotal);
    const ReadBack repaired = readBack(path);
    EXPECT_EQ(repaired.headerFrames, static_cast<std::int64_t>(kTotal));
    EXPECT_EQ(repaired.samples, stereoSine(kTotal, 1000.0, 0.5f));
}

// A FLAC is a stream, so a killed one plays already; what it cannot say is
// how long it is, because the encoder writes that when it finishes.
TEST(Repair, AKilledFlacSaysHowLongItIsAfterTheNextStart)
{
    ScratchFolder folder;
    const std::filesystem::path path = folder / "take.flac";
    FileSpec spec;
    spec.format = FileFormat::Flac;
    ASSERT_TRUE(killAfter([&] { return writeWithoutClosing(path, spec); }));
    const ReadBack killed = readBack(path);
    ASSERT_TRUE(killed.opened);
    // The encoder writes whole blocks of 4096 frames; the part of one it
    // was still gathering died with the process.
    EXPECT_GE(killed.frames(), kTotal - 4096);

    const std::vector<RepairReport> reports = repairOrphans(folder / "journal");
    ASSERT_EQ(reports.size(), 1U);
    ASSERT_TRUE(reports[0].result.has_value()) << repairErrorName(reports[0].result.error());
    const ReadBack repaired = readBack(path);
    EXPECT_EQ(repaired.headerFrames, static_cast<std::int64_t>(killed.frames()));
    EXPECT_EQ(repaired.frames(), killed.frames());
}

// **Through the writer thread and its journal**, the way the engine does it:
// the header was never brought up to date after the file was made, so the
// killed file says it holds nothing at all.
TEST(Repair, ARecordingKilledMidWayIsFoundAndRepairedFromTheJournal)
{
    ScratchFolder folder;
    const std::filesystem::path journal = folder / "journal";
    ASSERT_TRUE(killAfter([&] {
        auto tap = std::make_shared<rt::AudioTap>(2, 96000);
        WriterOptions options;
        options.journalFolder = journal;
        options.headerInterval = std::chrono::hours{1};
        options.dither = false;
        static std::unique_ptr<RecordingWriter> writer; // never destroyed: it is killed
        writer = std::make_unique<RecordingWriter>(nullptr, options);
        writer->attach(1, tap);
        RecordingSettings settings;
        settings.format = FileFormat::Wav;
        settings.sampleFormat = SampleFormat::Float32;
        settings.folder = folder.path();
        settings.fileNamePattern = "killed";
        writer->start(1, settings);
        writer->sync();
        const std::vector<float> samples = stereoSine(kTotal, 1000.0, 0.5f);
        std::vector<float> left(kTotal);
        std::vector<float> right(kTotal);
        for (std::size_t frame = 0; frame < kTotal; ++frame) {
            left[frame] = samples[frame * 2];
            right[frame] = samples[frame * 2 + 1];
        }
        tap->announceRate(48000);
        const std::array<const float *, 2> channels{left.data(), right.data()};
        if (!tap->write(channels, kTotal)) {
            return false;
        }
        writer->sync();
        return true;
    }));

    const std::filesystem::path path = folder / "killed.wav";
    // The RIFF size, which is what a player believes: still the empty
    // file's. (libsndfile is lenient about a data chunk that says it is
    // empty and reads to the end anyway; most players are not.)
    const auto riffSize = [&path] {
        std::ifstream file(path, std::ios::binary);
        std::array<unsigned char, 8> head{};
        file.read(reinterpret_cast<char *>(head.data()), head.size());
        return static_cast<std::uint64_t>(head[4]) | static_cast<std::uint64_t>(head[5]) << 8
            | static_cast<std::uint64_t>(head[6]) << 16 | static_cast<std::uint64_t>(head[7]) << 24;
    };
    EXPECT_LT(riffSize(), 1000U) << "the header was brought up to date after all";

    const std::vector<RepairReport> reports = repairOrphans(journal);
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_EQ(reports[0].file, path);
    ASSERT_TRUE(reports[0].result.has_value());
    EXPECT_TRUE(reports[0].result->changed);
    EXPECT_EQ(riffSize(), std::filesystem::file_size(path) - 8);
    const ReadBack repaired = readBack(path);
    EXPECT_EQ(repaired.headerFrames, static_cast<std::int64_t>(kTotal));
    EXPECT_EQ(repaired.samples, stereoSine(kTotal, 1000.0, 0.5f));
}

// **Past 4 GB a WAV has to become RF64**, and one cut off on the way there
// still has the plain header it started with. The file here is sparse: the
// disk holds its first bytes and a hole the size of four gigabytes.
TEST(Repair, AWavCutOffPastFourGigabytesBecomesRf64)
{
    ScratchFolder folder;
    const std::filesystem::path path = folder / "long.wav";
    FileSpec spec;
    spec.sampleFormat = SampleFormat::Int16;
    {
        std::expected<AudioFileWriter, FileError> writer = AudioFileWriter::create(path, spec);
        ASSERT_TRUE(writer.has_value());
        ASSERT_TRUE(writer->write(stereoSine(4800, 1000.0, 0.5f)).has_value());
        ASSERT_TRUE(writer->close().has_value());
    }
    const std::uint64_t dataAt = std::filesystem::file_size(path) - 4800 * 4;
    const std::uint64_t frames = 1'200'000'000; // 6.9 hours at 48 kHz, 4.8 GB
    std::error_code error;
    std::filesystem::resize_file(path, dataAt + frames * 4, error);
    if (error) {
        GTEST_SKIP() << "this file system cannot hold a sparse 4.8 GB file: " << error.message();
    }

    const std::expected<RepairResult, RepairError> result = repairFile(path);
    ASSERT_TRUE(result.has_value()) << repairErrorName(result.error());
    EXPECT_EQ(result->frames, frames);
    const ReadBack header = readBack(path, 4800);
    ASSERT_TRUE(header.opened);
    EXPECT_EQ(header.format & SF_FORMAT_TYPEMASK, SF_FORMAT_RF64);
    EXPECT_EQ(header.headerFrames, static_cast<std::int64_t>(frames));
    EXPECT_NEAR(rmsDb(header.samples, 2, 0, 0, 4800), -9.03, 0.05) << "the audio before the hole was lost";
}

TEST(Repair, AFileThatWasClosedProperlyIsLeftAsItIs)
{
    ScratchFolder folder;
    for (const FileFormat format : {FileFormat::Wav, FileFormat::Aiff, FileFormat::Flac, FileFormat::Vorbis}) {
        const std::filesystem::path path = folder / std::format("closed.{}", fileExtension(format));
        FileSpec spec;
        spec.format = format;
        {
            std::expected<AudioFileWriter, FileError> writer = AudioFileWriter::create(path, spec);
            ASSERT_TRUE(writer.has_value());
            ASSERT_TRUE(writer->write(stereoSine(10000, 1000.0, 0.5f)).has_value());
            ASSERT_TRUE(writer->close().has_value());
        }
        const auto before = std::filesystem::file_size(path);
        const std::expected<RepairResult, RepairError> result = repairFile(path);
        ASSERT_TRUE(result.has_value()) << path;
        EXPECT_FALSE(result->changed) << path;
        EXPECT_EQ(std::filesystem::file_size(path), before) << path;
    }
}

TEST(Repair, WhatIsStillBeingWrittenIsNotTouched)
{
    ScratchFolder folder;
    const std::filesystem::path path = folder / "live.wav";
    {
        std::ofstream file(path);
        file << "not really a WAV";
    }
    // An entry of this process's — a recording that is going on now — is
    // somebody else's business, however it looks.
    RecordingJournal journal(folder / "journal");
    ASSERT_TRUE(journal.add(path).has_value());
    EXPECT_TRUE(repairOrphans(folder / "journal").empty());

    // One of a process that has gone is repaired, and forgotten however
    // that went: a file that is not a recording is reported, not retried.
    {
        std::ofstream stale(folder / "journal/999999999-0-0.rec");
        stale << path.string();
    }
    const std::vector<RepairReport> reports = repairOrphans(folder / "journal");
    ASSERT_EQ(reports.size(), 1U);
    ASSERT_FALSE(reports[0].result.has_value());
    EXPECT_EQ(reports[0].result.error(), RepairError::NotRecognised);
    EXPECT_FALSE(std::filesystem::exists(folder / "journal/999999999-0-0.rec"));
}
