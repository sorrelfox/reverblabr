// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What SessionsListed says of each session: where it stands, so a
// client never has to ask a session that is only on disk and hear
// session.notFound, which every window would show.

#include "blocks/builtinblocks.h"
#include "core/editcommand.h"
#include "core/engine.h"
#include "core/enginefacade.h"
#include "core/mockengine.h"
#include "core/sessionstore.h"
#include "dsp/blockregistry.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <map>

using namespace rb;
using namespace rb::core;
using namespace Qt::StringLiterals;

namespace
{

/// Each listed session's state, by name, from the last SessionsListed.
std::map<QString, QString> statesByName(const QSignalSpy &listed)
{
    std::map<QString, QString> states;
    const QJsonArray sessions = QJsonDocument::fromJson(listed.back().at(0).toString().toUtf8()).array();
    for (const QJsonValue &entry : sessions) {
        const QJsonObject session = entry.toObject();
        states.emplace(session.value("name"_L1).toString(), session.value("state"_L1).toString());
    }
    return states;
}

/// The recordings index is SQLite, which Qt only opens with an application.
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

/// The last SessionsListed's entry called \a name.
QJsonObject entryNamed(const QSignalSpy &listed, const QString &name)
{
    const QJsonArray sessions = QJsonDocument::fromJson(listed.back().at(0).toString().toUtf8()).array();
    for (const QJsonValue &entry : sessions) {
        if (entry.toObject().value("name"_L1).toString() == name) {
            return entry.toObject();
        }
    }
    return {};
}

/// An edit adding a block of \a type, called \a nickname.
QString addBlock(const QString &blockId, const QString &type, const QString &nickname = {})
{
    BlockInstance block;
    block.id = blockId;
    block.type = type;
    block.nickname = nickname;
    return QString::fromUtf8(QJsonDocument(editCommandToJson(AddBlock{block, -1})).toJson(QJsonDocument::Compact));
}

} // namespace

TEST(SessionList, ASessionOnlyOnDiskIsClosedAndAnOpenOneSaysItsState)
{
    QTemporaryDir sessions;
    ASSERT_TRUE(sessions.isValid());
    dsp::BlockRegistry registry;
    ASSERT_TRUE(blocks::registerBuiltinBlocks(registry));
    Engine engine(registry, SessionStore{sessions.path()});
    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy listed(&engine, &EngineFacade::sessionsListed);

    engine.createSession(u"Kept open"_s);
    engine.createSession(u"Put away"_s);
    ASSERT_EQ(opened.count(), 2);
    engine.closeSession(opened.at(1).at(0).toString());

    engine.listSessions();
    ASSERT_EQ(listed.count(), 1);
    std::map<QString, QString> states = statesByName(listed);
    EXPECT_EQ(states[u"Kept open"_s], QString(sessionState::idle));
    EXPECT_EQ(states[u"Put away"_s], QString(sessionState::closed));

    // Started with no audio system behind it, it cannot run: the list says
    // what the session says, not a word of its own.
    engine.start(opened.at(0).at(0).toString());
    engine.listSessions();
    states = statesByName(listed);
    EXPECT_EQ(states[u"Kept open"_s], QString(sessionState::error));
    EXPECT_EQ(states[u"Put away"_s], QString(sessionState::closed));
}

TEST(SessionList, TheMockListsEveryOneOfItsSessionsWithItsState)
{
    MockEngine engine;
    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy listed(&engine, &EngineFacade::sessionsListed);
    engine.createSession(u"Resting"_s);
    engine.createSession(u"Going"_s);
    ASSERT_EQ(opened.count(), 2);
    engine.start(opened.at(1).at(0).toString());

    engine.listSessions();
    ASSERT_EQ(listed.count(), 1);
    const std::map<QString, QString> states = statesByName(listed);
    EXPECT_EQ(states.at(u"Resting"_s), QString(sessionState::idle));
    EXPECT_EQ(states.at(u"Going"_s), QString(sessionState::running));
}

// **What a row of the Sessions window shows**, from the list alone:
// the sources in the order they sit, what they were called, and how many
// recordings. A session only on disk is described from its file, so nothing
// has to be opened (and put in a tab) to be shown.
TEST(SessionList, EachSessionSaysWhereItsSoundComesFromWithoutBeingOpened)
{
    QTemporaryDir sessions;
    ASSERT_TRUE(sessions.isValid());
    dsp::BlockRegistry registry;
    ASSERT_TRUE(blocks::registerBuiltinBlocks(registry));
    Engine engine(registry, SessionStore{sessions.path()});
    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy listed(&engine, &EngineFacade::sessionsListed);

    engine.createSession(u"Call"_s);
    ASSERT_EQ(opened.count(), 1);
    const QString id = opened.at(0).at(0).toString();
    engine.applyEdit(id, addBlock(u"b1"_s, u"source.application"_s, u"The call"_s));
    engine.applyEdit(id, addBlock(u"b2"_s, u"effect.volume"_s));
    engine.applyEdit(id, addBlock(u"b3"_s, u"source.device"_s));
    engine.saveSession(id);
    engine.closeSession(id);

    engine.listSessions();
    ASSERT_EQ(listed.count(), 1);
    const QJsonObject call = entryNamed(listed, u"Call"_s);
    EXPECT_EQ(call.value("state"_L1).toString(), QString(sessionState::closed));
    EXPECT_EQ(call.value("sources"_L1).toArray(),
              (QJsonArray{QJsonObject{{"type"_L1, u"source.application"_s}, {"name"_L1, u"The call"_s}},
                          QJsonObject{{"type"_L1, u"source.device"_s}, {"name"_L1, QString{}}}}));
    EXPECT_EQ(call.value("recordings"_L1).toInt(-1), 0);
    EXPECT_FALSE(call.contains("startedAt"_L1)) << "a session that is not running has no start time";
    EXPECT_EQ(call.value("autoRun"_L1).toString(), u"off"_s);
}

// **Whether a session starts at login is in the list**, from the file
// for one on disk and as it is now for an open one, so the background engine
// can find the sessions to start, and the Sessions window can show it,
// without opening any of them.
TEST(SessionList, EachSessionSaysWhetherItRunsAtLogin)
{
    QTemporaryDir sessions;
    ASSERT_TRUE(sessions.isValid());
    dsp::BlockRegistry registry;
    ASSERT_TRUE(blocks::registerBuiltinBlocks(registry));
    Engine engine(registry, SessionStore{sessions.path()});
    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy listed(&engine, &EngineFacade::sessionsListed);

    engine.createSession(u"Radio"_s);
    engine.createSession(u"Call"_s);
    ASSERT_EQ(opened.count(), 2);
    const QString radio = opened.at(0).at(0).toString();
    const QString call = opened.at(1).at(0).toString();
    const auto autoRunOn = QString::fromUtf8(
        QJsonDocument(
            QJsonObject{{"op"_L1, u"setSessionSetting"_s}, {"key"_L1, u"autoRun"_s}, {"value"_L1, u"login"_s}})
            .toJson(QJsonDocument::Compact));
    engine.applyEdit(radio, autoRunOn);
    engine.saveSession(radio);
    engine.closeSession(radio);
    engine.applyEdit(call, autoRunOn); // open, and not saved yet

    engine.listSessions();
    ASSERT_EQ(listed.count(), 1);
    EXPECT_EQ(entryNamed(listed, u"Radio"_s).value("autoRun"_L1).toString(), u"login"_s) << "from its file";
    EXPECT_EQ(entryNamed(listed, u"Call"_s).value("autoRun"_L1).toString(), u"login"_s) << "as it is now";
}

// **Each session's timers, as the file holds them**, from its file when it
// is closed and as it is now when it is open: what the background engine
// makes its systemd timers from.
TEST(SessionList, EachSessionSaysItsTimers)
{
    QTemporaryDir sessions;
    ASSERT_TRUE(sessions.isValid());
    dsp::BlockRegistry registry;
    ASSERT_TRUE(blocks::registerBuiltinBlocks(registry));
    Engine engine(registry, SessionStore{sessions.path()});
    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy listed(&engine, &EngineFacade::sessionsListed);

    engine.createSession(u"Radio"_s);
    engine.createSession(u"Call"_s);
    ASSERT_EQ(opened.count(), 2);
    const QString radio = opened.at(0).at(0).toString();
    const QJsonObject timer{{"id"_L1, u"morning"_s},
                            {"enabled"_L1, true},
                            {"repeat"_L1, u"weekdays"_s},
                            {"time"_L1, u"07:30"_s},
                            {"duration"_L1, 3600},
                            {"quitApplications"_L1, false}};
    engine.applyEdit(
        radio, QString::fromUtf8(QJsonDocument(editCommandToJson(SetTimer{timer})).toJson(QJsonDocument::Compact)));
    engine.saveSession(radio);
    engine.closeSession(radio);

    engine.listSessions();
    ASSERT_EQ(listed.count(), 1);
    const QJsonArray timers = entryNamed(listed, u"Radio"_s).value("timers"_L1).toArray();
    ASSERT_EQ(timers.size(), 1);
    EXPECT_EQ(timers.at(0).toObject(), timer);
    EXPECT_TRUE(entryNamed(listed, u"Call"_s).value("timers"_L1).toArray().isEmpty());
}

// **When it started is the engine's clock**, said while it runs and gone
// once it stops, so a window opened later says the same as one that watched
// it start. And the recordings are counted from the index.
TEST(SessionList, ARunningSessionSaysWhenItStartedAndARecordingIsCounted)
{
    needApplication();
    MockEngine engine;
    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy listed(&engine, &EngineFacade::sessionsListed);
    engine.createSession(u"Show"_s);
    ASSERT_EQ(opened.count(), 1);
    const QString id = opened.at(0).at(0).toString();
    ASSERT_FALSE(engine.addFakeRecording(id, u"Take.wav"_s, 0.2).isEmpty());
    engine.waitForFakeRecordings();

    const qint64 before = QDateTime::currentMSecsSinceEpoch();
    engine.start(id);
    const qint64 after = QDateTime::currentMSecsSinceEpoch();
    engine.listSessions();
    const QJsonObject running = entryNamed(listed, u"Show"_s);
    ASSERT_TRUE(running.contains("startedAt"_L1));
    const auto startedAt = static_cast<qint64>(running.value("startedAt"_L1).toDouble());
    EXPECT_GE(startedAt, before);
    EXPECT_LE(startedAt, after);
    EXPECT_EQ(running.value("recordings"_L1).toInt(-1), 1);
    EXPECT_EQ(running.value("sources"_L1).toArray(), QJsonArray{});

    engine.stop(id);
    engine.listSessions();
    EXPECT_FALSE(entryNamed(listed, u"Show"_s).contains("startedAt"_L1));
}
