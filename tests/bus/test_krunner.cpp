// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The sessions in KRunner (ADR-0023): the runner the daemon serves,
// called the way KRunner calls it, on the same private bus and in the same
// PID namespace as bus.engine.
//
// **Never the real session bus**, where a query would reach the engine the
// person is using, and Open would start a window on their desktop. Every
// daemon here is started by this test, by hand or by a call, in a home of
// its own, and stopped before the bus goes.

#include "bus/enginebus.h"
#include "core/session.h"
#include "core/sessionstore.h"
#include "sessionrunner.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <gtest/gtest.h>
#include <signal.h>
#include <unistd.h>

#include <limits>
#include <memory>
#include <optional>
#include <vector>

using namespace rb;
using namespace Qt::StringLiterals;

namespace
{

/// Turns the event loop until \a predicate holds or \a milliseconds pass.
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

/// kill(2) for the daemon this test started, and nothing else, as in
/// test_enginebus.cpp: kill(0) signals the process group and kill(-1) every
/// process the user owns, so anything but an ordinary pid of another process
/// is a failed test, never a signal.
[[nodiscard]] bool signalProcess(qint64 pid, int signal)
{
    if (pid <= 1 || pid > std::numeric_limits<pid_t>::max() || pid == ::getpid()) {
        ADD_FAILURE() << "refusing to signal pid " << pid;
        return false;
    }
    return ::kill(static_cast<pid_t>(pid), signal) == 0;
}

[[nodiscard]] QStringList actionsOf(const daemon::RunnerMatch &match)
{
    return match.properties.value(u"actions"_s).toStringList();
}

class KRunner : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const QByteArray address = qgetenv("DBUS_SESSION_BUS_ADDRESS");
        ASSERT_FALSE(address.isEmpty());
        ASSERT_FALSE(address.contains(QByteArray("/run/user/")))
            << "this is the real session bus; run the test through ctest";
        ASSERT_FALSE(qgetenv("PIPEWIRE_REMOTE").isEmpty()) << "a daemon here must not reach the real PipeWire";

        ASSERT_TRUE(home.isValid());
        for (const char *folder : {"data", "state", "config", "cache", "runtime"}) {
            ASSERT_TRUE(QDir(home.path()).mkpath(QString::fromLatin1(folder)));
        }
        daemon::registerRunnerTypes();
        qDBusRegisterMetaType<QMap<QString, QString>>();
        bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, u"krunner"_s);
        ASSERT_TRUE(bus.isConnected());
        ASSERT_FALSE(daemonRunning()) << "a daemon was left from an earlier test";

        // A daemon the bus starts gets this home too.
        QDBusMessage update = QDBusMessage::createMethodCall(u"org.freedesktop.DBus"_s,
                                                             u"/org/freedesktop/DBus"_s,
                                                             u"org.freedesktop.DBus"_s,
                                                             u"UpdateActivationEnvironment"_s);
        QMap<QString, QString> environment;
        for (const auto &[variable, folder] : homeVariables()) {
            environment.insert(variable, folder);
        }
        update.setArguments({QVariant::fromValue(environment)});
        const QDBusMessage reply = bus.call(update);
        ASSERT_EQ(reply.type(), QDBusMessage::ReplyMessage) << reply.errorMessage().toStdString();
    }

    void TearDown() override
    {
        // **Nothing outlives the test**, and the daemon goes while the bus is
        // still there: without a PID namespace it would hold ctest's output
        // open until the timeout.
        if (const qint64 pid = daemonPid(); pid > 0 && signalProcess(pid, SIGTERM)) {
            if (!waitFor([this] { return !daemonRunning(); }, 5000)) {
                (void)signalProcess(pid, SIGKILL);
                (void)waitFor([this] { return !daemonRunning(); }, 5000);
            }
        }
        for (const std::unique_ptr<QProcess> &process : processes) {
            if (process->state() != QProcess::NotRunning) {
                process->kill();
                process->waitForFinished(5000);
            }
        }
        QDBusConnection::disconnectFromBus(u"krunner"_s);
    }

    [[nodiscard]] std::vector<std::pair<QString, QString>> homeVariables() const
    {
        return {
            {u"XDG_DATA_HOME"_s, home.filePath(u"data"_s)},
            {u"XDG_STATE_HOME"_s, home.filePath(u"state"_s)},
            {u"XDG_CONFIG_HOME"_s, home.filePath(u"config"_s)},
            {u"XDG_CACHE_HOME"_s, home.filePath(u"cache"_s)},
            // Where Auto Run notes that it has run in this login.
            {u"XDG_RUNTIME_DIR"_s, home.filePath(u"runtime"_s)},
        };
    }

    [[nodiscard]] bool daemonRunning() const
    {
        return bus.isConnected() && bus.interface()->isServiceRegistered(bus::kServiceName).value();
    }

    [[nodiscard]] qint64 daemonPid() const
    {
        if (!bus.isConnected()) {
            return -1;
        }
        const QDBusReply<uint> pid = bus.interface()->servicePid(bus::kServiceName);
        if (!pid.isValid()) {
            return -1;
        }
        return static_cast<qint64>(pid.value());
    }

    /// Starts reverblabrd with \a arguments, in this test's home, and waits
    /// until it has the name.
    void startDaemon(const QStringList &arguments)
    {
        auto process = std::make_unique<QProcess>();
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        for (const auto &[variable, folder] : homeVariables()) {
            environment.insert(variable, folder);
        }
        process->setProcessEnvironment(environment);
        process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
        process->start(QStringLiteral(RB_DAEMON), arguments);
        ASSERT_TRUE(process->waitForStarted(5000));
        processes.push_back(std::move(process));
        ASSERT_TRUE(waitFor([this] { return daemonRunning(); })) << "the daemon never took the name";
    }

    /// Calls \a method on the runner, as KRunner does: by the engine's name,
    /// so a call with no daemon running starts one.
    [[nodiscard]] QDBusMessage callRunner(const QString &method, const QVariantList &arguments = {})
    {
        QDBusMessage call =
            QDBusMessage::createMethodCall(bus::kServiceName, daemon::kRunnerPath, daemon::kRunnerInterface, method);
        call.setArguments(arguments);
        return bus.call(call, QDBus::Block, 20000);
    }

    /// Saves a session set to Auto Run, "Call microphone", for a query to
    /// start the engine on.
    void saveAutoRunSession()
    {
        core::Session saved;
        saved.name = u"Call microphone"_s;
        saved.settings.autoRun = QString::fromLatin1(core::autoRun::login);
        const core::SessionStore store(home.filePath(u"data/reverblabr/sessions"_s));
        ASSERT_TRUE(store.save(saved).has_value());
    }

    /// What the runner says "Call microphone" is doing, which starts the
    /// engine if it is not running. With no PipeWire here a session cannot
    /// run, so one Auto Run tried to start says "Could not run", and one
    /// only opened again says "Stopped".
    [[nodiscard]] QString callSaid()
    {
        return match(u"rb call"_s).value(0).properties.value(u"subtext"_s).toString();
    }

    /// Stops the running daemon with \a signal: SIGTERM for a clean exit,
    /// SIGKILL for a crash.
    void stopDaemon(int signal)
    {
        const qint64 pid = daemonPid();
        ASSERT_TRUE(signalProcess(pid, signal));
        ASSERT_TRUE(waitFor([this] { return !daemonRunning(); }, 5000));
    }

    [[nodiscard]] daemon::RunnerMatches match(const QString &query)
    {
        const QDBusMessage reply = callRunner(u"Match"_s, {query});
        EXPECT_EQ(reply.type(), QDBusMessage::ReplyMessage) << reply.errorMessage().toStdString();
        EXPECT_EQ(reply.signature(), u"a(sssida{sv})"_s) << "not what KRunner reads";
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            return {};
        }
        return qdbus_cast<daemon::RunnerMatches>(reply.arguments().constFirst());
    }

    void run(const QString &matchId, const QString &actionId = {})
    {
        const QDBusMessage reply = callRunner(u"Run"_s, {matchId, actionId});
        EXPECT_EQ(reply.type(), QDBusMessage::ReplyMessage) << reply.errorMessage().toStdString();
    }

    /// Calls \a method on the engine itself, as a window or a script does.
    void callEngine(const QString &method, const QVariantList &arguments)
    {
        QDBusMessage call =
            QDBusMessage::createMethodCall(bus::kServiceName, bus::kObjectPath, bus::kInterfaceName, method);
        call.setArguments(arguments);
        const QDBusMessage reply = bus.call(call);
        EXPECT_EQ(reply.type(), QDBusMessage::ReplyMessage) << reply.errorMessage().toStdString();
    }

    /// Makes a session called \a name in the daemon and returns its id, read
    /// back through the runner.
    [[nodiscard]] QString createSession(const QString &name)
    {
        callEngine(u"CreateSession"_s, {name});
        QString id;
        EXPECT_TRUE(waitFor([&] {
            for (const daemon::RunnerMatch &found : match(u"rb "_s + name)) {
                if (found.text.contains(name)) {
                    id = found.id;
                }
            }
            return !id.isEmpty();
        })) << "the runner never offered "
            << name.toStdString();
        return id;
    }

    /// The one match \a query gives whose text says \a text, or none.
    [[nodiscard]] std::optional<daemon::RunnerMatch> matchSaying(const QString &query, const QString &text)
    {
        for (const daemon::RunnerMatch &found : match(query)) {
            if (found.text == text) {
                return found;
            }
        }
        return std::nullopt;
    }

    QTemporaryDir home;
    QDBusConnection bus{QString{}};
    std::vector<std::unique_ptr<QProcess>> processes;
};

} // namespace

TEST_F(KRunner, AQueryStartsTheEngineAndFindsTheSessionsOnDisk)
{
    // A session on disk, and no engine running: as at login, before anybody
    // has opened Reverblabr.
    core::Session saved;
    saved.name = u"Evening radio"_s;
    const core::SessionStore store(home.filePath(u"data/reverblabr/sessions"_s));
    ASSERT_TRUE(store.save(saved).has_value());
    const QString id = saved.id.toString(QUuid::WithoutBraces);
    ASSERT_FALSE(daemonRunning());

    // **Only "rb" is ours**, whatever else is typed.
    EXPECT_TRUE(match(u"evening"_s).isEmpty());
    ASSERT_TRUE(daemonRunning()) << "KRunner's call did not start the engine";
    EXPECT_TRUE(match(u"rbevening"_s).isEmpty());

    const daemon::RunnerMatches found = match(u"rb evening"_s);
    ASSERT_EQ(found.size(), 1);
    EXPECT_EQ(found.first().id, id);
    EXPECT_EQ(found.first().text, u"Run “Evening radio”"_s);
    EXPECT_EQ(found.first().properties.value(u"subtext"_s).toString(), u"Stopped"_s);
    EXPECT_EQ(actionsOf(found.first()), QStringList{u"open"_s});
    EXPECT_EQ(found.first().iconName, u"io.github.reverblabr.Reverblabr"_s);
    // The whole name stands above a part of it, and any case will do.
    EXPECT_GT(match(u"RB Evening Radio"_s).value(0).categoryRelevance, found.first().categoryRelevance);

    // **Run on a session only on disk** opens it and runs it. With no
    // PipeWire here it cannot, which is how the test sees that it tried.
    run(id);
    EXPECT_TRUE(waitFor([&] {
        return match(u"rb evening"_s).value(0).properties.value(u"subtext"_s).toString() == u"Could not run"_s;
    })) << "the session was not opened and run";
}

TEST_F(KRunner, AQueryDoesNotKeepTheEngineRunning)
{
    ASSERT_NO_FATAL_FAILURE(startDaemon({u"--mock"_s, u"--idle-exit"_s, u"2"_s}));
    EXPECT_TRUE(match(u"rb"_s).isEmpty()) << "the mock has no sessions until one is made";
    // **KRunner stays on the bus for the whole login**; if its queries
    // made it a client, an engine it started would never exit.
    EXPECT_TRUE(waitFor([this] { return !daemonRunning(); }, 8000))
        << "the engine kept running for nobody after a query";
}

TEST_F(KRunner, TheMatchRunsAndStopsTheSessionAndRecRecordingIsSaid)
{
    ASSERT_NO_FATAL_FAILURE(startDaemon({u"--mock"_s, u"--idle-exit"_s, u"0"_s}));
    const QString podcast = createSession(u"Podcast"_s);
    const QString radio = createSession(u"Evening radio"_s);
    ASSERT_FALSE(podcast.isEmpty());
    ASSERT_FALSE(radio.isEmpty());
    callEngine(
        u"ApplyEdit"_s,
        {podcast, u"{\"op\":\"addBlock\",\"block\":{\"id\":\"rec\",\"type\":\"output.recorder\",\"pos\":[0,0]}}"_s});

    // "rb" alone lists every session; a part of a name, the ones it is in.
    EXPECT_EQ(match(u"rb"_s).size(), 2);
    EXPECT_EQ(match(u"rb  "_s).size(), 2);
    ASSERT_EQ(match(u"rb pod"_s).size(), 1);
    EXPECT_EQ(match(u"rb RADIO"_s).value(0).id, radio);

    EXPECT_TRUE(matchSaying(u"rb rec"_s, u"Nothing is being recorded"_s).has_value());

    run(podcast);
    ASSERT_TRUE(waitFor([&] { return matchSaying(u"rb podcast"_s, u"Stop “Podcast”"_s).has_value(); }))
        << "the match did not run the session";
    EXPECT_EQ(matchSaying(u"rb podcast"_s, u"Stop “Podcast”"_s)->properties.value(u"subtext"_s).toString(),
              u"Running and recording"_s);
    // **"rb rec" and every longer start of "recording"** say where.
    for (const QString &query : {u"rb rec"_s, u"rb Recording"_s}) {
        const std::optional<daemon::RunnerMatch> recording = matchSaying(query, u"Recording in “Podcast”"_s);
        ASSERT_TRUE(recording.has_value()) << query.toStdString();
        EXPECT_EQ(recording->id, u"recording/"_s + podcast);
        EXPECT_TRUE(actionsOf(*recording).isEmpty());
    }
    EXPECT_FALSE(matchSaying(u"rb re"_s, u"Recording in “Podcast”"_s).has_value()) << "two letters are a name";

    callEngine(u"ControlRecording"_s, {podcast, u"rec"_s, u"pause"_s});
    EXPECT_TRUE(waitFor([&] { return matchSaying(u"rb rec"_s, u"Recording paused in “Podcast”"_s).has_value(); }));

    run(podcast);
    ASSERT_TRUE(waitFor([&] { return matchSaying(u"rb podcast"_s, u"Run “Podcast”"_s).has_value(); }))
        << "the match did not stop the session";
    EXPECT_TRUE(matchSaying(u"rb rec"_s, u"Nothing is being recorded"_s).has_value());
    EXPECT_EQ(matchSaying(u"rb radio"_s, u"Run “Evening radio”"_s)->properties.value(u"subtext"_s).toString(),
              u"Stopped"_s)
        << "the other session was touched";
}

TEST_F(KRunner, OpenStartsTheWindowOnTheSessionWithKRunnersToken)
{
    // **A window that writes down how it was started**, in place of the
    // real one, which would come up on the desktop.
    const QString said = home.filePath(u"window-said"_s);
    const QString window = home.filePath(u"window.sh"_s);
    {
        QFile script(window);
        ASSERT_TRUE(script.open(QIODevice::WriteOnly));
        script.write(QStringLiteral("#!/bin/sh\nprintf '%s\\n' \"$*\" \"${XDG_ACTIVATION_TOKEN:-none}\" > \"%1.part\"\n"
                                    "mv \"%1.part\" \"%1\"\n")
                         .arg(said)
                         .toUtf8());
        script.close();
        ASSERT_TRUE(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    }
    ASSERT_NO_FATAL_FAILURE(startDaemon({u"--mock"_s, u"--idle-exit"_s, u"0"_s, u"--window"_s, window}));
    const QString podcast = createSession(u"Podcast"_s);
    ASSERT_FALSE(podcast.isEmpty());

    const QDBusMessage actions = callRunner(u"Actions"_s);
    ASSERT_EQ(actions.type(), QDBusMessage::ReplyMessage) << actions.errorMessage().toStdString();
    EXPECT_EQ(actions.signature(), u"a(sss)"_s);
    const auto offered = qdbus_cast<daemon::RunnerActions>(actions.arguments().constFirst());
    ASSERT_EQ(offered.size(), 1);
    EXPECT_EQ(offered.first().id, u"open"_s);
    EXPECT_EQ(offered.first().text, u"Open in Reverblabr"_s);

    const auto windowSaid = [&said]() -> QStringList {
        QFile file(said);
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QString::fromUtf8(file.readAll()).split(u'\n', Qt::SkipEmptyParts);
    };

    // KRunner hands the token over just before Run, on Wayland.
    EXPECT_EQ(callRunner(u"SetActivationToken"_s, {u"krunner-token"_s}).type(), QDBusMessage::ReplyMessage);
    run(podcast, u"open"_s);
    ASSERT_TRUE(waitFor([&] { return !windowSaid().isEmpty(); })) << "the window was not started";
    EXPECT_EQ(windowSaid(), (QStringList{u"--open-session="_s + podcast, u"krunner-token"_s}));
    EXPECT_TRUE(matchSaying(u"rb podcast"_s, u"Run “Podcast”"_s).has_value()) << "Open ran the session";

    // **A token is for one launch**: the next one has none to give.
    ASSERT_TRUE(QFile::remove(said));
    run(u"recording/"_s + podcast);
    ASSERT_TRUE(waitFor([&] { return !windowSaid().isEmpty(); })) << "a recording's match did not open its session";
    EXPECT_EQ(windowSaid(), (QStringList{u"--open-session="_s + podcast, u"none"_s}));

    // **Only a session's id reaches the window's command line**: anything
    // else KRunner hands back starts nothing, and uses up the token.
    ASSERT_TRUE(QFile::remove(said));
    EXPECT_EQ(callRunner(u"SetActivationToken"_s, {u"krunner-token"_s}).type(), QDBusMessage::ReplyMessage);
    run(u"--new-session"_s, u"open"_s);
    run(u"recording/--sessions"_s);
    run(podcast); // runs it, and takes the token with it
    ASSERT_TRUE(waitFor([&] { return matchSaying(u"rb podcast"_s, u"Stop “Podcast”"_s).has_value(); }));
    run(podcast, u"open"_s);
    ASSERT_TRUE(waitFor([&] { return !windowSaid().isEmpty(); }));
    EXPECT_EQ(windowSaid(), (QStringList{u"--open-session="_s + podcast, u"none"_s}))
        << "something that is not a session's id started the window, or a token outlived its Run";
}

TEST_F(KRunner, ARenameOrANewSessionShowsAtTheNextKeystroke)
{
    ASSERT_NO_FATAL_FAILURE(startDaemon({u"--mock"_s, u"--idle-exit"_s, u"0"_s}));
    const QString podcast = createSession(u"Podcast"_s);
    ASSERT_FALSE(podcast.isEmpty());
    ASSERT_EQ(match(u"rb"_s).size(), 1);
    // **Kept between keystrokes, and forgotten when it changes**: well
    // within the time a list is otherwise kept for.
    callEngine(u"ApplyEdit"_s, {podcast, u"{\"op\":\"rename\",\"name\":\"Late show\"}"_s});
    EXPECT_TRUE(matchSaying(u"rb late"_s, u"Run “Late show”"_s).has_value()) << "the rename did not show";
    callEngine(u"CreateSession"_s, {u"Morning"_s});
    EXPECT_EQ(match(u"rb"_s).size(), 2) << "the new session did not show";
}

// **The default, once a login** (ADR-0023): with nothing chosen in the
// settings, a session the person stopped stays stopped until the next login.
TEST_F(KRunner, AutoRunRunsOnceALoginAndAgainOnlyAfterACrash)
{
    ASSERT_NO_FATAL_FAILURE(saveAutoRunSession());

    // **The first engine of the login** starts it, whatever started the
    // engine: here, a KRunner query.
    EXPECT_EQ(callSaid(), u"Could not run"_s) << "the first engine of the login did not run it";
    // **Started again after a clean exit**, as after an idle minute: the
    // session is opened again, and left stopped.
    ASSERT_NO_FATAL_FAILURE(stopDaemon(SIGTERM));
    EXPECT_EQ(callSaid(), u"Stopped"_s) << "a KRunner query ran an Auto Run session again";
    // **After a crash** it runs again, as Restart=on-failure promises.
    ASSERT_NO_FATAL_FAILURE(stopDaemon(SIGKILL));
    EXPECT_EQ(callSaid(), u"Could not run"_s) << "a crash restart did not run it";
}

// **Every engine start**, when the person chose it in the Settings menu:
// a clean exit and a query that starts the engine again run the session
// again, as before ADR-0023, and a crash still does.
TEST_F(KRunner, AutoRunRunsAtEveryEngineStartWhenThatIsChosen)
{
    ASSERT_NO_FATAL_FAILURE(saveAutoRunSession());
    // As the window writes it (KConfig), in the test's own config home.
    QFile settings(home.filePath(u"config/reverblabrrc"_s));
    ASSERT_TRUE(settings.open(QIODevice::WriteOnly));
    ASSERT_GT(settings.write("[AutoRun]\nStart=every-engine-start\n"), 0);
    settings.close();

    EXPECT_EQ(callSaid(), u"Could not run"_s) << "the first engine of the login did not run it";
    ASSERT_NO_FATAL_FAILURE(stopDaemon(SIGTERM));
    EXPECT_EQ(callSaid(), u"Could not run"_s) << "an engine started after a clean exit did not run it";
    ASSERT_NO_FATAL_FAILURE(stopDaemon(SIGKILL));
    EXPECT_EQ(callSaid(), u"Could not run"_s) << "a crash restart did not run it";

    // **Read as the engine starts**: back to once a login, the next start
    // after a clean exit leaves it stopped, from the note the engines kept
    // all along.
    ASSERT_TRUE(settings.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_GT(settings.write("[AutoRun]\nStart=once-a-login\n"), 0);
    settings.close();
    ASSERT_NO_FATAL_FAILURE(stopDaemon(SIGTERM));
    EXPECT_EQ(callSaid(), u"Stopped"_s) << "the choice of once a login did not hold from the next start";
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
