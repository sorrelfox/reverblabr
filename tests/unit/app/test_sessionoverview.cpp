// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Sessions window's table: every saved session as a row, Run and
// Stop for several at once, a live level, and columns that stay where they
// were put. The window that draws it is checked in tests/qml.
//
// **The state file is the test's own**, so running this does not reach into
// whoever is running it; the real engine's sessions live in a temporary
// folder of the test's.

#include "appshell.h"
#include "blocks/builtinblocks.h"
#include "core/editcommand.h"
#include "core/engine.h"
#include "core/mockengine.h"
#include "core/sessionstore.h"
#include "core/telemetry.h"
#include "dsp/blockregistry.h"
#include "sessionoverview.h"
#include "windowstate.h"

#include <KSharedConfig>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <gtest/gtest.h>

#include <cstring>

using namespace rb;
using namespace Qt::StringLiterals;

namespace
{

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

/// The names of the rows, in the order the table shows them.
[[nodiscard]] QStringList namesOf(const ui::SessionOverview &table)
{
    QStringList names;
    for (int row = 0; row < table.rowCount(); ++row) {
        names.append(table.rowAt(row).value(u"name"_s).toString());
    }
    return names;
}

[[nodiscard]] QString addBlock(const QString &blockId, const QString &type, const QString &nickname = {})
{
    core::BlockInstance block;
    block.id = blockId;
    block.type = type;
    block.nickname = nickname;
    return QString::fromUtf8(
        QJsonDocument(core::editCommandToJson(core::AddBlock{block, -1})).toJson(QJsonDocument::Compact));
}

class SessionOverviewTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(state.isValid());
        ui::WindowState::setConfig(
            KSharedConfig::openConfig(state.filePath(u"reverblabrstaterc"_s), KConfig::SimpleConfig));
    }

    void TearDown() override
    {
        ui::AppShell::setEngine(nullptr);
        ui::WindowState::setConfig({});
    }

    QTemporaryDir state;
};

} // namespace

TEST_F(SessionOverviewTest, EverySessionIsARowWithWhatItCapturesAndWhereItStands)
{
    core::MockEngine engine;
    QSignalSpy opened(&engine, &core::EngineFacade::sessionOpened);
    engine.createSession(u"Radio"_s);
    engine.createSession(u"call"_s);
    ASSERT_EQ(opened.count(), 2);
    const QString radio = opened.at(0).at(0).toString();
    const QString call = opened.at(1).at(0).toString();
    engine.applyEdit(call, addBlock(u"b1"_s, u"source.application"_s, u"The call"_s));
    engine.start(radio);

    ui::SessionOverview table;
    table.attach(&engine);
    ASSERT_TRUE(spinUntil([&] { return table.count() == 2; }));

    // Sorted by name at first, whatever the case of the first letter.
    EXPECT_EQ(namesOf(table), (QStringList{u"call"_s, u"Radio"_s}));
    const QVariantMap first = table.rowAt(0);
    EXPECT_EQ(first.value(u"id"_s).toString(), call);
    EXPECT_EQ(first.value(u"state"_s).toString(), QString(core::sessionState::idle));
    const QVariantList sources = first.value(u"sources"_s).toList();
    ASSERT_EQ(sources.size(), 1);
    EXPECT_EQ(sources.at(0).toMap().value(u"type"_s).toString(), u"source.application"_s);
    EXPECT_EQ(sources.at(0).toMap().value(u"name"_s).toString(), u"The call"_s);
    const QVariantMap second = table.rowAt(1);
    EXPECT_TRUE(second.value(u"running"_s).toBool());
    EXPECT_TRUE(second.value(u"startedAt"_s).toDateTime().isValid()) << "a running session says when it started";

    // **Stopped anywhere, the row follows**, and says so without being asked.
    engine.stop(radio);
    ASSERT_TRUE(spinUntil([&] { return !table.rowAt(table.rowOf(radio)).value(u"running"_s).toBool(); }));
    EXPECT_FALSE(table.rowAt(table.rowOf(radio)).value(u"startedAt"_s).toDateTime().isValid());
}

TEST_F(SessionOverviewTest, SeveralSessionsAreStartedAndStoppedAtOnce)
{
    core::MockEngine engine;
    QSignalSpy opened(&engine, &core::EngineFacade::sessionOpened);
    engine.createSession(u"One"_s);
    engine.createSession(u"Two"_s);
    engine.createSession(u"Three"_s);
    ASSERT_EQ(opened.count(), 3);
    const QString one = opened.at(0).at(0).toString();
    const QString two = opened.at(1).at(0).toString();
    const QString three = opened.at(2).at(0).toString();

    ui::SessionOverview table;
    table.attach(&engine);
    ASSERT_TRUE(spinUntil([&] { return table.count() == 3; }));

    table.run({one, three});
    const auto running = [&](const QString &id) { return table.rowAt(table.rowOf(id)).value(u"running"_s).toBool(); };
    ASSERT_TRUE(spinUntil([&] { return running(one) && running(three); }));
    EXPECT_FALSE(running(two)) << "a session nobody asked for was started";

    table.stop({one, two, three});
    ASSERT_TRUE(spinUntil([&] { return !running(one) && !running(three); }));
}

TEST_F(SessionOverviewTest, ASessionOnlyOnDiskIsOpenedAndThenStarted)
{
    QTemporaryDir sessions;
    ASSERT_TRUE(sessions.isValid());
    dsp::BlockRegistry registry;
    ASSERT_TRUE(blocks::registerBuiltinBlocks(registry));
    core::Engine engine(registry, core::SessionStore{sessions.path()});
    QSignalSpy opened(&engine, &core::EngineFacade::sessionOpened);
    QSignalSpy states(&engine, &core::EngineFacade::stateChanged);
    engine.createSession(u"Put away"_s);
    ASSERT_EQ(opened.count(), 1);
    const QString id = opened.at(0).at(0).toString();
    engine.closeSession(id);

    ui::SessionOverview table;
    table.attach(&engine);
    ASSERT_TRUE(spinUntil([&] { return table.count() == 1; }));
    ASSERT_EQ(table.rowAt(0).value(u"state"_s).toString(), QString(core::sessionState::closed));

    states.clear();
    table.run({id});
    // Opened, and then asked to start: with no audio system behind this
    // engine the start is refused, which is still the start being asked.
    ASSERT_TRUE(spinUntil([&] { return opened.count() == 2 && !states.isEmpty(); }))
        << "the session was not opened and started";
    EXPECT_EQ(opened.at(1).at(0).toString(), id);
    EXPECT_EQ(states.at(0).at(0).toString(), id);
}

// **Auto Run is a column**: what the list says for each session, set
// from the table as an edit of the session, and the row says the new value
// from the patch rather than waiting for the list.
TEST_F(SessionOverviewTest, AutoRunIsShownAndSetAsAnEditOfTheSession)
{
    core::MockEngine engine;
    QSignalSpy opened(&engine, &core::EngineFacade::sessionOpened);
    QSignalSpy patched(&engine, &core::EngineFacade::sessionPatch);
    engine.createSession(u"Radio"_s);
    engine.createSession(u"Call"_s);
    ASSERT_EQ(opened.count(), 2);
    const QString radio = opened.at(0).at(0).toString();
    const QString call = opened.at(1).at(0).toString();

    ui::SessionOverview table;
    table.attach(&engine);
    ASSERT_TRUE(spinUntil([&] { return table.count() == 2; }));
    EXPECT_TRUE(ui::SessionOverview::allColumns().contains(u"autoRun"_s));
    const auto autoRun = [&](const QString &id) { return table.rowAt(table.rowOf(id)).value(u"autoRun"_s).toBool(); };
    EXPECT_FALSE(autoRun(radio));

    patched.clear();
    table.setAutoRun({radio}, true);
    ASSERT_EQ(patched.count(), 1) << "set as an edit of the session";
    const QJsonObject patch = QJsonDocument::fromJson(patched.at(0).at(2).toString().toUtf8()).object();
    EXPECT_EQ(patch.value("op"_L1).toString(), u"setSessionSetting"_s);
    EXPECT_EQ(patch.value("key"_L1).toString(), u"autoRun"_s);
    EXPECT_EQ(patch.value("value"_L1).toString(), u"login"_s);
    EXPECT_TRUE(autoRun(radio)) << "the row waited for the list";
    EXPECT_FALSE(autoRun(call));

    // Sorted by it, the ones that start at login come first.
    table.sortBy(u"autoRun"_s);
    EXPECT_EQ(table.rowAt(0).value(u"id"_s).toString(), radio);

    table.setAutoRun({radio}, false);
    EXPECT_FALSE(autoRun(radio));
}

// **One only on disk is opened to be set**, as Run opens it, and set once it
// has opened; the list then says so from its file.
TEST_F(SessionOverviewTest, AutoRunOfASessionOnlyOnDiskIsSetOnceItHasOpened)
{
    QTemporaryDir sessions;
    ASSERT_TRUE(sessions.isValid());
    dsp::BlockRegistry registry;
    ASSERT_TRUE(blocks::registerBuiltinBlocks(registry));
    core::Engine engine(registry, core::SessionStore{sessions.path()});
    QSignalSpy opened(&engine, &core::EngineFacade::sessionOpened);
    QSignalSpy patched(&engine, &core::EngineFacade::sessionPatch);
    engine.createSession(u"Put away"_s);
    ASSERT_EQ(opened.count(), 1);
    const QString id = opened.at(0).at(0).toString();
    engine.closeSession(id);

    ui::SessionOverview table;
    table.attach(&engine);
    ASSERT_TRUE(spinUntil([&] { return table.count() == 1; }));
    ASSERT_EQ(table.rowAt(0).value(u"state"_s).toString(), QString(core::sessionState::closed));

    patched.clear();
    table.setAutoRun({id}, true);
    ASSERT_TRUE(spinUntil([&] { return opened.count() == 2 && patched.count() == 1; }))
        << "the session was not opened and set";
    EXPECT_EQ(opened.at(1).at(0).toString(), id);
    EXPECT_TRUE(spinUntil([&] { return table.rowAt(table.rowOf(id)).value(u"autoRun"_s).toBool(); }));
}

TEST_F(SessionOverviewTest, TheColumnsStayHiddenMovedSizedAndSortedAsTheyWereLeft)
{
    core::MockEngine engine;
    {
        ui::SessionOverview table;
        table.attach(&engine);
        EXPECT_EQ(table.columns(), ui::SessionOverview::allColumns());

        table.setColumnShown(u"recordings"_s, false);
        table.moveColumn(3, 1); // "status", once recordings is hidden, to second
        table.setColumnWidth(u"name"_s, 240);
        table.sortBy(u"status"_s);
        table.sortBy(u"status"_s); // twice: the other way round
        EXPECT_EQ(table.columns(),
                  (QStringList{u"name"_s, u"status"_s, u"sources"_s, u"level"_s, u"autoRun"_s, u"run"_s}));
    }

    // **A second window, as the next launch makes one**, finds them as left.
    ui::SessionOverview again;
    again.attach(&engine);
    EXPECT_EQ(again.columns(), (QStringList{u"name"_s, u"status"_s, u"sources"_s, u"level"_s, u"autoRun"_s, u"run"_s}));
    EXPECT_FALSE(again.isColumnShown(u"recordings"_s));
    EXPECT_EQ(again.columnWidth(u"name"_s), 240);
    EXPECT_EQ(again.sortColumn(), u"status"_s);
    EXPECT_TRUE(again.sortDescending());

    // And shown again, it comes back where it sat.
    again.setColumnShown(u"recordings"_s, true);
    EXPECT_EQ(again.columns(),
              (QStringList{u"name"_s, u"status"_s, u"sources"_s, u"recordings"_s, u"level"_s, u"autoRun"_s, u"run"_s}));
}

TEST_F(SessionOverviewTest, TheNameCannotBeHidden)
{
    ui::SessionOverview table;
    table.setColumnShown(u"name"_s, false);
    EXPECT_TRUE(table.isColumnShown(u"name"_s));
    EXPECT_TRUE(table.columns().contains(u"name"_s));
}

TEST_F(SessionOverviewTest, SortedByStatusWhatRunsComesFirst)
{
    core::MockEngine engine;
    QSignalSpy opened(&engine, &core::EngineFacade::sessionOpened);
    engine.createSession(u"Alpha"_s);
    engine.createSession(u"Beta"_s);
    ASSERT_EQ(opened.count(), 2);
    engine.start(opened.at(1).at(0).toString());

    ui::SessionOverview table;
    table.attach(&engine);
    ASSERT_TRUE(spinUntil([&] { return table.count() == 2; }));
    table.sortBy(u"status"_s);
    EXPECT_EQ(namesOf(table), (QStringList{u"Beta"_s, u"Alpha"_s}));
}

TEST_F(SessionOverviewTest, TheLevelIsTheLoudestBlockAndComesDownSteadily)
{
    core::MockEngine engine;
    QSignalSpy opened(&engine, &core::EngineFacade::sessionOpened);
    engine.createSession(u"Loud"_s);
    ASSERT_EQ(opened.count(), 1);
    const QString id = opened.at(0).at(0).toString();
    engine.start(id);
    core::TelemetrySlot slot;
    ui::AppShell::setEngine(&engine, [&](const QString &sessionId) { return sessionId == id ? &slot : nullptr; });

    core::TelemetrySnapshot snapshot;
    snapshot.frame = 1;
    snapshot.running = true;
    snapshot.blockCount = 2;
    std::strncpy(snapshot.blocks[0].id, "quiet", core::kMaxTelemetryIdBytes - 1);
    snapshot.blocks[0].channelCount = 2;
    snapshot.blocks[0].peak[0] = 0.1f; // -20 dBFS
    std::strncpy(snapshot.blocks[1].id, "loud", core::kMaxTelemetryIdBytes - 1);
    snapshot.blocks[1].channelCount = 2;
    snapshot.blocks[1].peak[1] = 0.5f; // -6 dBFS, on the right
    slot.store(snapshot);

    ui::SessionOverview table;
    table.attach(&engine);
    ASSERT_TRUE(spinUntil([&] { return table.count() == 1; }));
    table.readLevels();
    EXPECT_NEAR(table.rowAt(0).value(u"level"_s).toFloat(), -6.02f, 0.05f);

    // Silence now: the level comes down at its own pace, not at once.
    snapshot.frame = 2;
    snapshot.blocks[1].peak[1] = 0.0f;
    snapshot.blocks[0].peak[0] = 0.0f;
    slot.store(snapshot);
    QThread::msleep(250);
    table.readLevels();
    const float fallen = table.rowAt(0).value(u"level"_s).toFloat();
    EXPECT_LT(fallen, -6.02f);
    EXPECT_GT(fallen, -6.02f - ui::SessionOverview::kFallDbPerSecond * 0.6f) << "it fell faster than a meter does";
}
