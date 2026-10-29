// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The recordings index: a file appears as it starts, is filled in from
// the file itself when it finishes, goes when the file goes, and a row a
// crash left in progress is finished on the next rescan. Every index here
// is a file of its own in a folder of its own.

#include "core/mockengine.h"
#include "core/recordinglibrary.h"
#include "record/audiofile.h"
#include "record/tags.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

using namespace rb;
using namespace rb::core;
using namespace Qt::StringLiterals;

namespace
{

/// **Qt finds its SQL drivers as plugins, and only once there is an
/// application.** The engine always has one; a bare test has to make it.
void needApplication()
{
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char name[] = "reverblabr-engine-test";
        static char *argv[] = {name, nullptr};
        // Never destroyed. A static application would be destroyed at
        // exit after Qt's own globals, and Qt 6.10's SQL module then
        // crashes on its destroyed() signal.
        [[maybe_unused]] static auto *application = new QCoreApplication(argc, argv);
    }
}

/// A second of stereo at 48 kHz: silence, then a tone at half scale.
void writeTake(const QString &path, double seconds = 1.0)
{
    record::FileSpec spec;
    spec.dither = false;
    auto writer = record::AudioFileWriter::create(std::filesystem::path{path.toStdString()}, spec);
    ASSERT_TRUE(writer.has_value());
    const auto frames = static_cast<std::size_t>(seconds * 48000.0);
    std::vector<float> samples(frames * 2, 0.0f);
    for (std::size_t frame = frames / 2; frame < frames; ++frame) {
        const auto value = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 440.0 * frame / 48000.0));
        samples[frame * 2] = value;
        samples[frame * 2 + 1] = value;
    }
    ASSERT_TRUE(writer->write(samples).has_value());
    ASSERT_TRUE(writer->close().has_value());
}

[[nodiscard]] QJsonObject only(const QJsonArray &rows)
{
    EXPECT_EQ(rows.size(), 1);
    return rows.isEmpty() ? QJsonObject{} : rows.first().toObject();
}

} // namespace

TEST(RecordingLibrary, AFileIsListedAsItStartsAndFilledInWhenItFinishes)
{
    needApplication();
    QTemporaryDir folder;
    RecordingLibrary library(folder.filePath(u"library.db"_s));
    ASSERT_TRUE(library.isOpen());
    QSignalSpy changed(&library, &RecordingLibrary::changed);

    const QString path = folder.filePath(u"Session 2026-09-25 10-00-00.wav"_s);
    library.fileStarted(u"s1"_s, u"rec"_s, path);
    ASSERT_EQ(changed.size(), 1);
    EXPECT_EQ(changed.first().first().toString(), u"s1"_s);
    QJsonObject row = only(library.recordings(u"s1"_s));
    EXPECT_TRUE(row.value(u"inProgress"_s).toBool());
    EXPECT_EQ(row.value(u"name"_s).toString(), u"Session 2026-09-25 10-00-00.wav"_s);
    EXPECT_EQ(row.value(u"format"_s).toString(), u"wav"_s);
    EXPECT_FALSE(row.value(u"started"_s).toString().isEmpty());
    EXPECT_TRUE(library.recordings(u"s2"_s).isEmpty()) << "another session's list has this session's file";

    writeTake(path, 2.0);
    library.fileFinished(u"s1"_s, u"rec"_s, path, 96000, 48000);
    library.waitForIdle();
    ASSERT_EQ(changed.size(), 2) << "finishing the file said nothing";
    row = only(library.recordings(u"s1"_s));
    EXPECT_FALSE(row.value(u"inProgress"_s).toBool());
    EXPECT_DOUBLE_EQ(row.value(u"seconds"_s).toDouble(), 2.0);
    EXPECT_EQ(row.value(u"sampleRate"_s).toInt(), 48000);
    EXPECT_EQ(row.value(u"channels"_s).toInt(), 2);
    EXPECT_EQ(row.value(u"sizeBytes"_s).toDouble(), static_cast<double>(QFile(path).size()));
    const QJsonArray peaks = row.value(u"peaks"_s).toArray();
    ASSERT_EQ(peaks.size(), RecordingLibrary::kListPeakCount);
    EXPECT_EQ(peaks.first().toDouble(), 0.0) << "the silent half has a waveform";
    EXPECT_NEAR(peaks.last().toDouble(), 0.5, 0.01);
}

TEST(RecordingLibrary, ItKeepsWhatItWasToldAcrossARestart)
{
    needApplication();
    QTemporaryDir folder;
    const QString path = folder.filePath(u"take.wav"_s);
    writeTake(path);
    {
        RecordingLibrary library(folder.filePath(u"library.db"_s));
        library.fileStarted(u"s1"_s, u"rec"_s, path);
        library.fileFinished(u"s1"_s, u"rec"_s, path);
        library.waitForIdle();
    }
    RecordingLibrary again(folder.filePath(u"library.db"_s));
    const QJsonObject row = only(again.recordings(u"s1"_s));
    EXPECT_DOUBLE_EQ(row.value(u"seconds"_s).toDouble(), 1.0);
    EXPECT_FALSE(row.value(u"inProgress"_s).toBool());
}

TEST(RecordingLibrary, AFileDeletedOutsideGoesOnTheNextRescan)
{
    needApplication();
    QTemporaryDir folder;
    RecordingLibrary library(folder.filePath(u"library.db"_s));
    const QString kept = folder.filePath(u"kept.wav"_s);
    const QString deleted = folder.filePath(u"deleted.wav"_s);
    for (const QString &path : {kept, deleted}) {
        writeTake(path);
        library.fileStarted(u"s1"_s, u"rec"_s, path);
        library.fileFinished(u"s1"_s, u"rec"_s, path);
    }
    library.waitForIdle();
    ASSERT_EQ(library.recordings(u"s1"_s).size(), 2);

    ASSERT_TRUE(QFile::remove(deleted));
    QSignalSpy changed(&library, &RecordingLibrary::changed);
    library.rescan();
    library.waitForIdle();
    EXPECT_EQ(only(library.recordings(u"s1"_s)).value(u"path"_s).toString(), kept);
    ASSERT_GE(changed.size(), 1);
    EXPECT_EQ(changed.first().first().toString(), u"s1"_s);
}

TEST(RecordingLibrary, ARowACrashLeftInProgressIsFinishedByTheNextRescan)
{
    needApplication();
    QTemporaryDir folder;
    const QString path = folder.filePath(u"crashed.wav"_s);
    {
        RecordingLibrary library(folder.filePath(u"library.db"_s));
        library.fileStarted(u"s1"_s, u"rec"_s, path);
        writeTake(path);
        // …and the process dies here: no fileFinished().
    }
    RecordingLibrary library(folder.filePath(u"library.db"_s));
    EXPECT_TRUE(only(library.recordings(u"s1"_s)).value(u"inProgress"_s).toBool());
    library.rescan();
    library.waitForIdle();
    const QJsonObject row = only(library.recordings(u"s1"_s));
    EXPECT_FALSE(row.value(u"inProgress"_s).toBool());
    EXPECT_DOUBLE_EQ(row.value(u"seconds"_s).toDouble(), 1.0);
}

TEST(RecordingLibrary, AFileStillBeingWrittenIsLeftAloneByARescan)
{
    needApplication();
    QTemporaryDir folder;
    RecordingLibrary library(folder.filePath(u"library.db"_s));
    const QString path = folder.filePath(u"live.wav"_s);
    writeTake(path);
    library.fileStarted(u"s1"_s, u"rec"_s, path);
    library.rescan();
    library.waitForIdle();
    EXPECT_TRUE(only(library.recordings(u"s1"_s)).value(u"inProgress"_s).toBool());
}

TEST(RecordingLibrary, AFileItCannotReadKeepsWhatTheRecorderCounted)
{
    // M4A is written through FFmpeg, which libsndfile does not read.
    needApplication();
    QTemporaryDir folder;
    RecordingLibrary library(folder.filePath(u"library.db"_s));
    const QString path = folder.filePath(u"take.m4a"_s);
    {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("not something libsndfile reads");
    }
    library.fileStarted(u"s1"_s, u"rec"_s, path);
    library.fileFinished(u"s1"_s, u"rec"_s, path, 144000, 48000);
    library.waitForIdle();
    const QJsonObject row = only(library.recordings(u"s1"_s));
    EXPECT_DOUBLE_EQ(row.value(u"seconds"_s).toDouble(), 3.0);
    EXPECT_EQ(row.value(u"format"_s).toString(), u"m4a"_s);
    EXPECT_TRUE(row.value(u"peaks"_s).toArray().isEmpty());
}

TEST(RecordingLibrary, NewestFirst)
{
    needApplication();
    QTemporaryDir folder;
    RecordingLibrary library(folder.filePath(u"library.db"_s));
    library.fileStarted(u"s1"_s, u"rec"_s, folder.filePath(u"first.wav"_s));
    QThread::msleep(5);
    library.fileStarted(u"s1"_s, u"rec"_s, folder.filePath(u"second.wav"_s));
    const QJsonArray rows = library.recordings(u"s1"_s);
    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(rows.at(0).toObject().value(u"name"_s).toString(), u"second.wav"_s);
}

TEST(RecordingLibrary, AnIndexThatCannotBeOpenedIsAnEmptyListAndNothingWorse)
{
    needApplication();
    QTemporaryDir folder;
    // A folder where the file should be.
    ASSERT_TRUE(QDir(folder.path()).mkdir(u"library.db"_s));
    RecordingLibrary library(folder.filePath(u"library.db"_s));
    EXPECT_FALSE(library.isOpen());
    library.fileStarted(u"s1"_s, u"rec"_s, folder.filePath(u"x.wav"_s));
    library.fileFinished(u"s1"_s, u"rec"_s, folder.filePath(u"x.wav"_s));
    library.rescan();
    library.waitForIdle();
    EXPECT_TRUE(library.recordings(u"s1"_s).isEmpty());
}

TEST(RecordingLibrary, TheMockKeepsAnIndexOfItsOwnAndAnswersAsTheEngineDoes)
{
    needApplication();
    MockEngine mock;
    QSignalSpy changed(&mock, &EngineFacade::recordingsChanged);
    const QString path = mock.addFakeRecording(u"s1"_s, u"Talk.wav"_s, 1.5);
    ASSERT_FALSE(path.isEmpty());
    EXPECT_TRUE(path.startsWith(mock.fakeRecordingFolder())) << "a fake recording outside the mock's own folder";
    mock.waitForFakeRecordings();
    ASSERT_GE(changed.size(), 2) << "started and finished";

    mock.listRecordings(u"s1"_s);
    const QJsonArray rows = QJsonDocument::fromJson(changed.last().at(1).toString().toUtf8()).array();
    const QJsonObject row = only(rows);
    EXPECT_EQ(row.value(u"name"_s).toString(), u"Talk.wav"_s);
    EXPECT_DOUBLE_EQ(row.value(u"seconds"_s).toDouble(), 1.5);
    EXPECT_EQ(row.value(u"peaks"_s).toArray().size(), RecordingLibrary::kListPeakCount);

    ASSERT_TRUE(QFile::remove(path));
    mock.rescanRecordings();
    mock.listRecordings(u"s1"_s);
    EXPECT_TRUE(QJsonDocument::fromJson(changed.last().at(1).toString().toUtf8()).array().isEmpty());
}

TEST(RecordingLibrary, ARecordingIsRenamedOnTheDiskAndInTheIndexAndKeepsItsExtension)
{
    needApplication();
    QTemporaryDir folder;
    RecordingLibrary library(folder.filePath(u"library.db"_s));
    const QString path = folder.filePath(u"Session 2026-09-28.wav"_s);
    writeTake(path);
    library.fileStarted(u"s1"_s, u"rec"_s, path);
    library.fileFinished(u"s1"_s, u"rec"_s, path);
    library.waitForIdle();
    QSignalSpy changed(&library, &RecordingLibrary::changed);

    const std::expected<QString, QString> renamed = library.rename(path, u"  Interview with Sam  "_s);
    ASSERT_TRUE(renamed.has_value()) << renamed.error().toStdString();
    EXPECT_EQ(*renamed, folder.filePath(u"Interview with Sam.wav"_s));
    EXPECT_FALSE(QFile::exists(path));
    EXPECT_TRUE(QFile::exists(*renamed));
    const QJsonObject row = only(library.recordings(u"s1"_s));
    EXPECT_EQ(row.value(u"name"_s).toString(), u"Interview with Sam.wav"_s);
    EXPECT_DOUBLE_EQ(row.value(u"seconds"_s).toDouble(), 1.0) << "the rest of the row did not come with it";
    EXPECT_EQ(changed.size(), 1);
}

TEST(RecordingLibrary, ARenameThatWouldLoseSomethingIsRefused)
{
    needApplication();
    QTemporaryDir folder;
    RecordingLibrary library(folder.filePath(u"library.db"_s));
    const QString path = folder.filePath(u"one.wav"_s);
    const QString other = folder.filePath(u"two.wav"_s);
    writeTake(path);
    writeTake(other);
    library.fileStarted(u"s1"_s, u"rec"_s, path);
    library.fileFinished(u"s1"_s, u"rec"_s, path);
    library.waitForIdle();

    EXPECT_EQ(library.rename(path, u"two"_s).error(), u"exists"_s) << "another recording would have been written over";
    EXPECT_EQ(library.rename(path, u"../elsewhere"_s).error(), u"invalidName"_s);
    EXPECT_EQ(library.rename(path, u"   "_s).error(), u"invalidName"_s);
    EXPECT_EQ(library.rename(path, QString(231, u'x')).error(), u"invalidName"_s);
    EXPECT_EQ(library.rename(folder.filePath(u"gone.wav"_s), u"new"_s).error(), u"notFound"_s);
    EXPECT_TRUE(QFile::exists(path));
    EXPECT_TRUE(QFile::exists(other));
}

TEST(RecordingLibrary, TagsAreWrittenIntoTheFileReadBackAndKeptInTheIndex)
{
    needApplication();
    QTemporaryDir folder;
    RecordingLibrary library(folder.filePath(u"library.db"_s));
    const QString path = folder.filePath(u"show.wav"_s);
    writeTake(path);
    library.fileStarted(u"s1"_s, u"rec"_s, path);
    library.fileFinished(u"s1"_s, u"rec"_s, path);
    library.waitForIdle();

    const QJsonObject tags{{u"title"_s, u"Friday show — ep. 41"_s},
                           {u"artist"_s, u"Reverblabr FM"_s},
                           {u"album"_s, u"Season 3"_s},
                           {u"genre"_s, u"Podcast"_s},
                           {u"year"_s, u"2026"_s},
                           {u"track"_s, u"41"_s}};
    ASSERT_TRUE(library.saveTags(path, tags));

    // In the file, where any player reads them…
    const std::optional<record::Tags> inFile =
        record::readTags(std::filesystem::path{path.toStdString()}, record::FileFormat::Wav);
    ASSERT_TRUE(inFile.has_value());
    EXPECT_EQ(inFile->title, "Friday show — ep. 41");
    EXPECT_EQ(inFile->track, "41");
    // …in the index, where the Files tab reads them…
    const QJsonObject listed = only(library.recordings(u"s1"_s)).value(u"tags"_s).toObject();
    EXPECT_EQ(listed.value(u"artist"_s).toString(), u"Reverblabr FM"_s);
    // …and in the inspector's whole view of it, with the full outline.
    const std::optional<QJsonObject> details = library.details(path);
    ASSERT_TRUE(details.has_value());
    EXPECT_EQ(details->value(u"tags"_s).toObject().value(u"album"_s).toString(), u"Season 3"_s);
    EXPECT_EQ(details->value(u"peaks"_s).toArray().size(), RecordingLibrary::kPeakCount);

    EXPECT_FALSE(library.saveTags(folder.filePath(u"gone.wav"_s), tags));
}

TEST(RecordingLibrary, TheTagsTheRecorderWroteAreReadWhenTheFileFinishes)
{
    needApplication();
    QTemporaryDir folder;
    RecordingLibrary library(folder.filePath(u"library.db"_s));
    const QString path = folder.filePath(u"tagged.flac"_s);
    {
        record::FileSpec spec;
        spec.format = record::FileFormat::Flac;
        auto writer = record::AudioFileWriter::create(std::filesystem::path{path.toStdString()}, spec);
        ASSERT_TRUE(writer.has_value());
        const std::vector<float> samples(4800 * 2, 0.1f);
        ASSERT_TRUE(writer->write(samples).has_value());
        ASSERT_TRUE(writer->close().has_value());
        record::Tags tags;
        tags.title = "From the Recorder";
        ASSERT_TRUE(record::writeTags(std::filesystem::path{path.toStdString()}, record::FileFormat::Flac, tags));
    }
    library.fileStarted(u"s1"_s, u"rec"_s, path);
    library.fileFinished(u"s1"_s, u"rec"_s, path);
    library.waitForIdle();
    EXPECT_EQ(only(library.recordings(u"s1"_s)).value(u"tags"_s).toObject().value(u"title"_s).toString(),
              u"From the Recorder"_s);
}
