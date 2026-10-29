// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What a timer does when its time comes (FireTimer), and the list of every
// session's timers (ListTimers), against the mock engine.

#include "blocks/builtinblocks.h"
#include "core/editcommand.h"
#include "core/engine.h"
#include "core/enginefacade.h"
#include "core/mockengine.h"
#include "core/sessionstore.h"
#include "dsp/blockregistry.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <gtest/gtest.h>

using namespace rb;
using namespace rb::core;
using namespace Qt::StringLiterals;

namespace
{

void needApplication()
{
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char name[] = "reverblabr-engine-test";
        static char *argv[] = {name, nullptr};
        static QCoreApplication application(argc, argv);
    }
}

template <typename Predicate>
bool waitFor(Predicate predicate, int milliseconds = 5000)
{
    QElapsedTimer clock;
    clock.start();
    while (!predicate() && clock.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return predicate();
}

QJsonObject timer(const QString &id, int seconds, bool quit = false)
{
    return QJsonObject{{"id"_L1, id},
                       {"enabled"_L1, true},
                       {"repeat"_L1, u"daily"_s},
                       {"time"_L1, u"07:30"_s},
                       {"duration"_L1, seconds},
                       {"quitApplications"_L1, quit}};
}

QString setTimer(const QJsonObject &json)
{
    return QString::fromUtf8(QJsonDocument(editCommandToJson(SetTimer{json})).toJson(QJsonDocument::Compact));
}

/// The session's state, as the engine last said it.
QString lastState(const QSignalSpy &states, const QString &sessionId)
{
    for (qsizetype index = states.size() - 1; index >= 0; --index) {
        if (states.at(index).at(0).toString() == sessionId) {
            return states.at(index).at(1).toString();
        }
    }
    return {};
}

class TimerRunnerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        needApplication();
        engine.createSession(u"Radio"_s);
        ASSERT_EQ(opened.count(), 1);
        session = opened.at(0).at(0).toString();
    }

    MockEngine engine;
    QSignalSpy opened{&engine, &EngineFacade::sessionOpened};
    QSignalSpy states{&engine, &EngineFacade::stateChanged};
    QSignalSpy fired{&engine, &EngineFacade::timerFired};
    QSignalSpy errors{&engine, &EngineFacade::error};
    QString session;
};

} // namespace

// **A timer's whole run**: the session starts when it fires and stops when
// its duration is over, with no window and nobody asking.
TEST_F(TimerRunnerTest, ATimerStartsItsSessionAndStopsItWhenTheDurationIsOver)
{
    engine.applyEdit(session, setTimer(timer(u"t1"_s, 1)));
    engine.fireTimer(u"t1"_s);
    ASSERT_EQ(fired.count(), 1);
    EXPECT_EQ(fired.at(0).at(0).toString(), u"t1"_s);
    EXPECT_EQ(fired.at(0).at(1).toString(), session);
    EXPECT_EQ(lastState(states, session), QString(sessionState::running));

    QSignalSpy listed(&engine, &EngineFacade::timersListed);
    engine.listTimers();
    ASSERT_EQ(listed.count(), 1);
    const QJsonObject entry = QJsonDocument::fromJson(listed.at(0).at(0).toString().toUtf8()).array().at(0).toObject();
    EXPECT_TRUE(entry.contains("endsAt"_L1)) << "the list says until when it runs";

    ASSERT_TRUE(waitFor([&] { return lastState(states, session) == QString(sessionState::idle); }, 4000))
        << "the session should have stopped a second after it started";
    EXPECT_TRUE(engine.applicationsQuitFor().isEmpty()) << "the timer did not say to quit anything";
    EXPECT_TRUE(errors.isEmpty());
}

TEST_F(TimerRunnerTest, ATimerSetToQuitTheApplicationsAsksForThatWhenItEnds)
{
    engine.applyEdit(session, setTimer(timer(u"t1"_s, 1, true)));
    engine.fireTimer(u"t1"_s);
    EXPECT_TRUE(engine.applicationsQuitFor().isEmpty()) << "not while it runs";
    ASSERT_TRUE(waitFor([&] { return !engine.applicationsQuitFor().isEmpty(); }, 4000));
    EXPECT_EQ(engine.applicationsQuitFor(), QStringList{session});
    EXPECT_EQ(lastState(states, session), QString(sessionState::idle));
}

// **Stopped by somebody, the run is over**: starting the session again
// by hand is not the timer's to stop.
TEST_F(TimerRunnerTest, ASessionStoppedByHandIsNotStoppedAgainByTheTimer)
{
    engine.applyEdit(session, setTimer(timer(u"t1"_s, 1, true)));
    engine.fireTimer(u"t1"_s);
    engine.stop(session);
    engine.start(session);
    QCoreApplication::processEvents();
    ASSERT_FALSE(waitFor([&] { return lastState(states, session) != QString(sessionState::running); }, 1800));
    EXPECT_TRUE(engine.applicationsQuitFor().isEmpty());
}

TEST_F(TimerRunnerTest, ASessionAlreadyRunningIsLeftToWhoeverStartedIt)
{
    engine.applyEdit(session, setTimer(timer(u"t1"_s, 1)));
    engine.start(session);
    engine.fireTimer(u"t1"_s);
    ASSERT_EQ(fired.count(), 1);
    ASSERT_FALSE(waitFor([&] { return lastState(states, session) != QString(sessionState::running); }, 1800));
}

TEST_F(TimerRunnerTest, ATimerNoSessionHasIsRefusedAndNothingStarts)
{
    engine.fireTimer(u"nowhere"_s);
    EXPECT_TRUE(fired.isEmpty());
    ASSERT_EQ(errors.count(), 1);
    EXPECT_EQ(errors.at(0).at(0).toString(), u"timer.notFound"_s);
    EXPECT_EQ(QJsonDocument::fromJson(errors.at(0).at(1).toString().toUtf8()).object().value("timer"_L1).toString(),
              u"nowhere"_s);
    EXPECT_TRUE(lastState(states, session).isEmpty() || lastState(states, session) == QString(sessionState::idle));
}

// **Every session's timers with what systemd is told**, and when each
// next runs; a timer this version cannot read is not among them.
TEST_F(TimerRunnerTest, TheListSaysEachTimerWithItsSessionItsCalendarAndItsNextRun)
{
    engine.createSession(u"Call"_s);
    const QString call = opened.at(1).at(0).toString();
    engine.applyEdit(session, setTimer(timer(u"morning"_s, 3600)));
    QJsonObject once = timer(u"past"_s, 60);
    once.insert("repeat"_L1, u"once"_s);
    once.insert("date"_L1, u"2001-01-01"_s);
    engine.applyEdit(call, setTimer(once));

    QSignalSpy listed(&engine, &EngineFacade::timersListed);
    engine.listTimers();
    ASSERT_EQ(listed.count(), 1);
    const QJsonArray timers = QJsonDocument::fromJson(listed.at(0).at(0).toString().toUtf8()).array();
    ASSERT_EQ(timers.size(), 2);
    // In the order of the sessions, which the mock keeps by id.
    const auto byId = [&timers](const QString &id) {
        for (const QJsonValue &each : timers) {
            if (each.toObject().value("id"_L1).toString() == id) {
                return each.toObject();
            }
        }
        return QJsonObject{};
    };
    const QJsonObject morning = byId(u"morning"_s);
    EXPECT_EQ(morning.value("id"_L1).toString(), u"morning"_s);
    EXPECT_EQ(morning.value("sessionId"_L1).toString(), session);
    EXPECT_EQ(morning.value("sessionName"_L1).toString(), u"Radio"_s);
    EXPECT_EQ(morning.value("onCalendar"_L1).toString(), u"*-*-* 07:30:00"_s);
    EXPECT_EQ(morning.value("repeat"_L1).toString(), u"daily"_s);
    EXPECT_TRUE(morning.value("nextRun"_L1).isDouble());
    EXPECT_FALSE(morning.contains("endsAt"_L1));
    const QJsonObject past = byId(u"past"_s);
    EXPECT_EQ(past.value("sessionName"_L1).toString(), u"Call"_s);
    EXPECT_EQ(past.value("onCalendar"_L1).toString(), u"2001-01-01 07:30:00"_s);
    EXPECT_FALSE(past.contains("nextRun"_L1)) << "a one-off in the past never runs again";
}

// **A session only on disk is opened first**, then started, as
// `reverblabr-ctl start` does: the engine, with sessions in a folder of
// the test's own and no audio system, so the start is refused and the
// run ends there.
TEST(TimerRunnerEngine, ASessionOnlyOnDiskIsOpenedAndThenStarted)
{
    needApplication();
    QTemporaryDir sessions;
    ASSERT_TRUE(sessions.isValid());
    dsp::BlockRegistry registry;
    ASSERT_TRUE(blocks::registerBuiltinBlocks(registry));
    Engine engine(registry, SessionStore{sessions.path()});
    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy states(&engine, &EngineFacade::stateChanged);
    QSignalSpy fired(&engine, &EngineFacade::timerFired);

    engine.createSession(u"Radio"_s);
    ASSERT_EQ(opened.count(), 1);
    const QString session = opened.at(0).at(0).toString();
    engine.applyEdit(session, setTimer(timer(u"t1"_s, 60)));
    engine.saveSession(session);
    engine.closeSession(session);
    states.clear();

    engine.fireTimer(u"t1"_s);
    ASSERT_EQ(fired.count(), 1);
    EXPECT_EQ(fired.at(0).at(1).toString(), session);
    ASSERT_EQ(opened.count(), 2) << "opened again";
    EXPECT_EQ(opened.at(1).at(0).toString(), session);
    ASSERT_FALSE(states.isEmpty()) << "and asked to start";
    EXPECT_EQ(states.at(0).at(1).toString(), QString(sessionState::starting));
}
