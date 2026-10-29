// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The writer thread: what a Recorder's tap is given ends up in a
// file, all of it and nothing else, and what happened is said.

#include "record/recordingwriter.h"
#include "record/repair.h"
#include "record/tags.h"
#include "recordfixtures.h"
#include "rt/audiotap.h"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

using namespace rb;
using namespace rb::record;
using namespace rb::recordtest;
using namespace std::chrono;

namespace
{

/// Everything the writer said, in order.
class Events
{
public:
    [[nodiscard]] RecordingWriter::EventSink sink()
    {
        return [this](const RecordingEvent &event) {
            const std::lock_guard lock(m_mutex);
            m_events.push_back(event);
        };
    }

    [[nodiscard]] std::vector<RecordingEvent> all()
    {
        const std::lock_guard lock(m_mutex);
        return m_events;
    }

    [[nodiscard]] std::vector<RecordingEvent> of(RecordingEvent::Kind kind)
    {
        std::vector<RecordingEvent> found;
        for (const RecordingEvent &event : all()) {
            if (event.kind == kind) {
                found.push_back(event);
            }
        }
        return found;
    }

private:
    std::mutex m_mutex;
    std::vector<RecordingEvent> m_events;
};

[[nodiscard]] WriterOptions testOptions()
{
    WriterOptions options;
    options.clock = [] { return local_days{year{2026} / September / 25} + hours{14} + minutes{3} + seconds{22}; };
    options.dither = false;
    // **A disk with room**, whatever the machine running the tests has, so
    // no test but the ones about room hears about it.
    options.freeSpace = [](const std::filesystem::path &) {
        return std::optional<std::uint64_t>{std::uint64_t{1} << 40};
    };
    return options;
}

[[nodiscard]] RecordingSettings settingsIn(const ScratchFolder &folder, std::string pattern = "%session %time")
{
    RecordingSettings settings;
    settings.format = FileFormat::Wav;
    settings.sampleFormat = SampleFormat::Float32;
    settings.folder = folder.path();
    settings.fileNamePattern = std::move(pattern);
    settings.sessionName = "Podcast";
    settings.blockName = "rec";
    return settings;
}

/// What a Recorder does each cycle: stereo, planar, into the tap.
void play(rt::AudioTap &tap, const std::vector<float> &interleaved, std::uint32_t rate = 48000)
{
    const std::size_t frames = interleaved.size() / 2;
    std::vector<float> left(frames);
    std::vector<float> right(frames);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        left[frame] = interleaved[frame * 2];
        right[frame] = interleaved[frame * 2 + 1];
    }
    tap.announceRate(rate);
    const std::array<const float *, 2> channels{left.data(), right.data()};
    ASSERT_TRUE(tap.write(channels, static_cast<std::uint32_t>(frames)));
}

} // namespace

TEST(RecordingWriter, WhatArrivesAfterStartIsTheFileAndNothingElse)
{
    ScratchFolder folder;
    Events events;
    auto tap = std::make_shared<rt::AudioTap>(2, 96000);
    {
        RecordingWriter writer(events.sink(), testOptions());
        writer.attach(7, tap);
        // Before anybody pressed anything: not in the file.
        play(*tap, stereoSine(1000, 200.0, 0.9f));
        writer.sync();
        writer.start(7, settingsIn(folder));
        const std::vector<float> take = stereoSine(48000, 1000.0, 0.5f);
        play(*tap, take);
        writer.finish(7);
        writer.sync();

        const std::vector<RecordingEvent> said = events.all();
        ASSERT_EQ(said.size(), 2U);
        EXPECT_EQ(said[0].kind, RecordingEvent::Kind::FileStarted);
        EXPECT_EQ(said[0].key, 7U);
        EXPECT_EQ(said[0].sampleRate, 48000U);
        EXPECT_EQ(said[1].kind, RecordingEvent::Kind::FileFinished);
        EXPECT_EQ(said[1].frames, 48000U);
        EXPECT_EQ(said[1].path, said[0].path);
        EXPECT_EQ(said[0].path, folder / "Podcast 14-03-22.wav");
        EXPECT_EQ(readBack(said[0].path).samples, take);
    }
}

TEST(RecordingWriter, StartingAndFinishingWithNothingBetweenMakesNoFile)
{
    ScratchFolder folder;
    Events events;
    auto tap = std::make_shared<rt::AudioTap>(2, 96000);
    RecordingWriter writer(events.sink(), testOptions());
    writer.attach(1, tap);
    writer.start(1, settingsIn(folder));
    writer.finish(1);
    writer.sync();
    EXPECT_TRUE(events.all().empty());
    EXPECT_TRUE(std::filesystem::is_empty(folder.path()));
}

TEST(RecordingWriter, AChangeOfRateStartsANewFileAtTheNewRate)
{
    ScratchFolder folder;
    Events events;
    auto tap = std::make_shared<rt::AudioTap>(2, 192000);
    RecordingWriter writer(events.sink(), testOptions());
    writer.attach(1, tap);
    writer.start(1, settingsIn(folder));
    play(*tap, stereoSine(24000, 1000.0, 0.5f), 48000);
    play(*tap, stereoSine(22050, 1000.0, 0.5f, 44100.0), 44100);
    writer.finish(1);
    writer.sync();

    const std::vector<RecordingEvent> finished = events.of(RecordingEvent::Kind::FileFinished);
    ASSERT_EQ(finished.size(), 2U);
    EXPECT_EQ(finished[0].frames, 24000U);
    EXPECT_EQ(finished[1].frames, 22050U);
    EXPECT_NE(finished[0].path, finished[1].path);
    EXPECT_EQ(readBack(finished[0].path).sampleRate, 48000);
    EXPECT_EQ(readBack(finished[1].path).sampleRate, 44100);
}

TEST(RecordingWriter, TwoRecordersOnTheSameAudioWriteTheSameSound)
{
    ScratchFolder folder;
    Events events;
    auto wav = std::make_shared<rt::AudioTap>(2, 96000);
    auto flac = std::make_shared<rt::AudioTap>(2, 96000);
    RecordingWriter writer(events.sink(), testOptions());
    writer.attach(1, wav);
    writer.attach(2, flac);
    RecordingSettings asWav = settingsIn(folder);
    asWav.sampleFormat = SampleFormat::Int24;
    RecordingSettings asFlac = asWav;
    asFlac.format = FileFormat::Flac;
    writer.start(1, asWav);
    writer.start(2, asFlac);
    const std::vector<float> take = stereoSine(30000, 1000.0, 0.5f);
    play(*wav, take);
    play(*flac, take);
    writer.finish(1);
    writer.finish(2);
    writer.sync();

    const std::vector<RecordingEvent> finished = events.of(RecordingEvent::Kind::FileFinished);
    ASSERT_EQ(finished.size(), 2U);
    EXPECT_EQ(finished[0].path.extension(), ".wav");
    EXPECT_EQ(finished[1].path.extension(), ".flac");
    EXPECT_EQ(readBack(finished[0].path).samples, readBack(finished[1].path).samples);
}

TEST(RecordingWriter, AudioTheDiskCouldNotTakeIsReportedAndTheRestKept)
{
    ScratchFolder folder;
    Events events;
    auto tap = std::make_shared<rt::AudioTap>(2, 1000);
    RecordingWriter writer(events.sink(), testOptions());
    writer.attach(1, tap);
    writer.start(1, settingsIn(folder));
    writer.sync();
    // Three cycles in a row with nobody emptying the tap: the first fits,
    // the next two do not.
    play(*tap, stereoSine(600, 1000.0, 0.5f));
    std::vector<float> left(600, 0.25f);
    const std::array<const float *, 2> channels{left.data(), left.data()};
    EXPECT_FALSE(tap->write(channels, 600));
    EXPECT_FALSE(tap->write(channels, 600));
    writer.finish(1);
    writer.sync();

    const std::vector<RecordingEvent> overflows = events.of(RecordingEvent::Kind::Overflow);
    ASSERT_EQ(overflows.size(), 1U);
    EXPECT_EQ(overflows[0].frames, 1200U);
    const std::vector<RecordingEvent> finished = events.of(RecordingEvent::Kind::FileFinished);
    ASSERT_EQ(finished.size(), 1U);
    EXPECT_EQ(finished[0].frames, 600U);
}

TEST(RecordingWriter, AFileThatCannotBeMadeIsSaidOnceAndNothingIsWrittenTillTheNextStart)
{
    ScratchFolder folder;
    {
        std::ofstream notAFolder(folder / "file");
    }
    Events events;
    auto tap = std::make_shared<rt::AudioTap>(2, 96000);
    RecordingWriter writer(events.sink(), testOptions());
    writer.attach(1, tap);
    RecordingSettings settings = settingsIn(folder);
    settings.folder = folder / "file/below";
    writer.start(1, settings);
    for (int cycle = 0; cycle < 5; ++cycle) {
        play(*tap, stereoSine(4800, 1000.0, 0.5f));
        writer.sync();
    }
    const std::vector<RecordingEvent> said = events.all();
    ASSERT_EQ(said.size(), 1U);
    EXPECT_EQ(said[0].kind, RecordingEvent::Kind::Failed);
    EXPECT_EQ(said[0].error, FileError::FolderNotCreated);

    // Somewhere that works, and a new start: recording again, with what
    // arrives from now on and nothing of what nobody could write before.
    writer.finish(1);
    writer.start(1, settingsIn(folder));
    play(*tap, stereoSine(4800, 1000.0, 0.5f));
    writer.finish(1);
    writer.sync();
    const std::vector<RecordingEvent> finished = events.of(RecordingEvent::Kind::FileFinished);
    ASSERT_EQ(finished.size(), 1U);
    EXPECT_EQ(finished[0].frames, 4800U);
}

TEST(RecordingWriter, TheJournalHoldsAFileForAsLongAsItIsBeingWritten)
{
    ScratchFolder folder;
    auto tap = std::make_shared<rt::AudioTap>(2, 96000);
    WriterOptions options = testOptions();
    options.journalFolder = folder / "journal";
    RecordingWriter writer(nullptr, options);
    writer.attach(1, tap);
    writer.start(1, settingsIn(folder));
    play(*tap, stereoSine(4800, 1000.0, 0.5f));
    writer.sync();
    const auto entries = [&] {
        std::vector<std::filesystem::path> found;
        for (const auto &entry : std::filesystem::directory_iterator(folder / "journal")) {
            found.push_back(entry.path());
        }
        return found;
    };
    ASSERT_EQ(entries().size(), 1U);
    writer.finish(1);
    writer.sync();
    EXPECT_TRUE(entries().empty());
}

// **Three hours without a lost sample**. Three hours of frames go
// through the tap and the writer thread into a file, faster than real time,
// in cycles of every length from 1 to 512, with the producer running on a
// thread of its own as the audio thread does. Every sample says which frame
// it is, so a frame lost, doubled or moved anywhere on the way is found.
//
// **At 8 kHz, mono and 16 bits**, so that the file is 173 MB rather than
// the 2 GB of 48 kHz stereo: the tap and the writer count frames and do not
// care what rate they are at, so three hours of them is three hours of
// them.
TEST(RecordingWriter, ThreeHoursOfAudioReachTheFileWithoutALostSample)
{
    constexpr std::uint32_t kRate = 8000;
    constexpr std::uint64_t kFrames = std::uint64_t{3} * 3600 * kRate;
    // The sample for a frame: a count through every 16-bit value, exactly.
    const auto sampleFor = [](std::uint64_t frame) {
        return static_cast<float>(static_cast<int>(frame % 65536) - 32768) / 32768.0f;
    };

    ScratchFolder folder;
    Events events;
    auto tap = std::make_shared<rt::AudioTap>(1, kRate * 10);
    const auto began = steady_clock::now();
    {
        // The writer looks every 20 ms, which empties ten seconds of tap fifty
        // times a second: five hundred times faster than real time, and the
        // most this test could go. Every millisecond lets it go at the speed
        // of the code instead.
        WriterOptions options = testOptions();
        options.pollInterval = milliseconds{1};
        RecordingWriter writer(events.sink(), options);
        writer.attach(1, tap);
        RecordingSettings settings = settingsIn(folder, "three hours");
        settings.sampleFormat = SampleFormat::Int16;
        writer.start(1, settings);
        writer.sync();

        std::thread audio([&] {
            std::mt19937 random(57);
            std::uniform_int_distribution<std::uint32_t> cycle(1, 512);
            std::vector<float> samples(512);
            std::uint64_t written = 0;
            tap->announceRate(kRate);
            while (written < kFrames) {
                const auto frames =
                    static_cast<std::uint32_t>(std::min<std::uint64_t>(cycle(random), kFrames - written));
                for (std::uint32_t frame = 0; frame < frames; ++frame) {
                    samples[frame] = sampleFor(written + frame);
                }
                // Held back only because this runs faster than real time; the
                // audio thread never is, and never waits.
                while (tap->capacityFrames() - tap->readableFrames() < frames) {
                    std::this_thread::sleep_for(microseconds{200});
                }
                const std::array<const float *, 1> channels{samples.data()};
                if (!tap->write(channels, frames)) {
                    return;
                }
                written += frames;
            }
        });
        audio.join();
        writer.finish(1);
        writer.sync();
    }
    const double seconds = duration<double>(steady_clock::now() - began).count();
    std::cout << "three hours recorded in " << seconds << " s\n";

    EXPECT_EQ(tap->droppedFrames(), 0U);
    const std::vector<RecordingEvent> finished = events.of(RecordingEvent::Kind::FileFinished);
    ASSERT_EQ(finished.size(), 1U);
    EXPECT_EQ(finished[0].frames, kFrames);

    // Read back a piece at a time: the whole of it as floats would be
    // 345 MB of memory for a test.
    SF_INFO info{};
    SNDFILE *file = sf_open(finished[0].path.c_str(), SFM_READ, &info);
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(info.frames, static_cast<sf_count_t>(kFrames));
    std::vector<float> piece(65536);
    std::uint64_t frame = 0;
    std::uint64_t firstWrong = kFrames;
    for (;;) {
        const sf_count_t got = sf_readf_float(file, piece.data(), static_cast<sf_count_t>(piece.size()));
        if (got <= 0) {
            break;
        }
        for (sf_count_t index = 0; index < got && firstWrong == kFrames; ++index, ++frame) {
            if (piece[static_cast<std::size_t>(index)] != sampleFor(frame)) {
                firstWrong = frame;
            }
        }
        if (firstWrong != kFrames) {
            break;
        }
    }
    sf_close(file);
    EXPECT_EQ(frame, kFrames);
    EXPECT_EQ(firstWrong, kFrames) << "frame " << firstWrong << " is not the one that was recorded there";
}

// **A command applies where it was asked for** (seen in the gate on the merged
// tree). start() and finish() are asked for on one thread and carried out on
// another, and a writer that was slow to wake used to take "now" to mean
// when it got round to them: it threw away the audio that had arrived in
// between, which is the beginning of the recording. Here the writer is held
// up on purpose — by a sink that sleeps — and the audio arrives before it
// can get to the start.
TEST(RecordingWriter, AStartThatIsCarriedOutLateStillRecordsFromWhereItWasAskedFor)
{
    ScratchFolder folder;
    Events events;
    auto tap = std::make_shared<rt::AudioTap>(2, 96000);
    std::atomic<bool> slow{false};
    RecordingWriter::EventSink sink = events.sink();
    RecordingWriter writer(
        [&slow, sink](const RecordingEvent &event) {
            sink(event);
            if (slow.load()) {
                std::this_thread::sleep_for(milliseconds{100});
            }
        },
        testOptions());
    writer.attach(1, tap);
    writer.start(1, settingsIn(folder, "first"));
    play(*tap, stereoSine(4800, 1000.0, 0.5f));
    writer.sync(); // the first file is open, so the sink has spoken
    // The next thing the writer says keeps it busy for a tenth of a second:
    // finishing this file, before it can get to the start of the next.
    slow.store(true);
    writer.finish(1);
    writer.start(1, settingsIn(folder, "second"));
    const std::vector<float> take = stereoSine(9600, 440.0, 0.5f);
    play(*tap, take);
    writer.finish(1);
    writer.sync();

    const std::vector<RecordingEvent> finished = events.of(RecordingEvent::Kind::FileFinished);
    ASSERT_EQ(finished.size(), 2U);
    EXPECT_EQ(finished[0].frames, 4800U);
    EXPECT_EQ(finished[1].frames, 9600U) << "the second recording lost its beginning";
    EXPECT_EQ(readBack(finished[1].path).samples, take);
}

namespace
{

/// The frequency of one channel of interleaved \a samples between two
/// frames, from how often it crosses zero going up.
[[nodiscard]] double frequencyOf(const std::vector<float> &samples, int rate, std::size_t from, std::size_t to)
{
    std::size_t first = 0;
    std::size_t last = 0;
    int crossings = 0;
    for (std::size_t frame = from + 1; frame < to; ++frame) {
        if (samples[(frame - 1) * 2] < 0.0f && samples[frame * 2] >= 0.0f) {
            if (crossings == 0) {
                first = frame;
            }
            last = frame;
            ++crossings;
        }
    }
    return crossings < 2 ? 0.0 : (crossings - 1) * static_cast<double>(rate) / static_cast<double>(last - first);
}

} // namespace

// **A file at another rate than the session's**, converted on the
// writer's thread: as long as what went in, at the new rate, with the tone
// at its frequency and its level.
TEST(RecordingWriter, AFileAtAnotherRateIsConvertedOnTheWritersThread)
{
    for (const auto &[from, to] : {std::pair{44100U, 48000U}, std::pair{48000U, 44100U}}) {
        SCOPED_TRACE(std::format("{} to {}", from, to));
        ScratchFolder folder;
        Events events;
        auto tap = std::make_shared<rt::AudioTap>(2, 192000);
        RecordingWriter writer(events.sink(), testOptions());
        writer.attach(1, tap);
        RecordingSettings settings = settingsIn(folder);
        settings.fileRate = to;
        writer.start(1, settings);
        // Two seconds, in the pieces a session would hand over.
        const std::vector<float> take = stereoSine(2 * from, 1000.0, 0.5f, from);
        for (std::size_t at = 0; at < take.size(); at += 2 * 1024) {
            play(*tap,
                 std::vector<float>(take.begin() + static_cast<std::ptrdiff_t>(at),
                                    take.begin() + static_cast<std::ptrdiff_t>(std::min(at + 2048, take.size()))),
                 from);
            writer.sync();
        }
        writer.finish(1);
        writer.sync();

        const std::vector<RecordingEvent> finished = events.of(RecordingEvent::Kind::FileFinished);
        ASSERT_EQ(finished.size(), 1U);
        EXPECT_EQ(finished[0].sampleRate, to);
        // Within a frame for every second converted.
        EXPECT_NEAR(static_cast<double>(finished[0].frames), 2.0 * to, 2.0);
        const ReadBack file = readBack(finished[0].path);
        EXPECT_EQ(file.sampleRate, static_cast<int>(to));
        EXPECT_EQ(file.frames(), finished[0].frames);
        const std::size_t middleFrom = to / 4;
        const std::size_t middleTo = file.frames() - to / 4;
        EXPECT_NEAR(rmsDb(file.samples, 2, 0, middleFrom, middleTo), -9.03, 0.1);
        EXPECT_NEAR(frequencyOf(file.samples, static_cast<int>(to), middleFrom, middleTo), 1000.0, 1.0);
        std::cout << from << " to " << to << ": " << finished[0].frames << " frames\n";
    }
}

TEST(RecordingWriter, AFileAtARateOfItsOwnKeepsItWhenTheSessionsChanges)
{
    ScratchFolder folder;
    Events events;
    auto tap = std::make_shared<rt::AudioTap>(2, 192000);
    RecordingWriter writer(events.sink(), testOptions());
    writer.attach(1, tap);
    RecordingSettings settings = settingsIn(folder);
    settings.fileRate = 48000;
    writer.start(1, settings);
    play(*tap, stereoSine(48000, 1000.0, 0.5f, 48000.0), 48000);
    play(*tap, stereoSine(44100, 1000.0, 0.5f, 44100.0), 44100);
    writer.finish(1);
    writer.sync();

    // One file, not two: a second at each rate, both at 48 kHz.
    const std::vector<RecordingEvent> finished = events.of(RecordingEvent::Kind::FileFinished);
    ASSERT_EQ(finished.size(), 1U);
    EXPECT_EQ(finished[0].sampleRate, 48000U);
    EXPECT_NEAR(static_cast<double>(finished[0].frames), 96000.0, 2.0);
}

// Opus is made at 48 kHz and the rates that divide it, and a session at
// 44.1 kHz is the commonest there is.
TEST(RecordingWriter, AnOpusFileInASessionAt44kIsMadeAt48k)
{
    ScratchFolder folder;
    Events events;
    auto tap = std::make_shared<rt::AudioTap>(2, 192000);
    RecordingWriter writer(events.sink(), testOptions());
    writer.attach(1, tap);
    RecordingSettings settings = settingsIn(folder);
    settings.format = FileFormat::Opus;
    writer.start(1, settings);
    play(*tap, stereoSine(44100, 1000.0, 0.5f, 44100.0), 44100);
    writer.finish(1);
    writer.sync();

    EXPECT_TRUE(events.of(RecordingEvent::Kind::Failed).empty());
    const std::vector<RecordingEvent> finished = events.of(RecordingEvent::Kind::FileFinished);
    ASSERT_EQ(finished.size(), 1U);
    EXPECT_EQ(finished[0].sampleRate, 48000U);
    const ReadBack file = readBack(finished[0].path);
    EXPECT_EQ(file.sampleRate, 48000);
    EXPECT_NEAR(static_cast<double>(file.frames()), 48000.0, 3000.0);
    EXPECT_NEAR(rmsDb(file.samples, 2, 0, 12000, 36000), -9.03, 1.0);
}

TEST(RecordingWriter, ADiskGettingFullIsSaidOnceAndTheRecordingGoesOn)
{
    ScratchFolder folder;
    Events events;
    WriterOptions options = testOptions();
    options.spaceInterval = milliseconds{0};
    options.freeSpace = [](const std::filesystem::path &) { return std::optional<std::uint64_t>{500U << 20}; };
    auto tap = std::make_shared<rt::AudioTap>(2, 96000);
    RecordingWriter writer(events.sink(), options);
    writer.attach(1, tap);
    writer.start(1, settingsIn(folder));
    for (int second = 0; second < 3; ++second) {
        play(*tap, stereoSine(48000, 1000.0, 0.5f));
        writer.sync();
    }
    writer.finish(1);
    writer.sync();

    const std::vector<RecordingEvent> low = events.of(RecordingEvent::Kind::LowDiskSpace);
    ASSERT_EQ(low.size(), 1U);
    EXPECT_EQ(low[0].bytesFree, 500U << 20);
    EXPECT_TRUE(events.of(RecordingEvent::Kind::Failed).empty());
    const std::vector<RecordingEvent> finished = events.of(RecordingEvent::Kind::FileFinished);
    ASSERT_EQ(finished.size(), 1U);
    EXPECT_EQ(finished[0].frames, 3U * 48000U);
}

// **Stopped while it still can be**: closed properly, with everything
// before it kept, rather than cut off by a write that did not fit.
TEST(RecordingWriter, ADiskRunningOutFinishesTheFileCleanlyAndSaysWhy)
{
    ScratchFolder folder;
    Events events;
    auto free = std::make_shared<std::atomic<std::uint64_t>>(std::uint64_t{10} << 30);
    WriterOptions options = testOptions();
    options.spaceInterval = milliseconds{0};
    options.freeSpace = [free](const std::filesystem::path &) { return std::optional<std::uint64_t>{free->load()}; };
    auto tap = std::make_shared<rt::AudioTap>(2, 96000);
    RecordingWriter writer(events.sink(), options);
    writer.attach(1, tap);
    writer.start(1, settingsIn(folder));
    play(*tap, stereoSine(48000, 1000.0, 0.5f));
    writer.sync();
    // The second second arrives, and then the disk is nearly full: whether
    // the writer has written that second before it looks or after, it
    // looks after, and the file ends with it.
    play(*tap, stereoSine(48000, 1000.0, 0.5f));
    free->store(std::uint64_t{10} << 20);
    writer.sync();
    // Nothing more goes anywhere until somebody starts it again.
    play(*tap, stereoSine(48000, 1000.0, 0.5f));
    writer.sync();

    const std::vector<RecordingEvent> said = events.all();
    ASSERT_EQ(said.size(), 3U);
    EXPECT_EQ(said[0].kind, RecordingEvent::Kind::FileStarted);
    EXPECT_EQ(said[1].kind, RecordingEvent::Kind::Failed);
    EXPECT_EQ(said[1].error, FileError::DiskFull);
    EXPECT_EQ(said[2].kind, RecordingEvent::Kind::FileFinished);
    EXPECT_EQ(said[2].frames, 2U * 48000U);
    // Closed properly: its header already says all of it.
    const std::expected<RepairResult, RepairError> checked = repairFile(said[2].path);
    ASSERT_TRUE(checked.has_value());
    EXPECT_FALSE(checked->changed);
    EXPECT_EQ(readBack(said[2].path).frames(), 2U * 48000U);
}

TEST(RecordingWriter, NoFileIsStartedOnADiskThatIsAsGoodAsFull)
{
    ScratchFolder folder;
    Events events;
    WriterOptions options = testOptions();
    options.freeSpace = [](const std::filesystem::path &) { return std::optional<std::uint64_t>{10U << 20}; };
    auto tap = std::make_shared<rt::AudioTap>(2, 96000);
    RecordingWriter writer(events.sink(), options);
    writer.attach(1, tap);
    writer.start(1, settingsIn(folder));
    play(*tap, stereoSine(4800, 1000.0, 0.5f));
    writer.sync();

    const std::vector<RecordingEvent> said = events.all();
    ASSERT_EQ(said.size(), 1U);
    EXPECT_EQ(said[0].kind, RecordingEvent::Kind::Failed);
    EXPECT_EQ(said[0].error, FileError::DiskFull);
    EXPECT_TRUE(std::filesystem::is_empty(folder.path()));
}

// ---------------------------------------------------------------- limits

namespace
{

/// Stereo frames that say which frame they are, exactly, from \a first on:
/// a count on the left, its negative on the right.
[[nodiscard]] std::vector<float> counted(std::uint64_t first, std::size_t frames)
{
    std::vector<float> samples(frames * 2);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const float value = static_cast<float>((first + frame) % 1000000 + 1) / 1048576.0f;
        samples[frame * 2] = value;
        samples[frame * 2 + 1] = -value;
    }
    return samples;
}

/// Plays \a samples in cycles of every length from 1 to 512, the way a
/// session hands them over, with the writer draining in between.
void playInCycles(rt::AudioTap &tap, RecordingWriter &writer, const std::vector<float> &samples, std::uint32_t rate)
{
    std::mt19937 random(59);
    std::uniform_int_distribution<std::size_t> cycle(1, 512);
    std::size_t at = 0;
    while (at < samples.size()) {
        const std::size_t frames = std::min(cycle(random), (samples.size() - at) / 2);
        play(tap,
             std::vector<float>(samples.begin() + static_cast<std::ptrdiff_t>(at),
                                samples.begin() + static_cast<std::ptrdiff_t>(at + frames * 2)),
             rate);
        at += frames * 2;
        if (tap.readableFrames() > tap.capacityFrames() / 2) {
            writer.sync();
        }
    }
}

/// Every file's samples, one after another, in the order they finished.
[[nodiscard]] std::vector<float> joined(const std::vector<RecordingEvent> &finished)
{
    std::vector<float> all;
    for (const RecordingEvent &file : finished) {
        const std::vector<float> samples = readBack(file.path).samples;
        all.insert(all.end(), samples.begin(), samples.end());
    }
    return all;
}

/// A second of tone at \a db, stereo, at 48 kHz.
[[nodiscard]] std::vector<float> toneAt(double seconds, double db)
{
    return stereoSine(static_cast<std::size_t>(seconds * 48000), 1000.0, static_cast<float>(std::pow(10.0, db / 20.0)));
}

[[nodiscard]] std::vector<float> operator+(std::vector<float> first, const std::vector<float> &second)
{
    first.insert(first.end(), second.begin(), second.end());
    return first;
}

} // namespace

// **Split every so often: every sample in exactly one file**. Five
// and a half seconds at 8 kHz, split every second, in cycles of every
// length: six files, five of exactly 8000 frames and the last 4000, and
// the six together are the recording, sample for sample.
TEST(RecordingWriter, FilesSplitByTimeHoldEverySampleExactlyOnce)
{
    ScratchFolder folder;
    Events events;
    auto tap = std::make_shared<rt::AudioTap>(2, 80000);
    RecordingWriter writer(events.sink(), testOptions());
    writer.attach(1, tap);
    RecordingSettings settings = settingsIn(folder, "%session %counter");
    settings.limits.splitEverySeconds = 1.0;
    writer.start(1, settings);
    const std::vector<float> take = counted(0, 44000);
    playInCycles(*tap, writer, take, 8000);
    writer.finish(1);
    writer.sync();

    const std::vector<RecordingEvent> finished = events.of(RecordingEvent::Kind::FileFinished);
    ASSERT_EQ(finished.size(), 6U);
    for (std::size_t file = 0; file < 5; ++file) {
        EXPECT_EQ(finished[file].frames, 8000U);
        EXPECT_EQ(finished[file].path.filename(), std::format("Podcast {:03}.wav", file + 1));
    }
    EXPECT_EQ(finished[5].frames, 4000U);
    const std::vector<float> all = joined(finished);
    EXPECT_EQ(all.size(), take.size());
    EXPECT_EQ(all, take) << "a sample is missing, doubled or out of place across the files";
    double sum = 0.0;
    for (const float sample : all) {
        sum += std::fabs(sample);
    }
    double expected = 0.0;
    for (const float sample : take) {
        expected += std::fabs(sample);
    }
    EXPECT_EQ(sum, expected);
}

TEST(RecordingWriter, FilesSplitBySizeEndNearTheSizeAndHoldEverySampleOnce)
{
    ScratchFolder folder;
    Events events;
    auto tap = std::make_shared<rt::AudioTap>(2, 96000);
    RecordingWriter writer(events.sink(), testOptions());
    writer.attach(1, tap);
    RecordingSettings settings = settingsIn(folder, "%counter");
    settings.limits.splitEveryBytes = 200 * 1024;
    writer.start(1, settings);
    const std::vector<float> take = counted(0, 96000); // 750 KB of float stereo
    playInCycles(*tap, writer, take, 48000);
    writer.finish(1);
    writer.sync();

    const std::vector<RecordingEvent> finished = events.of(RecordingEvent::Kind::FileFinished);
    ASSERT_EQ(finished.size(), 4U);
    for (std::size_t file = 0; file + 1 < finished.size(); ++file) {
        const auto size = std::filesystem::file_size(finished[file].path);
        // Looked at after each piece written, and a piece is at most 4096
        // frames of 8 bytes.
        EXPECT_GE(size, 200U * 1024U);
        EXPECT_LT(size, 200U * 1024U + 4096U * 8U + 1024U);
    }
    EXPECT_EQ(joined(finished), take);
}

TEST(RecordingWriter, ARecordingEndsByItselfOnTheFrameItWasSetTo)
{
    ScratchFolder folder;
    Events events;
    auto tap = std::make_shared<rt::AudioTap>(2, 192000);
    RecordingWriter writer(events.sink(), testOptions());
    writer.attach(1, tap);
    RecordingSettings settings = settingsIn(folder);
    settings.limits.endAfterSeconds = 1.0;
    writer.start(1, settings);
    const std::vector<float> take = counted(0, 96000);
    playInCycles(*tap, writer, take, 48000);
    writer.sync();
    playInCycles(*tap, writer, counted(96000, 4800), 48000); // after the end: nowhere
    writer.finish(1);
    writer.sync();

    const std::vector<RecordingEvent> stopped = events.of(RecordingEvent::Kind::Stopped);
    ASSERT_EQ(stopped.size(), 1U);
    EXPECT_EQ(stopped[0].stopReason, RecordingEvent::StopReason::TimeLimit);
    const std::vector<RecordingEvent> finished = events.of(RecordingEvent::Kind::FileFinished);
    ASSERT_EQ(finished.size(), 1U);
    EXPECT_EQ(finished[0].frames, 48000U);
    EXPECT_EQ(readBack(finished[0].path).samples, counted(0, 48000));
}

// **The silence rules, at their threshold and after their time**:
// a second of tone, three seconds under the threshold, a second of tone,
// with two seconds set. Counted in frames, so "±50 ms" is exact here.
TEST(RecordingWriter, SilenceIsRemovedSplitOnOrStoppedAtAfterItsTimeBelowItsThreshold)
{
    // -65 dB is under a -60 dB threshold; the tone is well over it.
    // The last second starts a frame into its sine: a sine's first sample
    // is zero, and zero is silence however loud the rest of it is.
    const std::vector<float> loud = toneAt(1.0 + 1.0 / 48000.0, -6.0);
    const std::vector<float> take =
        toneAt(1.0, -6.0) + toneAt(3.0, -65.0) + std::vector<float>(loud.begin() + 2, loud.end());
    struct Case {
        FileLimits::Silence rule;
        std::vector<std::uint64_t> files;
        bool stops;
    };
    for (const Case &rule : {Case{FileLimits::Silence::Remove, {192000}, false},
                             Case{FileLimits::Silence::Split, {144000, 48000}, false},
                             Case{FileLimits::Silence::Stop, {144000}, true}}) {
        SCOPED_TRACE(static_cast<int>(rule.rule));
        ScratchFolder folder;
        Events events;
        auto tap = std::make_shared<rt::AudioTap>(2, 96000);
        RecordingWriter writer(events.sink(), testOptions());
        writer.attach(1, tap);
        RecordingSettings settings = settingsIn(folder, "%counter");
        settings.limits.silence = rule.rule;
        settings.limits.silenceSeconds = 2.0;
        settings.limits.silenceThresholdDb = -60.0f;
        writer.start(1, settings);
        playInCycles(*tap, writer, take, 48000);
        writer.finish(1);
        writer.sync();

        const std::vector<RecordingEvent> finished = events.of(RecordingEvent::Kind::FileFinished);
        ASSERT_EQ(finished.size(), rule.files.size());
        for (std::size_t file = 0; file < finished.size(); ++file) {
            EXPECT_EQ(finished[file].frames, rule.files[file]) << "file " << file;
        }
        const std::vector<RecordingEvent> stopped = events.of(RecordingEvent::Kind::Stopped);
        EXPECT_EQ(stopped.size(), rule.stops ? 1U : 0U);
        if (rule.stops) {
            EXPECT_EQ(stopped[0].stopReason, RecordingEvent::StopReason::Silence);
        }
        // What was kept is the recording's own frames, in order: the first
        // three seconds (tone and two of the silence) and, where the rule
        // lets it, the last second.
        const std::vector<float> all = joined(finished);
        const std::vector<float> kept = std::vector<float>(take.begin(), take.begin() + 144000 * 2)
            + (rule.stops ? std::vector<float>{} : std::vector<float>(take.end() - 48000 * 2, take.end()));
        EXPECT_EQ(all, kept);
    }
}

TEST(RecordingWriter, SomethingQuietButOverTheThresholdIsNotSilence)
{
    ScratchFolder folder;
    Events events;
    auto tap = std::make_shared<rt::AudioTap>(2, 96000);
    RecordingWriter writer(events.sink(), testOptions());
    writer.attach(1, tap);
    RecordingSettings settings = settingsIn(folder);
    settings.limits.silence = FileLimits::Silence::Remove;
    settings.limits.silenceSeconds = 2.0;
    settings.limits.silenceThresholdDb = -60.0f;
    writer.start(1, settings);
    playInCycles(*tap, writer, toneAt(1.0, -6.0) + toneAt(3.0, -55.0) + toneAt(1.0, -6.0), 48000);
    writer.finish(1);
    writer.sync();
    const std::vector<RecordingEvent> finished = events.of(RecordingEvent::Kind::FileFinished);
    ASSERT_EQ(finished.size(), 1U);
    EXPECT_EQ(finished[0].frames, 240000U);
}

// **Pause and resume leave no gap and nothing twice**, and split
// ends one file on the frame before the next begins — each at the frame
// the tap had got to when it was asked for, with nothing synchronised in
// between.
TEST(RecordingWriter, PauseResumeAndSplitApplyAtTheExactFrameTheyWereAskedAt)
{
    ScratchFolder folder;
    Events events;
    auto tap = std::make_shared<rt::AudioTap>(2, 96000);
    RecordingWriter writer(events.sink(), testOptions());
    writer.attach(1, tap);
    writer.start(1, settingsIn(folder, "%counter"));
    play(*tap, counted(0, 1000));
    writer.pause(1);
    play(*tap, counted(1000, 500)); // while paused: in no file
    writer.resume(1);
    play(*tap, counted(1500, 700));
    writer.split(1);
    play(*tap, counted(2200, 300));
    writer.finish(1);
    writer.sync();

    const std::vector<RecordingEvent> finished = events.of(RecordingEvent::Kind::FileFinished);
    ASSERT_EQ(finished.size(), 2U);
    EXPECT_EQ(finished[0].path.filename(), "001.wav");
    EXPECT_EQ(finished[1].path.filename(), "002.wav");
    EXPECT_EQ(readBack(finished[0].path).samples, counted(0, 1000) + counted(1500, 700));
    EXPECT_EQ(readBack(finished[1].path).samples, counted(2200, 300));
    EXPECT_EQ(events.of(RecordingEvent::Kind::Paused).size(), 1U);
    EXPECT_EQ(events.of(RecordingEvent::Kind::Resumed).size(), 1U);
}

TEST(RecordingWriter, AnAiffSplitsItselfBeforeItsHeaderCannotSaySoAndTheOthersDoNot)
{
    EXPECT_EQ(safetyLimits(FileFormat::Aiff).splitEveryBytes, std::uint64_t{2} << 30);
    EXPECT_EQ(safetyLimits(FileFormat::Aiff).splitEverySeconds, 24.0 * 3600.0);
    for (const FileFormat format :
         {FileFormat::Wav, FileFormat::Flac, FileFormat::Mp3, FileFormat::Opus, FileFormat::Vorbis}) {
        EXPECT_EQ(safetyLimits(format).splitEveryBytes, 0U);
        EXPECT_EQ(safetyLimits(format).splitEverySeconds, 0.0);
    }
}

// **Each file gets its tags when it is finished**, tokens filled in
// with its own counter: a recording split in two is tracks 1 and 2.
TEST(RecordingWriter, EveryFileIsTaggedWhenItIsFinishedWithItsOwnCounter)
{
    ScratchFolder folder;
    Events events;
    auto tap = std::make_shared<rt::AudioTap>(2, 96000);
    RecordingWriter writer(events.sink(), testOptions());
    writer.attach(1, tap);
    RecordingSettings settings = settingsIn(folder, "%counter");
    settings.format = FileFormat::Flac;
    settings.tags.title = "%session, part %counter";
    settings.tags.artist = "Анна";
    settings.tags.track = "%counter";
    writer.start(1, settings);
    play(*tap, stereoSine(4800, 1000.0, 0.5f));
    writer.split(1);
    play(*tap, stereoSine(4800, 1000.0, 0.5f));
    writer.finish(1);
    writer.sync();

    EXPECT_TRUE(events.of(RecordingEvent::Kind::TagsNotWritten).empty());
    const std::vector<RecordingEvent> finished = events.of(RecordingEvent::Kind::FileFinished);
    ASSERT_EQ(finished.size(), 2U);
    for (std::size_t file = 0; file < 2; ++file) {
        const std::optional<Tags> tags = readTags(finished[file].path, FileFormat::Flac);
        ASSERT_TRUE(tags.has_value());
        EXPECT_EQ(tags->title, std::format("Podcast, part {:03}", file + 1));
        EXPECT_EQ(tags->artist, "Анна");
        EXPECT_EQ(tags->track, std::format("{:03}", file + 1));
    }
}

// **Mono is the two sides mixed**: half of each, so a sound on one
// side alone is kept at half its level rather than lost.
TEST(RecordingWriter, AMonoRecordingIsTheTwoSidesMixed)
{
    ScratchFolder folder;
    Events events;
    auto tap = std::make_shared<rt::AudioTap>(2, 96000);
    RecordingWriter writer(events.sink(), testOptions());
    writer.attach(1, tap);
    RecordingSettings settings = settingsIn(folder);
    settings.mono = true;
    writer.start(1, settings);
    std::vector<float> sides(2 * 4800);
    for (std::size_t frame = 0; frame < 4800; ++frame) {
        sides[frame * 2] = 0.5f;
        sides[frame * 2 + 1] = 0.25f;
    }
    play(*tap, sides);
    writer.finish(1);
    writer.sync();
    const std::vector<RecordingEvent> finished = events.of(RecordingEvent::Kind::FileFinished);
    ASSERT_EQ(finished.size(), 1U);
    const ReadBack file = readBack(finished[0].path);
    EXPECT_EQ(file.channels, 1);
    ASSERT_EQ(file.frames(), 4800U);
    EXPECT_EQ(file.samples.front(), 0.375f);
    EXPECT_EQ(file.samples.back(), 0.375f);
}
