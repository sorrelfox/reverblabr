// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What the global shortcuts are and what each one does, against the
// mock engine: no kglobalaccel, no bus and no window. Their registration
// with KGlobalAccel is tested on a private bus (tests/bus), and what the
// block shortcuts do to the front session's blocks in QML
// (tst_globalshortcuts.qml).

#include "appshell.h"
#include "core/editcommand.h"
#include "core/mockengine.h"
#include "globalshortcuts.h"
#include "windowstate.h"

#include <KSharedConfig>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <gtest/gtest.h>

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

[[nodiscard]] QString openSession(core::MockEngine &engine, const QString &name)
{
    QSignalSpy opened(&engine, &core::EngineFacade::sessionOpened);
    engine.createSession(name);
    return opened.isEmpty() ? QString{} : opened.at(0).at(0).toString();
}

[[nodiscard]] QString rename(const QString &name)
{
    return QString::fromUtf8(
        QJsonDocument(core::editCommandToJson(core::Rename{QString{}, name})).toJson(QJsonDocument::Compact));
}

/// The text of the action with \a id, or empty when there is none.
[[nodiscard]] QString textOf(const ui::GlobalShortcuts &shortcuts, const QString &id)
{
    for (const ui::GlobalShortcuts::Action &action : shortcuts.actions()) {
        if (action.id == id) {
            return action.text;
        }
    }
    return {};
}

[[nodiscard]] bool running(core::MockEngine &engine, const QString &sessionId)
{
    QSignalSpy state(&engine, &core::EngineFacade::stateChanged);
    engine.getSessionState(sessionId);
    return !state.isEmpty() && state.constLast().at(1).toString() == core::sessionState::running;
}

class GlobalShortcutsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // The sessions come from the Sessions window's own list, which reads
        // its columns from the state file: a file of the test's own.
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

TEST_F(GlobalShortcutsTest, TheFixedActionsComeFirstEachWithAnIdAndWords)
{
    core::MockEngine engine;
    ui::GlobalShortcuts shortcuts(&engine);
    const QList<ui::GlobalShortcuts::Action> fixed = ui::GlobalShortcuts::fixedActions();
    ASSERT_EQ(fixed.size(), 8);
    QSet<QString> ids;
    for (const ui::GlobalShortcuts::Action &action : fixed) {
        EXPECT_FALSE(action.text.isEmpty()) << action.id.toStdString();
        EXPECT_FALSE(action.id.startsWith(ui::GlobalShortcuts::kSessionPrefix)) << action.id.toStdString();
        ids.insert(action.id);
    }
    EXPECT_EQ(ids.size(), fixed.size()) << "two actions share an id";
    EXPECT_EQ(ui::GlobalShortcuts::sessionOf(QString(ui::GlobalShortcuts::kShowWindow)), QString{});
    EXPECT_EQ(textOf(shortcuts, QString(ui::GlobalShortcuts::kTimeShiftBack)), u"Time Shift: back 10 seconds"_s);
    // Before the sessions are listed, they are all there is.
    EXPECT_EQ(shortcuts.actions().mid(0, fixed.size()), fixed);
}

TEST_F(GlobalShortcutsTest, EverySavedSessionHasAnActionOfItsOwnThatKeepsItsIdWhenRenamed)
{
    core::MockEngine engine;
    const QString podcast = openSession(engine, u"Podcast"_s);
    const QString call = openSession(engine, u"Call"_s);
    ui::GlobalShortcuts shortcuts(&engine);
    ASSERT_TRUE(spinUntil([&] { return shortcuts.isReady(); }));

    const QString podcastAction = QString(ui::GlobalShortcuts::kSessionPrefix) + podcast;
    const QString callAction = QString(ui::GlobalShortcuts::kSessionPrefix) + call;
    EXPECT_EQ(shortcuts.actions().size(), ui::GlobalShortcuts::fixedActions().size() + 2);
    EXPECT_EQ(textOf(shortcuts, podcastAction), u"Run or stop “Podcast”"_s);
    EXPECT_EQ(textOf(shortcuts, callAction), u"Run or stop “Call”"_s);
    EXPECT_EQ(ui::GlobalShortcuts::sessionOf(podcastAction), podcast);

    // **Renamed, the same action**: the key set for it stays with it.
    QSignalSpy forgotten(&shortcuts, &ui::GlobalShortcuts::forgotten);
    engine.applyEdit(podcast, rename(u"Radio"_s));
    ASSERT_TRUE(spinUntil([&] { return textOf(shortcuts, podcastAction) == u"Run or stop “Radio”"_s; }))
        << textOf(shortcuts, podcastAction).toStdString();
    EXPECT_TRUE(forgotten.isEmpty()) << "a renamed session's key was forgotten";
}

TEST_F(GlobalShortcutsTest, ADeletedSessionsActionIsForgottenButNotWhenNothingIsLeft)
{
    core::MockEngine engine;
    const QString keep = openSession(engine, u"Keep"_s);
    const QString gone = openSession(engine, u"Gone"_s);
    ui::GlobalShortcuts shortcuts(&engine);
    ASSERT_TRUE(spinUntil([&] { return shortcuts.isReady() && shortcuts.actions().size() == 10; }));

    QSignalSpy forgotten(&shortcuts, &ui::GlobalShortcuts::forgotten);
    engine.deleteSession(gone);
    ASSERT_TRUE(spinUntil([&] { return !forgotten.isEmpty(); })) << "a deleted session's key was kept";
    EXPECT_EQ(forgotten.at(0).at(0).toString(), QString(ui::GlobalShortcuts::kSessionPrefix) + gone);
    EXPECT_TRUE(textOf(shortcuts, QString(ui::GlobalShortcuts::kSessionPrefix) + gone).isEmpty());

    // **An empty list forgets nothing**: it is as likely a folder that could
    // not be read, and every key would go with it.
    forgotten.clear();
    engine.deleteSession(keep);
    ASSERT_TRUE(spinUntil([&] { return shortcuts.actions().size() == 8; }));
    QCoreApplication::processEvents();
    EXPECT_TRUE(forgotten.isEmpty()) << "the last session's key went with an empty list";
}

TEST_F(GlobalShortcutsTest, ASessionsShortcutRunsItAndThenStopsIt)
{
    core::MockEngine engine;
    const QString session = openSession(engine, u"Evening"_s);
    ui::GlobalShortcuts shortcuts(&engine);
    ASSERT_TRUE(spinUntil([&] { return shortcuts.isReady(); }));

    const QString action = QString(ui::GlobalShortcuts::kSessionPrefix) + session;
    shortcuts.trigger(action);
    ASSERT_TRUE(spinUntil([&] { return running(engine, session); })) << "the shortcut did not run the session";
    // The list heard the same StateChanged, so the second press knows it
    // runs.
    shortcuts.trigger(action);
    ASSERT_TRUE(spinUntil([&] { return !running(engine, session); })) << "a second press did not stop it";
}

TEST_F(GlobalShortcutsTest, ShowAndTheBlockActionsArePassedOnAndAnythingElseIsNot)
{
    core::MockEngine engine;
    ui::GlobalShortcuts shortcuts(&engine);
    QSignalSpy show(&shortcuts, &ui::GlobalShortcuts::showRequested);
    QSignalSpy block(&shortcuts, &ui::GlobalShortcuts::blockActionRequested);

    shortcuts.trigger(QString(ui::GlobalShortcuts::kShowWindow));
    EXPECT_EQ(show.count(), 1);
    EXPECT_TRUE(block.isEmpty()) << "Show Reverblabr reached the blocks";

    QStringList passed;
    for (const ui::GlobalShortcuts::Action &action : ui::GlobalShortcuts::fixedActions()) {
        if (action.id != ui::GlobalShortcuts::kShowWindow) {
            shortcuts.trigger(action.id);
            passed.append(action.id);
        }
    }
    ASSERT_EQ(block.count(), passed.size());
    for (int i = 0; i < passed.size(); ++i) {
        EXPECT_EQ(block.at(i).at(0).toString(), passed.at(i));
    }

    block.clear();
    shortcuts.trigger(u"nothing.atAll"_s);
    shortcuts.trigger(QString(ui::GlobalShortcuts::kSessionPrefix) + u"no-such-session"_s);
    EXPECT_TRUE(block.isEmpty());
    EXPECT_EQ(show.count(), 1);
}
