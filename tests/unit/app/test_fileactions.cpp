// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Files tab's actions and the list's words. The actions reach the
// desktop through KIO; here they reach a stand-in, except for one trash
// job run for real into a trash of the test's own — never anybody's.

#include "fileactions.h"
#include "recordinglist.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <gtest/gtest.h>

using namespace rb::ui;
using namespace Qt::StringLiterals;

namespace
{

void needApplication()
{
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char name[] = "reverblabr-app-test";
        static char *argv[] = {name, nullptr};
        // Offscreen, so the test needs no display.
        qputenv("QT_QPA_PLATFORM", "offscreen");
        static QGuiApplication application(argc, argv);
    }
}

/// Puts KIO back however a test ends.
struct RunnerGuard {
    ~RunnerGuard()
    {
        FileActions::setRunnerForTesting({});
    }
};

} // namespace

TEST(FileActionsTest, EachActionAsksForTheRightJobOnTheRightFile)
{
    needApplication();
    const RunnerGuard guard;
    QList<std::pair<FileActions::Action, QUrl>> asked;
    FileActions::setRunnerForTesting(
        [&asked](FileActions::Action action, const QUrl &url, QWindow *, const FileActions::Done &done) {
            asked.append({action, url});
            done(QString{});
        });

    FileActions actions;
    QSignalSpy trashed(&actions, &FileActions::trashed);
    const QString path = u"/music/Reverblabr/Talk 2026-09-25.flac"_s;
    actions.showInFolder(path);
    actions.openWith(path);
    actions.moveToTrash(path);

    ASSERT_EQ(asked.size(), 3);
    EXPECT_EQ(asked.at(0).first, FileActions::Action::ShowInFolder);
    EXPECT_EQ(asked.at(1).first, FileActions::Action::OpenWith);
    EXPECT_EQ(asked.at(2).first, FileActions::Action::MoveToTrash);
    for (const auto &[action, url] : asked) {
        EXPECT_EQ(url, QUrl::fromLocalFile(path)) << "a path with a space was not made a file URL";
    }
    ASSERT_EQ(trashed.size(), 1);
    EXPECT_EQ(trashed.first().first().toString(), path);

    actions.moveToTrash(QString{});
    EXPECT_EQ(asked.size(), 3) << "nothing was asked for on no file at all";
}

TEST(FileActionsTest, AFailureIsReportedInKiosWordsAndNothingIsTrashed)
{
    needApplication();
    const RunnerGuard guard;
    FileActions::setRunnerForTesting([](FileActions::Action, const QUrl &, QWindow *, const FileActions::Done &done) {
        done(u"Could not move to the trash"_s);
    });
    FileActions actions;
    QSignalSpy trashed(&actions, &FileActions::trashed);
    QSignalSpy failed(&actions, &FileActions::failed);
    actions.moveToTrash(u"/tmp/x.wav"_s);
    EXPECT_TRUE(trashed.isEmpty());
    ASSERT_EQ(failed.size(), 1);
    EXPECT_EQ(failed.first().at(2).toString(), u"Could not move to the trash"_s);
}

TEST(FileActionsTest, TheTrashIsTheFreedesktopTrashAndTheFileCanComeBack)
{
    // **One real KIO job**, into a trash of the test's own: its data home
    // and the file both inside the build tree's test home, on one disk, so
    // KIO picks that home's trash rather than one at the top of another
    // disk.
    needApplication();
    const QString base = qEnvironmentVariable("XDG_CACHE_HOME");
    ASSERT_FALSE(base.isEmpty()) << "no test home: this test must not run against a real one";
    // **Made here, not assumed**: in a build tree nothing has run in
    // yet, the cache folder is not there, and a temporary folder cannot be
    // made inside a folder that does not exist.
    ASSERT_TRUE(QDir().mkpath(base));
    QTemporaryDir home(QDir(base).filePath(u"rb-trash-XXXXXX"_s));
    ASSERT_TRUE(home.isValid());
    const QByteArray sharedData = qgetenv("XDG_DATA_HOME");
    qputenv("XDG_DATA_HOME", home.filePath(u"data"_s).toUtf8());
    const auto restore = qScopeGuard([&sharedData] { qputenv("XDG_DATA_HOME", sharedData); });
    QDir().mkpath(home.filePath(u"data"_s));

    const QString path = home.filePath(u"Talk 2026-09-25 10-00.wav"_s);
    {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("RIFF");
    }

    QString answer = u"(no answer)"_s;
    QEventLoop loop;
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    FileActions::runWithKio(
        FileActions::Action::MoveToTrash, QUrl::fromLocalFile(path), nullptr, [&answer, &loop](const QString &error) {
            answer = error;
            loop.quit();
        });
    loop.exec();
    if (answer.contains(u"trash"_s, Qt::CaseInsensitive) && answer.contains(u"protocol"_s, Qt::CaseInsensitive)) {
        GTEST_SKIP() << "this machine has no KIO trash worker: " << answer.toStdString();
    }
    ASSERT_TRUE(answer.isEmpty()) << answer.toStdString();
    EXPECT_FALSE(QFile::exists(path)) << "the file is still where it was";
    const QString trashed = home.filePath(u"data/Trash/files/Talk 2026-09-25 10-00.wav"_s);
    EXPECT_TRUE(QFile::exists(trashed)) << "not in the freedesktop trash of the test's own home";
    // The trash knows where it came from, which is what lets it be put back.
    QFile info(home.filePath(u"data/Trash/info/Talk 2026-09-25 10-00.wav.trashinfo"_s));
    ASSERT_TRUE(info.open(QIODevice::ReadOnly));
    EXPECT_TRUE(QString::fromUtf8(info.readAll()).contains(u"Path="_s));
}

TEST(RecordingListTest, LengthsAndSizesAreWrittenTheWayTheListShowsThem)
{
    needApplication();
    EXPECT_EQ(RecordingList::formatDuration(0), u"0:00"_s);
    EXPECT_EQ(RecordingList::formatDuration(42.9), u"0:42"_s);
    EXPECT_EQ(RecordingList::formatDuration(58 * 60 + 12), u"58:12"_s);
    EXPECT_EQ(RecordingList::formatDuration(3600 + 30 * 60 + 41), u"1:30:41"_s);
    EXPECT_EQ(RecordingList::formatName(u"flac"_s), u"FLAC"_s);
    const QString size = RecordingList::formatSize(111.6e6);
    EXPECT_TRUE(size.contains(u"MB"_s)) << size.toStdString();
    EXPECT_TRUE(size.contains(u"111"_s)) << size.toStdString();
}
