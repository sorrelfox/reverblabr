// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What each Recorder is doing, as the face and the panel read it:
// the engine's word on files, pauses and stops, and the clock the REC pill
// counts, which stops while a recording is paused and goes on across a
// split.

#include "core/enginefacade.h"
#include "core/mockengine.h"
#include "core/presetstore.h"
#include "recordingstatus.h"

#include <QDir>
#include <QTemporaryDir>
#include <QUrl>
#include <gtest/gtest.h>

using namespace rb;
using namespace Qt::StringLiterals;

namespace
{

class RecordingStatusTest : public ::testing::Test
{
protected:
    RecordingStatusTest()
    {
        status.attach(&engine);
        status.setClock([this] { return now; });
    }

    /// What the engine says, by hand: the mock's own signals.
    void started(const QString &file)
    {
        Q_EMIT engine.recordingFileStarted(u"s"_s, u"rec"_s, file);
    }

    void paused(bool paused)
    {
        Q_EMIT engine.recordingPaused(u"s"_s, u"rec"_s, paused);
    }

    QTemporaryDir presets;
    core::MockEngine engine{core::PresetStore{presets.path()}};
    ui::RecordingStatus status;
    qint64 now = 1'000'000;
};

} // namespace

TEST_F(RecordingStatusTest, AFileStartedIsARecordingGoingAndItsClockCounts)
{
    EXPECT_EQ(status.state(u"s"_s, u"rec"_s), u"idle"_s);
    started(u"/music/take 001.mp3"_s);
    EXPECT_EQ(status.state(u"s"_s, u"rec"_s), u"recording"_s);
    EXPECT_EQ(status.file(u"s"_s, u"rec"_s), u"/music/take 001.mp3"_s);
    now += 12'500;
    EXPECT_EQ(status.elapsedSeconds(u"s"_s, u"rec"_s), 12);
    EXPECT_EQ(ui::RecordingStatus::clock(3723), u"01:02:03"_s);
}

TEST_F(RecordingStatusTest, APauseStopsTheClockAndASplitDoesNotStartItAgain)
{
    started(u"/music/take 001.mp3"_s);
    now += 10'000;
    paused(true);
    EXPECT_EQ(status.state(u"s"_s, u"rec"_s), u"paused"_s);
    now += 60'000; // paused: not counted
    EXPECT_EQ(status.elapsedSeconds(u"s"_s, u"rec"_s), 10);
    paused(false);
    now += 5'000;
    EXPECT_EQ(status.elapsedSeconds(u"s"_s, u"rec"_s), 15);

    // **A split is a new file, not a new recording**: the clock goes on.
    Q_EMIT engine.recordingFileFinished(u"s"_s, u"rec"_s, u"/music/take 001.mp3"_s, u"{}"_s);
    started(u"/music/take 002.mp3"_s);
    now += 5'000;
    EXPECT_EQ(status.elapsedSeconds(u"s"_s, u"rec"_s), 20);
    EXPECT_EQ(status.file(u"s"_s, u"rec"_s), u"/music/take 002.mp3"_s);
}

TEST_F(RecordingStatusTest, AStopByItselfIsSaidWithItsReasonAndAStoppedSessionIsIdle)
{
    started(u"/music/take.mp3"_s);
    now += 3'000;
    Q_EMIT engine.recordingStopped(u"s"_s, u"rec"_s, u"silence"_s);
    EXPECT_EQ(status.state(u"s"_s, u"rec"_s), u"stopped"_s);
    EXPECT_EQ(status.stopReason(u"s"_s, u"rec"_s), u"silence"_s);
    now += 3'000;
    EXPECT_EQ(status.elapsedSeconds(u"s"_s, u"rec"_s), 3) << "a stopped recording's clock went on";

    Q_EMIT engine.stateChanged(u"s"_s, u"idle"_s);
    EXPECT_EQ(status.state(u"s"_s, u"rec"_s), u"idle"_s);
    // The next start is a new recording, from nought.
    started(u"/music/take (2).mp3"_s);
    EXPECT_EQ(status.elapsedSeconds(u"s"_s, u"rec"_s), 0);
    // Another session's Recorder of the same id is another Recorder.
    EXPECT_EQ(status.state(u"other"_s, u"rec"_s), u"idle"_s);
}

TEST_F(RecordingStatusTest, AFolderIsShownAndChosenAsAPersonWritesIt)
{
    EXPECT_EQ(ui::RecordingStatus::folderUrl(QString{}), QUrl::fromLocalFile(ui::RecordingStatus::defaultFolder()));
    EXPECT_EQ(ui::RecordingStatus::folderUrl(u"~/Podcasts"_s), QUrl::fromLocalFile(QDir::homePath() + u"/Podcasts"_s));
    EXPECT_EQ(ui::RecordingStatus::folderFromUrl(QUrl::fromLocalFile(QDir::homePath() + u"/Podcasts"_s)),
              u"~/Podcasts"_s);
    EXPECT_EQ(ui::RecordingStatus::folderFromUrl(QUrl::fromLocalFile(u"/srv/recordings"_s)), u"/srv/recordings"_s);
}

// **The preview is the writer's own naming**: what it would make safe, the
// preview makes safe, and the extension is the format's.
TEST_F(RecordingStatusTest, TheNamePreviewIsTheNameTheFileWillHave)
{
    const QString name =
        ui::RecordingStatus::namePreview(u"Mix 1/2 %session %counter"_s, u"Take"_s, u"rec"_s, u"flac"_s);
    EXPECT_EQ(name, u"Mix 1-2 Take 001.flac"_s);
    EXPECT_EQ(ui::RecordingStatus::namePreview(u"  "_s, u"Take"_s, u"rec"_s, u"aac"_s).right(4), u".m4a"_s);
    EXPECT_TRUE(ui::RecordingStatus::namePreview(u"  "_s, u"Take"_s, u"rec"_s, u"mp3"_s).startsWith(u"Take "_s));
    EXPECT_EQ(ui::RecordingStatus::namePreview(u"..."_s, u"Take"_s, u"rec"_s, u"wav"_s), u"Recording.wav"_s);
}
