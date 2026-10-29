// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// reverblabr-ctl against the daemon, every command run as a person
// or a script would run it: the built tool, as a process of its own, on a
// private session bus whose only service is the reverblabrd just built.
//
// **Only ever a private bus.** The test refuses to run on the real session
// bus, and ctest runs it inside dbus-run-session in a PID namespace of its
// own (tests/ctl/CMakeLists.txt), as bus.engine is. ctl.mock activates the
// mock engine, which can run sessions without PipeWire; ctl.engine
// activates the real one, which keeps sessions on disk, with no PipeWire
// behind it.

#include "bus/enginebus.h"
#include "engineclient.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDBusVirtualObject>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>
#include <gtest/gtest.h>
#include <signal.h>
#include <unistd.h>

#include <chrono>
#include <limits>
#include <memory>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace
{

template <typename Predicate>
bool waitFor(Predicate predicate, int milliseconds = 10000)
{
    QElapsedTimer clock;
    clock.start();
    while (!predicate() && clock.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return predicate();
}

/// kill(2) for the daemon this test had started, and nothing else: a pid of
/// 1 or below, or this test's own, is a failed test, never a signal (a
/// kill(-1) would reach every process the user owns).
[[nodiscard]] bool signalProcess(qint64 pid, int signal)
{
    if (pid <= 1 || pid > std::numeric_limits<pid_t>::max() || pid == ::getpid()) {
        ADD_FAILURE() << "refusing to signal pid " << pid;
        return false;
    }
    return ::kill(static_cast<pid_t>(pid), signal) == 0;
}

/// One run of the tool.
struct ToolRun {
    int exit = -1;
    QByteArray out;
    QByteArray err;

    [[nodiscard]] QJsonObject json() const
    {
        return QJsonDocument::fromJson(out).object();
    }
    [[nodiscard]] QStringList lines() const
    {
        return QString::fromUtf8(out).split(u'\n', Qt::SkipEmptyParts);
    }
    [[nodiscard]] QString errorCode() const
    {
        return json().value("error"_L1).toObject().value("code"_L1).toString();
    }
};

/// The keys of a JSON object, to hold an answer to the documented shape
/// (docs/ctl.md): **no more and no fewer**, since a field that appears
/// without being written down is one a script starts to rely on.
[[nodiscard]] QSet<QString> keysOf(const QJsonObject &object)
{
    const QStringList keys = object.keys();
    return {keys.begin(), keys.end()};
}

[[nodiscard]] QSet<QString> keys(std::initializer_list<const char *> names)
{
    QSet<QString> set;
    for (const char *name : names) {
        set.insert(QString::fromLatin1(name));
    }
    return set;
}

[[nodiscard]] ToolRun ctl(const QStringList &arguments,
                          const QProcessEnvironment &environment = QProcessEnvironment::systemEnvironment())
{
    QProcess process;
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral(RB_CTL), arguments);
    ToolRun run;
    if (!process.waitForStarted(5000) || !process.waitForFinished(30000)) {
        ADD_FAILURE() << "reverblabr-ctl did not run: " << process.errorString().toStdString();
        process.kill();
        process.waitForFinished(5000);
        return run;
    }
    run.exit = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    run.out = process.readAllStandardOutput();
    run.err = process.readAllStandardError();
    return run;
}

/// What the engine says on the bus, heard by the test as any client would
/// hear it, and the calls the tool itself has no command for (making a
/// session, adding a block).
class Listener : public QObject
{
    Q_OBJECT

public:
    explicit Listener(QDBusConnection connection)
        : m_connection(std::move(connection))
    {
        for (const char *name : {"SessionOpened", "SessionPatch", "UndoStateChanged", "Error"}) {
            m_connection.connect(QString(),
                                 QString(rb::bus::kObjectPath),
                                 QString(rb::bus::kInterfaceName),
                                 QString::fromLatin1(name),
                                 this,
                                 SLOT(heard(QDBusMessage)));
        }
    }

    [[nodiscard]] bool call(const QString &method, const QVariantList &arguments)
    {
        QDBusMessage message = QDBusMessage::createMethodCall(
            QString(rb::bus::kServiceName), QString(rb::bus::kObjectPath), QString(rb::bus::kInterfaceName), method);
        message.setArguments(arguments);
        return m_connection.call(message, QDBus::Block, 10000).type() == QDBusMessage::ReplyMessage;
    }

    /// Makes a session and says its id.
    [[nodiscard]] QString createSession(const QString &name)
    {
        const qsizetype before = m_heard.size();
        if (!call(u"CreateSession"_s, {name})) {
            return {};
        }
        QString id;
        (void)waitFor([&] {
            for (qsizetype index = before; index < m_heard.size(); ++index) {
                if (m_heard[index].member() == "SessionOpened"_L1) {
                    id = m_heard[index].arguments().value(0).toString();
                    return true;
                }
            }
            return false;
        });
        return id;
    }

    [[nodiscard]] bool
    addBlock(const QString &sessionId, const QString &blockId, const QString &type, const QString &nickname = {})
    {
        QJsonObject block{{"id"_L1, blockId}, {"type"_L1, type}, {"pos"_L1, QJsonArray{0, 0}}};
        if (!nickname.isEmpty()) {
            block.insert("nickname"_L1, nickname);
        }
        const QJsonObject edit{{"op"_L1, u"addBlock"_s}, {"block"_L1, block}};
        const qsizetype before = m_heard.size();
        if (!call(u"ApplyEdit"_s, {sessionId, QString::fromUtf8(QJsonDocument(edit).toJson(QJsonDocument::Compact))})) {
            return false;
        }
        return waitFor([&] {
            for (qsizetype index = before; index < m_heard.size(); ++index) {
                const QJsonObject patch =
                    QJsonDocument::fromJson(m_heard[index].arguments().value(2).toString().toUtf8()).object();
                if (m_heard[index].member() == "SessionPatch"_L1
                    && patch.value("block"_L1).toObject().value("id"_L1).toString() == blockId) {
                    return true;
                }
            }
            return false;
        });
    }

    /// Whether the window's Undo would take something back now.
    [[nodiscard]] bool canUndo(const QString &sessionId)
    {
        const qsizetype before = m_heard.size();
        if (!call(u"GetUndoState"_s, {sessionId})) {
            return false;
        }
        bool can = false;
        (void)waitFor([&] {
            for (qsizetype index = before; index < m_heard.size(); ++index) {
                if (m_heard[index].member() == "UndoStateChanged"_L1
                    && m_heard[index].arguments().value(0).toString() == sessionId) {
                    can = m_heard[index].arguments().value(1).toBool();
                    return true;
                }
            }
            return false;
        });
        return can;
    }

    /// Every Error the engine has sent while this test listened.
    [[nodiscard]] QStringList errors() const
    {
        QStringList codes;
        for (const QDBusMessage &message : m_heard) {
            if (message.member() == "Error"_L1) {
                codes.append(message.arguments().value(0).toString());
            }
        }
        return codes;
    }

private Q_SLOTS:
    void heard(const QDBusMessage &message)
    {
        m_heard.append(message);
    }

private:
    QDBusConnection m_connection;
    QList<QDBusMessage> m_heard;
};

/// Every test has a home of its own for the daemon the bus starts, and a
/// connection of its own to watch and prepare it with.
class CtlOnABus : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // **Refuse to run anywhere but a private bus**: the tool starts,
        // stops and changes sessions.
        const QByteArray address = qgetenv("DBUS_SESSION_BUS_ADDRESS");
        ASSERT_FALSE(address.isEmpty());
        ASSERT_FALSE(address.contains(QByteArray("/run/user/")))
            << "this is the real session bus; run the test through ctest";
        ASSERT_FALSE(qgetenv("PIPEWIRE_REMOTE").isEmpty()) << "a daemon here must not reach the real PipeWire";
        ASSERT_TRUE(home.isValid());
        for (const char *folder : {"data", "state", "config", "cache"}) {
            ASSERT_TRUE(QDir(home.path()).mkpath(QString::fromLatin1(folder)));
        }
        watch = QDBusConnection::connectToBus(QDBusConnection::SessionBus, u"watch"_s);
        ASSERT_TRUE(watch.isConnected());
        ASSERT_FALSE(daemonRunning()) << "a daemon was left from an earlier test";

        // The daemon the bus starts for this test keeps its sessions here.
        QDBusMessage update = QDBusMessage::createMethodCall(u"org.freedesktop.DBus"_s,
                                                             u"/org/freedesktop/DBus"_s,
                                                             u"org.freedesktop.DBus"_s,
                                                             u"UpdateActivationEnvironment"_s);
        QMap<QString, QString> environment;
        for (const char *folder : {"data", "state", "config", "cache"}) {
            environment.insert(u"XDG_%1_HOME"_s.arg(QString::fromLatin1(folder).toUpper()),
                               home.filePath(QString::fromLatin1(folder)));
        }
        update.setArguments({QVariant::fromValue(environment)});
        const QDBusMessage reply = watch.call(update);
        ASSERT_EQ(reply.type(), QDBusMessage::ReplyMessage) << reply.errorMessage().toStdString();

        listener = std::make_unique<Listener>(watch);
        engine = std::make_unique<rb::ctl::EngineClient>(watch, 10s);
    }

    void TearDown() override
    {
        engine.reset();
        listener.reset();
        if (const qint64 pid = daemonPid(); pid > 0 && signalProcess(pid, SIGTERM)) {
            if (!waitFor([this] { return !daemonRunning(); }, 5000)) {
                (void)signalProcess(pid, SIGKILL);
                (void)waitFor([this] { return !daemonRunning(); }, 5000);
            }
        }
        QDBusConnection::disconnectFromBus(u"watch"_s);
    }

    [[nodiscard]] bool daemonRunning() const
    {
        return watch.isConnected() && watch.interface()->isServiceRegistered(QString(rb::bus::kServiceName)).value();
    }

    [[nodiscard]] qint64 daemonPid() const
    {
        if (!watch.isConnected()) {
            return -1;
        }
        const QDBusReply<uint> pid = watch.interface()->servicePid(QString(rb::bus::kServiceName));
        if (!pid.isValid()) {
            return -1;
        }
        return static_cast<qint64>(pid.value());
    }

    /// A session, made through the bus as the window makes one.
    [[nodiscard]] QString session(const QString &name)
    {
        const QString id = listener->createSession(name);
        EXPECT_FALSE(id.isEmpty()) << "no session " << name.toStdString();
        return id;
    }

    /// A block's saved settings, as the engine holds them now.
    [[nodiscard]] QJsonObject paramsOf(const QString &sessionId, const QString &blockId)
    {
        const rb::ctl::Result<QJsonObject> snapshot = engine->snapshot(sessionId);
        EXPECT_TRUE(snapshot) << snapshot.error().code.toStdString();
        for (const QJsonValue &block : snapshot.value_or(QJsonObject{}).value("blocks"_L1).toArray()) {
            if (block.toObject().value("id"_L1).toString() == blockId) {
                return block.toObject().value("params"_L1).toObject();
            }
        }
        return {};
    }

    /// Each session's state, by name, as the engine lists them.
    [[nodiscard]] QMap<QString, QString> states()
    {
        QMap<QString, QString> byName;
        const rb::ctl::Result<QJsonArray> listed = engine->listSessions();
        EXPECT_TRUE(listed);
        for (const QJsonValue &entry : listed.value_or(QJsonArray{})) {
            byName.insert(entry.toObject().value("name"_L1).toString(), entry.toObject().value("state"_L1).toString());
        }
        return byName;
    }

    QTemporaryDir home;
    QDBusConnection watch{QString{}};
    std::unique_ptr<Listener> listener;
    std::unique_ptr<rb::ctl::EngineClient> engine;
};

using CtlMock = CtlOnABus;
using CtlEngine = CtlOnABus;

const QString kSpaced = u"Podcast — Mic + Call ☕"_s;

} // namespace

TEST_F(CtlMock, ListSaysEachSessionsNameOnALineAndInJsonItsIdAndState)
{
    const QString radio = session(u"Evening radio"_s);
    const QString podcast = session(kSpaced);

    const ToolRun plain = ctl({u"list"_s});
    ASSERT_EQ(plain.exit, 0) << plain.err.toStdString();
    const QStringList names = plain.lines();
    EXPECT_EQ(QSet<QString>(names.begin(), names.end()), (QSet<QString>{u"Evening radio"_s, kSpaced}));

    const ToolRun json = ctl({u"--json"_s, u"list"_s});
    ASSERT_EQ(json.exit, 0);
    EXPECT_EQ(keysOf(json.json()), keys({"sessions"}));
    const QJsonArray sessions = json.json().value("sessions"_L1).toArray();
    ASSERT_EQ(sessions.size(), 2);
    QSet<QString> ids;
    for (const QJsonValue &entry : sessions) {
        EXPECT_EQ(keysOf(entry.toObject()), keys({"id", "name", "state"}));
        EXPECT_EQ(entry.toObject().value("state"_L1).toString(), u"idle"_s);
        ids.insert(entry.toObject().value("id"_L1).toString());
    }
    EXPECT_EQ(ids, (QSet<QString>{radio, podcast}));
}

TEST_F(CtlMock, StartAndStopRunASessionNamedWithSpacesAndUnicode)
{
    const QString id = session(kSpaced);

    const ToolRun started = ctl({u"--json"_s, u"start"_s, kSpaced});
    ASSERT_EQ(started.exit, 0) << started.out.toStdString() << started.err.toStdString();
    EXPECT_EQ(keysOf(started.json()), keys({"session", "changed"}));
    EXPECT_EQ(keysOf(started.json().value("session"_L1).toObject()), keys({"id", "name", "state"}));
    EXPECT_EQ(started.json().value("session"_L1).toObject().value("id"_L1).toString(), id);
    EXPECT_EQ(started.json().value("session"_L1).toObject().value("state"_L1).toString(), u"running"_s);
    EXPECT_TRUE(started.json().value("changed"_L1).toBool());
    EXPECT_EQ(states().value(kSpaced), u"running"_s);

    // Already so is done, not a failure.
    const ToolRun again = ctl({u"--json"_s, u"start"_s, kSpaced});
    EXPECT_EQ(again.exit, 0);
    EXPECT_FALSE(again.json().value("changed"_L1).toBool(true));

    const ToolRun stopped = ctl({u"stop"_s, kSpaced});
    ASSERT_EQ(stopped.exit, 0) << stopped.err.toStdString();
    EXPECT_EQ(stopped.lines(), QStringList{u"Stopped “%1”."_s.arg(kSpaced)});
    EXPECT_EQ(states().value(kSpaced), u"idle"_s);

    const ToolRun stoppedAgain = ctl({u"--json"_s, u"stop"_s, id});
    EXPECT_EQ(stoppedAgain.exit, 0);
    EXPECT_FALSE(stoppedAgain.json().value("changed"_L1).toBool(true));
}

TEST_F(CtlMock, StatusSaysOneSessionsStateByNameOrById)
{
    const QString id = session(u"Evening radio"_s);
    const ToolRun byName = ctl({u"status"_s, u"evening radio"_s});
    ASSERT_EQ(byName.exit, 0) << byName.err.toStdString();
    EXPECT_EQ(byName.lines(), QStringList{u"Evening radio: stopped"_s});

    const ToolRun byId = ctl({u"--json"_s, u"status"_s, id});
    ASSERT_EQ(byId.exit, 0);
    EXPECT_EQ(keysOf(byId.json()), keys({"session"}));
    EXPECT_EQ(byId.json().value("session"_L1).toObject().value("name"_L1).toString(), u"Evening radio"_s);
}

TEST_F(CtlMock, SplitStartsTheNextFileInEachRecorderOfARunningSession)
{
    const QString id = session(u"Show"_s);
    ASSERT_TRUE(listener->addBlock(id, u"rec1"_s, u"output.recorder"_s, u"Programme"_s));
    ASSERT_TRUE(listener->addBlock(id, u"rec2"_s, u"output.recorder"_s));
    const QString quiet = session(u"Quiet"_s);

    // Not running: nothing to split.
    const ToolRun early = ctl({u"--json"_s, u"split"_s, u"Show"_s});
    EXPECT_EQ(early.exit, 1);
    EXPECT_EQ(early.errorCode(), u"ctl.notRunning"_s);

    ASSERT_EQ(ctl({u"start"_s, u"Show"_s}).exit, 0);
    const ToolRun all = ctl({u"--json"_s, u"split"_s, u"Show"_s});
    ASSERT_EQ(all.exit, 0) << all.out.toStdString() << all.err.toStdString();
    EXPECT_EQ(keysOf(all.json()), keys({"session", "split"}));
    const QJsonArray split = all.json().value("split"_L1).toArray();
    ASSERT_EQ(split.size(), 2);
    QSet<QString> blocks;
    for (const QJsonValue &entry : split) {
        EXPECT_EQ(keysOf(entry.toObject()), keys({"block", "file"}));
        EXPECT_FALSE(entry.toObject().value("file"_L1).toString().isEmpty());
        blocks.insert(entry.toObject().value("block"_L1).toString());
    }
    EXPECT_EQ(blocks, (QSet<QString>{u"rec1"_s, u"rec2"_s}));

    const ToolRun one = ctl({u"--json"_s, u"split"_s, u"Show"_s, u"programme"_s});
    ASSERT_EQ(one.exit, 0);
    ASSERT_EQ(one.json().value("split"_L1).toArray().size(), 1);
    EXPECT_EQ(one.json().value("split"_L1).toArray().at(0).toObject().value("block"_L1).toString(), u"rec1"_s);

    ASSERT_EQ(ctl({u"start"_s, quiet}).exit, 0);
    const ToolRun none = ctl({u"--json"_s, u"split"_s, u"Quiet"_s});
    EXPECT_EQ(none.exit, 3);
    EXPECT_EQ(none.errorCode(), u"ctl.blockNotFound"_s);
}

TEST_F(CtlMock, SwitchTurnsTheSessionsInputSwitchWhichNeedNotBeNamedWhenThereIsOne)
{
    const QString id = session(u"Talk"_s);
    ASSERT_TRUE(listener->addBlock(id, u"sw"_s, u"advanced.inputswitch"_s, u"Mic or line"_s));

    const ToolRun toB = ctl({u"--json"_s, u"switch"_s, u"Talk"_s, u"B"_s});
    ASSERT_EQ(toB.exit, 0) << toB.out.toStdString() << toB.err.toStdString();
    EXPECT_EQ(keysOf(toB.json()), keys({"session", "block", "param", "value", "input"}));
    EXPECT_EQ(toB.json().value("input"_L1).toString(), u"b"_s);
    EXPECT_EQ(paramsOf(id, u"sw"_s).value("input"_L1).toString(), u"b"_s);

    const ToolRun toA = ctl({u"switch"_s, u"Talk"_s, u"Mic or line"_s, u"a"_s});
    ASSERT_EQ(toA.exit, 0) << toA.err.toStdString();
    EXPECT_EQ(toA.lines(), QStringList{u"“Mic or line” now takes input A."_s});
    EXPECT_EQ(paramsOf(id, u"sw"_s).value("input"_L1).toString(), u"a"_s);

    const ToolRun toC = ctl({u"--json"_s, u"switch"_s, u"Talk"_s, u"C"_s});
    EXPECT_EQ(toC.exit, 2);
    EXPECT_EQ(toC.errorCode(), u"ctl.badValue"_s);

    ASSERT_TRUE(listener->addBlock(id, u"sw2"_s, u"advanced.inputswitch"_s));
    const ToolRun which = ctl({u"--json"_s, u"switch"_s, u"Talk"_s, u"B"_s});
    EXPECT_EQ(which.exit, 3);
    EXPECT_EQ(which.errorCode(), u"ctl.blockAmbiguous"_s);
}

TEST_F(CtlMock, SetChangesOneSettingAsAStepTheWindowCanUndo)
{
    const QString id = session(u"Voice"_s);
    ASSERT_TRUE(listener->addBlock(id, u"vol"_s, u"effect.volume"_s, u"Level"_s));

    const ToolRun set = ctl({u"--json"_s, u"set"_s, u"Voice"_s, u"Level"_s, u"gain"_s, u"0.5"_s});
    ASSERT_EQ(set.exit, 0) << set.out.toStdString() << set.err.toStdString();
    EXPECT_EQ(keysOf(set.json()), keys({"session", "block", "param", "value"}));
    EXPECT_EQ(set.json().value("value"_L1).toDouble(), 0.5);
    EXPECT_EQ(paramsOf(id, u"vol"_s).value("gain"_L1).toDouble(), 0.5);
    // **One step to undo**, as a change made in the window is: Undo takes
    // it back.
    EXPECT_TRUE(listener->canUndo(id));
    ASSERT_TRUE(listener->call(u"Undo"_s, {id}));
    EXPECT_TRUE(waitFor([&] { return paramsOf(id, u"vol"_s).value("gain"_L1).toDouble(-1.0) != 0.5; }))
        << "Undo did not take the setting back";
    ASSERT_EQ(ctl({u"set"_s, u"Voice"_s, u"Level"_s, u"gain"_s, u"0.5"_s}).exit, 0);

    const ToolRun tooMuch = ctl({u"--json"_s, u"set"_s, u"Voice"_s, u"vol"_s, u"gain"_s, u"99"_s});
    EXPECT_EQ(tooMuch.exit, 2);
    EXPECT_EQ(tooMuch.errorCode(), u"ctl.badValue"_s);
    EXPECT_EQ(paramsOf(id, u"vol"_s).value("gain"_L1).toDouble(), 0.5);

    const ToolRun noSuchSetting = ctl({u"--json"_s, u"set"_s, u"Voice"_s, u"vol"_s, u"loudness"_s, u"1"_s});
    EXPECT_EQ(noSuchSetting.exit, 3);
    EXPECT_EQ(noSuchSetting.errorCode(), u"ctl.paramNotFound"_s);

    const ToolRun noSuchBlock = ctl({u"set"_s, u"Voice"_s, u"Nobody"_s, u"gain"_s, u"1"_s});
    EXPECT_EQ(noSuchBlock.exit, 3);
    EXPECT_EQ(QString::fromUtf8(noSuchBlock.err), u"reverblabr-ctl: The session has no block called “Nobody”.\n"_s);
}

TEST_F(CtlMock, AMistypedNameOrCommandLineHasAnExitCodeOfItsOwn)
{
    (void)session(u"Twin"_s);
    (void)session(u"Twin"_s);

    const ToolRun nowhere = ctl({u"--json"_s, u"start"_s, u"Nowhere"_s});
    EXPECT_EQ(nowhere.exit, 3);
    EXPECT_EQ(keysOf(nowhere.json()), keys({"error"}));
    EXPECT_EQ(keysOf(nowhere.json().value("error"_L1).toObject()), keys({"code", "args"}));
    EXPECT_EQ(nowhere.errorCode(), u"ctl.sessionNotFound"_s);

    const ToolRun twins = ctl({u"--json"_s, u"start"_s, u"Twin"_s});
    EXPECT_EQ(twins.exit, 3);
    EXPECT_EQ(twins.errorCode(), u"ctl.sessionAmbiguous"_s);
    EXPECT_EQ(
        twins.json().value("error"_L1).toObject().value("args"_L1).toObject().value("matches"_L1).toArray().size(), 2);

    EXPECT_EQ(ctl({}).exit, 2);
    EXPECT_EQ(ctl({u"frobnicate"_s}).exit, 2);
    EXPECT_EQ(ctl({u"set"_s, u"Twin"_s}).exit, 2);
    EXPECT_EQ(ctl({u"--timeout"_s, u"soon"_s, u"list"_s}).exit, 2);
    EXPECT_EQ(ctl({u"--json"_s, u"--frobnicate"_s, u"list"_s}).errorCode(), u"ctl.usage"_s);
}

TEST_F(CtlMock, JsonIsTheSameInEveryLanguageAndAPersonReadsTheirOwn)
{
    (void)session(u"Evening radio"_s);
    ASSERT_EQ(ctl({u"start"_s, u"Evening radio"_s}).exit, 0);

    QProcessEnvironment english = QProcessEnvironment::systemEnvironment();
    english.insert(u"LANG"_s, u"C.UTF-8"_s);
    english.remove(u"LANGUAGE"_s);
    english.remove(u"LC_ALL"_s);
    QProcessEnvironment test = english;
    test.insert(u"LANG"_s, u"en_US.UTF-8"_s);
    test.insert(u"LANGUAGE"_s, u"xx"_s);
    test.insert(u"REVERBLABR_CTL_LOCALEDIR"_s, QStringLiteral(RB_CTL_TEST_LOCALEDIR));

    const ToolRun read = ctl({u"status"_s}, test);
    ASSERT_EQ(read.exit, 0);
    // The test catalog's words (tests/ctl/xx.po), not the English ones.
    EXPECT_EQ(read.lines(), QStringList{u"Evening radio → [running]"_s});

    const ToolRun scripted = ctl({u"--json"_s, u"status"_s}, test);
    const ToolRun scriptedInEnglish = ctl({u"--json"_s, u"status"_s}, english);
    ASSERT_EQ(scripted.exit, 0);
    EXPECT_EQ(scripted.out, scriptedInEnglish.out);
    EXPECT_TRUE(scripted.out.contains("\"running\""));
}

TEST_F(CtlEngine, ASessionOnlyOnDiskIsListedClosedWithoutAnErrorInAnyWindow)
{
    const QString id = session(u"On the shelf"_s);
    ASSERT_TRUE(engine->closeSession(id, u"On the shelf"_s));

    const ToolRun status = ctl({u"--json"_s, u"status"_s});
    ASSERT_EQ(status.exit, 0) << status.err.toStdString();
    ASSERT_EQ(status.json().value("sessions"_L1).toArray().size(), 1);
    EXPECT_EQ(status.json().value("sessions"_L1).toArray().at(0).toObject().value("state"_L1).toString(), u"closed"_s);
    ASSERT_EQ(ctl({u"status"_s, u"On the shelf"_s}).exit, 0);
    ASSERT_EQ(ctl({u"list"_s}).exit, 0);
    ASSERT_EQ(ctl({u"stop"_s, u"On the shelf"_s}).exit, 0);
    // Every window hears every Error: asking must raise none.
    QCoreApplication::processEvents();
    EXPECT_TRUE(listener->errors().isEmpty()) << listener->errors().join(u' ').toStdString();
}

TEST_F(CtlEngine, ASettingOfASessionOnlyOnDiskIsSavedAndTheSessionPutBack)
{
    const QString id = session(u"Archive"_s);
    ASSERT_TRUE(listener->addBlock(id, u"vol"_s, u"effect.volume"_s));
    ASSERT_TRUE(engine->closeSession(id, u"Archive"_s));

    const ToolRun set = ctl({u"set"_s, u"Archive"_s, u"vol"_s, u"gain"_s, u"0.25"_s});
    ASSERT_EQ(set.exit, 0) << set.err.toStdString();
    EXPECT_EQ(states().value(u"Archive"_s), u"closed"_s);

    ASSERT_TRUE(engine->openSession(id));
    EXPECT_EQ(paramsOf(id, u"vol"_s).value("gain"_L1).toDouble(), 0.25);
}

TEST_F(CtlEngine, ASessionThatCannotRunIsTheEnginesOwnRefusal)
{
    const QString id = session(u"No audio here"_s);
    ASSERT_TRUE(engine->closeSession(id, u"No audio here"_s));

    // No PipeWire behind this engine: the session opens and cannot start.
    const ToolRun start = ctl({u"--json"_s, u"start"_s, u"No audio here"_s});
    EXPECT_EQ(start.exit, 1) << start.out.toStdString();
    EXPECT_EQ(start.errorCode(), u"session.cannotRun"_s);
}

TEST_F(CtlEngine, WithoutActivationOnlyARunningEngineIsAsked)
{
    const ToolRun none = ctl({u"--json"_s, u"--no-activate"_s, u"list"_s});
    EXPECT_EQ(none.exit, 4);
    EXPECT_EQ(none.errorCode(), u"ctl.unreachable"_s);
    EXPECT_FALSE(daemonRunning()) << "--no-activate started the engine";

    (void)session(u"Started by the test"_s);
    const ToolRun some = ctl({u"--no-activate"_s, u"list"_s});
    EXPECT_EQ(some.exit, 0);
    EXPECT_EQ(some.lines(), QStringList{u"Started by the test"_s});
}

namespace
{

/// An engine that takes every call and answers none, as a stuck one would.
class Silent : public QDBusVirtualObject
{
public:
    QString introspect(const QString &) const override
    {
        return {};
    }
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        return connection.send(message.createReply());
    }
};

} // namespace

TEST_F(CtlEngine, AnEngineThatDoesNotAnswerIsATimeout)
{
    Silent silent;
    ASSERT_TRUE(watch.registerVirtualObject(QString(rb::bus::kObjectPath), &silent));
    ASSERT_TRUE(watch.registerService(QString(rb::bus::kServiceName)));

    const ToolRun run = ctl({u"--json"_s, u"--timeout"_s, u"1"_s, u"list"_s});
    EXPECT_EQ(run.exit, 5);
    EXPECT_EQ(run.errorCode(), u"ctl.timeout"_s);
    const ToolRun said = ctl({u"--timeout=0.5"_s, u"status"_s});
    EXPECT_EQ(QString::fromUtf8(said.err), u"reverblabr-ctl: The engine did not answer ListSessions in time.\n"_s);

    // Not the daemon: TearDown must find nobody to stop.
    ASSERT_TRUE(watch.unregisterService(QString(rb::bus::kServiceName)));
    watch.unregisterObject(QString(rb::bus::kObjectPath));
}

TEST(CtlReach, WithNoSessionBusThereIsNoEngineToAsk)
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(u"DBUS_SESSION_BUS_ADDRESS"_s,
                       u"unix:path=%1/no-such-bus"_s.arg(QStringLiteral(RB_CTL_TEST_DIR)));
    const ToolRun run = ctl({u"--json"_s, u"list"_s}, environment);
    EXPECT_EQ(run.exit, 4);
    EXPECT_EQ(run.errorCode(), u"ctl.unreachable"_s);
    EXPECT_EQ(run.json().value("error"_L1).toObject().value("args"_L1).toObject().value("reason"_L1).toString(),
              u"noSessionBus"_s);
}

TEST(CtlReach, AnEngineThatIsNotInstalledCannotBeStarted)
{
    // A bus of its own with no activation files, as on a machine with the
    // tool and not the engine.
    QProcess process;
    process.start(QStringLiteral(RB_DBUS_RUN_SESSION),
                  {u"--config-file=%1"_s.arg(QStringLiteral(RB_CTL_EMPTY_BUS_CONF)),
                   u"--"_s,
                   QStringLiteral(RB_CTL),
                   u"--json"_s,
                   u"list"_s});
    ASSERT_TRUE(process.waitForFinished(30000));
    const QJsonObject error =
        QJsonDocument::fromJson(process.readAllStandardOutput()).object().value("error"_L1).toObject();
    EXPECT_EQ(process.exitCode(), 4);
    EXPECT_EQ(error.value("code"_L1).toString(), u"ctl.unreachable"_s);
    EXPECT_EQ(error.value("args"_L1).toObject().value("reason"_L1).toString(),
              u"org.freedesktop.DBus.Error.ServiceUnknown"_s);
}

TEST(CtlReach, TheToolAsksTheEngineByTheEnginesOwnNames)
{
    EXPECT_EQ(QLatin1StringView(rb::ctl::kServiceName), rb::bus::kServiceName);
    EXPECT_EQ(QLatin1StringView(rb::ctl::kObjectPath), rb::bus::kObjectPath);
    EXPECT_EQ(QLatin1StringView(rb::ctl::kInterfaceName), rb::bus::kInterfaceName);
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    // For UpdateActivationEnvironment's a{ss}.
    qDBusRegisterMetaType<QMap<QString, QString>>();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

#include "test_ctl.moc"
