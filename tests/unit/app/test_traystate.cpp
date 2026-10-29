// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What the system tray says, against the mock engine: no tray host, no
// bus and no icon. The icon itself is tested on a private bus (tests/bus).

#include "appshell.h"
#include "core/editcommand.h"
#include "core/mockengine.h"
#include "core/telemetry.h"
#include "traystate.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QThread>
#include <gtest/gtest.h>

#include <cstring>

using namespace rb;
using namespace Qt::StringLiterals;

namespace
{

/// A timer needs an application: the policy's grace before a loss is said
/// runs on one. Whichever test of this binary comes first makes it.
void needApplication()
{
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char name[] = "reverblabr-app-test";
        static char *argv[] = {name, nullptr};
        static QCoreApplication application(argc, argv);
    }
}

template <typename Predicate>
bool spinUntil(Predicate predicate, int milliseconds = 3000)
{
    QElapsedTimer clock;
    clock.start();
    while (!predicate() && clock.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
    return predicate();
}

[[nodiscard]] QString edit(const core::EditCommand &command)
{
    return QString::fromUtf8(QJsonDocument(core::editCommandToJson(command)).toJson(QJsonDocument::Compact));
}

[[nodiscard]] QString addBlock(const QString &blockId, const QString &type, const QJsonObject &params = {})
{
    core::BlockInstance block;
    block.id = blockId;
    block.type = type;
    block.params = params;
    return edit(core::AddBlock{block, -1});
}

[[nodiscard]] QString openSession(core::MockEngine &engine, const QString &name)
{
    QSignalSpy opened(&engine, &core::EngineFacade::sessionOpened);
    engine.createSession(name);
    return opened.isEmpty() ? QString{} : opened.at(0).at(0).toString();
}

[[nodiscard]] QStringList meterIds(const ui::TrayState &tray)
{
    QStringList ids;
    for (const QVariant &meter : tray.meters()) {
        ids.append(meter.toMap().value(u"blockId"_s).toString());
    }
    return ids;
}

class TrayStateTest : public ::testing::Test
{
protected:
    void TearDown() override
    {
        ui::AppShell::setEngine(nullptr);
    }
};

} // namespace

TEST_F(TrayStateTest, ItIsActiveWhileASessionRunsAndSaysHowMany)
{
    core::MockEngine engine;
    const QString one = openSession(engine, u"One"_s);
    const QString two = openSession(engine, u"Two"_s);
    ui::TrayState tray(&engine);
    ASSERT_TRUE(spinUntil([&] { return tray.status() == ui::TrayState::kPassive; }));
    EXPECT_EQ(tray.runningCount(), 0);
    EXPECT_EQ(tray.toolTip(), u"No session running"_s);

    engine.start(one);
    ASSERT_TRUE(spinUntil([&] { return tray.runningCount() == 1; }));
    EXPECT_EQ(tray.status(), QString(ui::TrayState::kActive));
    EXPECT_EQ(tray.toolTip(), u"1 session running"_s);
    engine.start(two);
    ASSERT_TRUE(spinUntil([&] { return tray.runningCount() == 2; }));
    EXPECT_EQ(tray.toolTip(), u"2 sessions running"_s);

    engine.stop(one);
    engine.stop(two);
    ASSERT_TRUE(spinUntil([&] { return tray.runningCount() == 0; }));
    EXPECT_EQ(tray.status(), QString(ui::TrayState::kPassive));
}

// **A window closed into the tray leaves the icon as the only way back**,
// and a desktop tray hides passive icons behind its arrow: with nothing
// running, the icon is passive only while the window shows.
TEST_F(TrayStateTest, TheIconDoesNotHideWhileTheWindowIsClosedIntoIt)
{
    core::MockEngine engine;
    (void)openSession(engine, u"One"_s);
    ui::TrayState tray(&engine);
    ASSERT_TRUE(spinUntil([&] { return tray.status() == ui::TrayState::kPassive; }));

    QSignalSpy changed(&tray, &ui::TrayState::changed);
    tray.setWindowShown(false);
    EXPECT_EQ(tray.status(), QString(ui::TrayState::kActive));
    EXPECT_EQ(changed.count(), 1) << "the icon is not told to show itself";

    tray.setWindowShown(true);
    EXPECT_EQ(tray.status(), QString(ui::TrayState::kPassive));
}

TEST_F(TrayStateTest, ARecorderWritingIsTheRedDotAndAPausedOneIsNot)
{
    core::MockEngine engine;
    const QString id = openSession(engine, u"Show"_s);
    engine.applyEdit(id, addBlock(u"rec"_s, u"output.recorder"_s));
    ui::TrayState tray(&engine);
    EXPECT_FALSE(tray.isRecording());

    engine.start(id);
    ASSERT_TRUE(spinUntil([&] { return tray.isRecording(); })) << "a Recorder writing a file is not recording";
    EXPECT_EQ(tray.toolTip(), u"1 session running, recording"_s);

    engine.controlRecording(id, u"rec"_s, u"pause"_s);
    ASSERT_TRUE(spinUntil([&] { return !tray.isRecording(); })) << "a paused Recorder still shows the red dot";
    engine.controlRecording(id, u"rec"_s, u"resume"_s);
    ASSERT_TRUE(spinUntil([&] { return tray.isRecording(); }));

    engine.stop(id);
    ASSERT_TRUE(spinUntil([&] { return !tray.isRecording(); })) << "a stopped session still records";
}

TEST_F(TrayStateTest, GoodNewsDoesNotAskForAttention)
{
    needApplication();
    core::MockEngine engine;
    ui::TrayState tray(&engine);
    const QString args = u"{\"session\":\"Call\",\"block\":\"app\",\"application\":\"Firefox\"}"_s;

    // Lost for longer than the policy's grace: that is news, and asks.
    Q_EMIT engine.notice(u"capture.lost"_s, args);
    ASSERT_TRUE(spinUntil([&] { return tray.needsAttention(); }, 8000)) << "an application gone quiet did not ask";
    tray.acknowledge();

    // **Back again is good news**: said as a notification, and nothing for
    // the tray to ask anybody to look at.
    Q_EMIT engine.notice(u"capture.regained"_s, args);
    QCoreApplication::processEvents();
    EXPECT_FALSE(tray.needsAttention()) << "an application playing again asked for attention";
}

TEST_F(TrayStateTest, TheMetersMarkedForTheTrayAreListedAndFollowEveryEdit)
{
    core::MockEngine engine;
    const QString id = openSession(engine, u"Studio"_s);
    // Marked before the tray is there: its snapshot is asked for.
    engine.applyEdit(id, addBlock(u"shown"_s, u"meter.peakrms"_s, QJsonObject{{u"showInTray"_s, true}}));
    engine.applyEdit(id, addBlock(u"hidden"_s, u"meter.peakrms"_s));
    engine.applyEdit(id, addBlock(u"notAMeter"_s, u"effect.volume"_s, QJsonObject{{u"showInTray"_s, true}}));
    ui::TrayState tray(&engine);
    ASSERT_TRUE(spinUntil([&] { return meterIds(tray) == QStringList{u"shown"_s}; }))
        << "the tray did not list the marked meter of a session opened before it";
    EXPECT_EQ(tray.meters().at(0).toMap().value(u"sessionName"_s).toString(), u"Studio"_s);

    // Marked and unmarked from the panel.
    engine.applyEdit(id, edit(core::SetParam{u"hidden"_s, u"showInTray"_s, true}));
    ASSERT_TRUE(spinUntil([&] { return meterIds(tray) == (QStringList{u"shown"_s, u"hidden"_s}); }));
    engine.applyEdit(id, edit(core::SetParam{u"shown"_s, u"showInTray"_s, false}));
    ASSERT_TRUE(spinUntil([&] { return meterIds(tray) == QStringList{u"hidden"_s}; }));

    // A VU meter can be shown too, and one added marked is listed at once.
    engine.applyEdit(id, addBlock(u"vu"_s, u"meter.vu"_s, QJsonObject{{u"showInTray"_s, true}}));
    ASSERT_TRUE(spinUntil([&] { return meterIds(tray) == (QStringList{u"hidden"_s, u"vu"_s}); }));

    // Renamed, it is called by its new name; removed, it goes.
    engine.applyEdit(id, edit(core::Rename{u"vu"_s, u"Programme"_s}));
    ASSERT_TRUE(spinUntil([&] { return tray.meters().value(1).toMap().value(u"nickname"_s) == u"Programme"_s; }));
    engine.applyEdit(id, edit(core::RemoveBlock{u"hidden"_s}));
    ASSERT_TRUE(spinUntil([&] { return meterIds(tray) == QStringList{u"vu"_s}; }));

    // The session renamed, and then closed: its meters go with it.
    engine.applyEdit(id, edit(core::Rename{QString{}, u"Booth"_s}));
    ASSERT_TRUE(spinUntil([&] { return tray.meters().value(0).toMap().value(u"sessionName"_s) == u"Booth"_s; }));
    engine.closeSession(id);
    ASSERT_TRUE(spinUntil([&] { return tray.meters().isEmpty(); }));
}

TEST_F(TrayStateTest, AMeterReadsItsOwnBlocksLevelWhileItsSessionRuns)
{
    core::MockEngine engine;
    const QString id = openSession(engine, u"Levels"_s);
    engine.applyEdit(id, addBlock(u"meter"_s, u"meter.peakrms"_s, QJsonObject{{u"showInTray"_s, true}}));
    core::TelemetrySlot slot;
    ui::AppShell::setEngine(&engine, [&](const QString &sessionId) { return sessionId == id ? &slot : nullptr; });
    core::TelemetrySnapshot snapshot;
    snapshot.frame = 1;
    snapshot.running = true;
    snapshot.blockCount = 2;
    std::strncpy(snapshot.blocks[0].id, "louder", core::kMaxTelemetryIdBytes - 1);
    snapshot.blocks[0].channelCount = 2;
    snapshot.blocks[0].peak[0] = 1.0f;
    std::strncpy(snapshot.blocks[1].id, "meter", core::kMaxTelemetryIdBytes - 1);
    snapshot.blocks[1].channelCount = 2;
    snapshot.blocks[1].peak[1] = 0.5f; // -6 dBFS, on the right
    snapshot.blocks[1].active = true;
    slot.store(snapshot);

    ui::TrayState tray(&engine);
    ASSERT_TRUE(spinUntil([&] { return tray.meters().size() == 1; }));
    tray.readLevels();
    EXPECT_LE(tray.meterLevel(id, u"meter"_s).value(u"peakDb"_s).toDouble(), -100.0)
        << "a meter whose session does not run showed a level";

    engine.start(id);
    ASSERT_TRUE(spinUntil([&] { return tray.runningCount() == 1; }));
    tray.readLevels();
    const QVariantMap level = tray.meterLevel(id, u"meter"_s);
    EXPECT_NEAR(level.value(u"peakDb"_s).toDouble(), -6.02, 0.05) << "not its own block's level";
    EXPECT_TRUE(level.value(u"active"_s).toBool());

    engine.stop(id);
    ASSERT_TRUE(spinUntil([&] { return tray.runningCount() == 0; }));
    tray.readLevels();
    EXPECT_LE(tray.meterLevel(id, u"meter"_s).value(u"peakDb"_s).toDouble(), -100.0);
}

TEST_F(TrayStateTest, WhatWentWrongAsksForAttentionUntilItIsSeen)
{
    core::MockEngine engine;
    const QString id = openSession(engine, u"Plugins"_s);
    ui::TrayState tray(&engine);
    ASSERT_TRUE(spinUntil([&] { return tray.status() == ui::TrayState::kPassive; }));

    // Something that is not news does not ask.
    engine.reportError(u"edit.blockNotFound"_s, u"{}"_s);
    EXPECT_FALSE(tray.needsAttention()) << "an everyday error asked for attention";

    engine.reportError(u"plugin.crashed"_s,
                       u"{\"sessionId\":\"%1\",\"blockId\":\"p\",\"plugin\":\"Reverb\"}"_s.arg(id));
    ASSERT_TRUE(spinUntil([&] { return tray.needsAttention(); })) << "a crashed plugin did not ask for attention";
    EXPECT_EQ(tray.status(), QString(ui::TrayState::kAttention));
    EXPECT_EQ(tray.toolTip(), u"Plugin stopped"_s);

    tray.acknowledge();
    EXPECT_FALSE(tray.needsAttention()) << "looking at it did not count as seen";
    EXPECT_EQ(tray.status(), QString(ui::TrayState::kPassive));

    // **A session in error keeps asking**, since looking does not mend it.
    engine.setAudioSystemRunning(false);
    engine.start(id);
    ASSERT_TRUE(spinUntil([&] { return tray.needsAttention(); })) << "a session in error did not ask";
    tray.acknowledge();
    EXPECT_TRUE(tray.needsAttention()) << "a session still in error stopped asking once it was seen";
}
