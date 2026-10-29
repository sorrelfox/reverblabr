// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The engine on the bus (ADR-0017): the bridge both ways, and the
// daemon behind it — started by a call, outliving the window, back after a
// crash with its sessions stopped, and gone once nobody uses it.
//
// **Never the real session bus.** The whole binary runs inside
// dbus-run-session with a configuration whose only service directory is one
// in the build tree (CMakeLists.txt), so what starts on a call is the
// reverblabrd just built, and nothing is installed or enabled for systemd.
// The daemons it starts have no PipeWire to reach (PIPEWIRE_REMOTE names a
// socket that is not there) and a home of their own under /tmp.

#include "bus/enginebus.h"
#include "bus/engineservice.h"
#include "bus/remoteengine.h"
#include "core/enginefacade.h"
#include "core/mockengine.h"
#include "core/sessionstore.h"
#include "core/sharedtelemetry.h"
#include "recordingfacade.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <gtest/gtest.h>
#include <signal.h>
#include <unistd.h>

#include <functional>
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

/// Turns the event loop for \a milliseconds, whatever happens.
void idle(int milliseconds)
{
    (void)waitFor([] { return false; }, milliseconds);
}

/// kill(2) for one process this test started, and nothing else. **A pid of
/// 0 or -1 is not a process**: kill(0) signals the whole process group and
/// kill(-1) every process the user owns, which is the desktop session; a
/// pid that has lost its sign on the way here (a uint -1 is 4294967295) is
/// the same thing. So anything but an ordinary pid of another process is a
/// failed test, never a signal.
[[nodiscard]] bool signalProcess(qint64 pid, int signal)
{
    if (pid <= 1 || pid > std::numeric_limits<pid_t>::max() || pid == ::getpid()) {
        ADD_FAILURE() << "refusing to signal pid " << pid;
        return false;
    }
    return ::kill(static_cast<pid_t>(pid), signal) == 0;
}

/// How many descriptors process \a pid has open.
[[nodiscard]] int descriptorsOf(qint64 pid)
{
    return static_cast<int>(QDir(u"/proc/%1/fd"_s.arg(pid)).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).size());
}

/// How many telemetry mappings this process has (ADR-0018).
[[nodiscard]] qsizetype telemetryMappings()
{
    QFile maps(u"/proc/self/maps"_s);
    if (!maps.open(QIODevice::ReadOnly)) {
        return -1;
    }
    return maps.readAll().count("reverblabr-telemetry");
}

/// A value of \a type for argument \a index, different from every other
/// argument's, so an argument that lands in the wrong place shows.
[[nodiscard]] QVariant sampleOf(QMetaType type, int index)
{
    switch (type.id()) {
    case QMetaType::QString:
        return u"argument %1 ⟨ü⟩"_s.arg(index);
    case QMetaType::Bool:
        return index % 2 == 0;
    case QMetaType::Int:
        return -1000 - index;
    case QMetaType::Double:
        return 0.25 + index;
    case QMetaType::ULongLong:
        return QVariant::fromValue<quint64>(5'000'000'000ULL + static_cast<quint64>(index));
    default:
        ADD_FAILURE() << "no sample for a parameter of type " << type.name();
        return {};
    }
}

[[nodiscard]] QVariantList samplesFor(const QMetaMethod &method)
{
    QVariantList arguments;
    for (int index = 0; index < method.parameterCount(); ++index) {
        arguments.append(sampleOf(method.parameterMetaType(index), index));
    }
    return arguments;
}

/// The facade's own members of \a kind, as the bridge serves them.
[[nodiscard]] std::vector<QMetaMethod> facadeMembers(QMetaMethod::MethodType kind)
{
    std::vector<QMetaMethod> members;
    const QMetaObject &meta = core::EngineFacade::staticMetaObject;
    for (int index = meta.methodOffset(); index < meta.methodCount(); ++index) {
        if (meta.method(index).methodType() == kind) {
            members.push_back(meta.method(index));
        }
    }
    return members;
}

/// Every test has a bus connection of its own for each party, and a home of
/// its own for any daemon it starts.
class EngineBus : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // **Refuse to run anywhere but a private bus.** Everything here
        // starts and kills engines by their bus name.
        const QByteArray address = qgetenv("DBUS_SESSION_BUS_ADDRESS");
        ASSERT_FALSE(address.isEmpty());
        ASSERT_FALSE(address.contains(QByteArray("/run/user/")))
            << "this is the real session bus; run the test through ctest";
        ASSERT_FALSE(qgetenv("PIPEWIRE_REMOTE").isEmpty()) << "a daemon here must not reach the real PipeWire";

        ASSERT_TRUE(home.isValid());
        for (const char *folder : {"data", "state", "config", "cache"}) {
            ASSERT_TRUE(QDir(home.path()).mkpath(QString::fromLatin1(folder)));
        }
        // Not one of connections: TearDown still asks it for the daemon
        // after closing those.
        watch = QDBusConnection::connectToBus(QDBusConnection::SessionBus, u"watch"_s);
        ASSERT_TRUE(watch.isConnected());
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
        const QDBusMessage reply = watch.call(update);
        ASSERT_EQ(reply.type(), QDBusMessage::ReplyMessage) << reply.errorMessage().toStdString();
    }

    void TearDown() override
    {
        engines.clear();
        for (const QString &name : std::as_const(connections)) {
            QDBusConnection::disconnectFromBus(name);
        }
        // **Nothing outlives the test**: a daemon it started, by hand or by
        // calling, is stopped, and killed if it will not stop.
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
        QDBusConnection::disconnectFromBus(u"watch"_s);
    }

    [[nodiscard]] std::vector<std::pair<QString, QString>> homeVariables() const
    {
        return {
            {u"XDG_DATA_HOME"_s, home.filePath(u"data"_s)},
            {u"XDG_STATE_HOME"_s, home.filePath(u"state"_s)},
            {u"XDG_CONFIG_HOME"_s, home.filePath(u"config"_s)},
            {u"XDG_CACHE_HOME"_s, home.filePath(u"cache"_s)},
        };
    }

    /// A connection of its own, as another process would have: a unique
    /// name that leaves the bus when it is closed.
    [[nodiscard]] QDBusConnection open(const QString &name)
    {
        connections.append(name);
        return QDBusConnection::connectToBus(QDBusConnection::SessionBus, name);
    }

    /// Closes a connection opened by open(), as a process that quits does.
    void close(const QString &name)
    {
        QDBusConnection::disconnectFromBus(name);
        connections.removeAll(name);
    }

    /// A window's engine, on a connection of its own.
    bus::RemoteEngine *window(const QString &name)
    {
        engines.push_back(std::make_unique<bus::RemoteEngine>(open(name)));
        return engines.back().get();
    }

    [[nodiscard]] bool daemonRunning() const
    {
        return watch.isConnected() && watch.interface()->isServiceRegistered(bus::kServiceName).value();
    }

    /// The daemon's pid, or -1 when nobody has the name.
    [[nodiscard]] qint64 daemonPid() const
    {
        if (!watch.isConnected()) {
            return -1;
        }
        const QDBusReply<uint> pid = watch.interface()->servicePid(bus::kServiceName);
        // **Not `pid.isValid() ? pid.value() : -1`**: that expression is a
        // uint, and its -1 arrives as 4294967295, a pid kill() reads as -1.
        if (!pid.isValid()) {
            return -1;
        }
        return static_cast<qint64>(pid.value());
    }

    /// Starts reverblabrd with \a arguments, in this test's home, and waits
    /// until it has the name.
    QProcess *startDaemon(const QStringList &arguments)
    {
        auto process = std::make_unique<QProcess>();
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        for (const auto &[variable, folder] : homeVariables()) {
            environment.insert(variable, folder);
        }
        process->setProcessEnvironment(environment);
        process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
        process->start(QStringLiteral(RB_DAEMON), arguments);
        EXPECT_TRUE(process->waitForStarted(5000));
        EXPECT_TRUE(waitFor([this] { return daemonRunning(); })) << "the daemon never took the name";
        processes.push_back(std::move(process));
        return processes.back().get();
    }

    /// The sessions the daemon has written down as open.
    [[nodiscard]] QStringList writtenDown() const
    {
        QFile file(home.filePath(u"state/reverblabr/daemon.json"_s));
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        QStringList ids;
        for (const QJsonValue &id : QJsonDocument::fromJson(file.readAll()).object().value("open"_L1).toArray()) {
            ids.append(id.toString());
        }
        return ids;
    }

    QTemporaryDir home;
    QDBusConnection watch{QString{}};
    QStringList connections;
    std::vector<std::unique_ptr<bus::RemoteEngine>> engines;
    std::vector<std::unique_ptr<QProcess>> processes;
};

} // namespace

TEST_F(EngineBus, EveryMethodReachesTheEngineWithItsArgumentsInOrder)
{
    test::RecordingFacade engine;
    bus::EngineService service(&engine, open(u"service"_s));
    ASSERT_TRUE(service.publish());
    bus::RemoteEngine *remote = window(u"window"_s);

    const std::vector<QMetaMethod> methods = facadeMembers(QMetaMethod::Slot);
    ASSERT_GE(methods.size(), 40U);
    std::vector<std::pair<QString, QVariantList>> expected;
    for (const QMetaMethod &method : methods) {
        QVariantList arguments = samplesFor(method);
        expected.emplace_back(QString::fromLatin1(method.name()), arguments);
        bus::invoke(remote, method, arguments);
    }

    ASSERT_TRUE(waitFor([&] { return engine.calls.size() >= expected.size(); }))
        << engine.calls.size() << " of " << expected.size() << " calls arrived";
    ASSERT_EQ(engine.calls.size(), expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        EXPECT_EQ(engine.calls[index].first, expected[index].first);
        EXPECT_EQ(engine.calls[index].second, expected[index].second) << expected[index].first.toStdString();
    }
}

TEST_F(EngineBus, EverySignalComesBackAsTheFacadeSignalOfTheSameName)
{
    test::RecordingFacade engine;
    bus::EngineService service(&engine, open(u"service"_s));
    ASSERT_TRUE(service.publish());
    bus::RemoteEngine *remote = window(u"window"_s);
    // The match rules are in place once the bus has answered a call.
    remote->listSessions();
    ASSERT_TRUE(waitFor([&] { return !engine.calls.empty(); }));

    const std::vector<QMetaMethod> signalList = facadeMembers(QMetaMethod::Signal);
    ASSERT_GE(signalList.size(), 20U);
    for (const QMetaMethod &signal : signalList) {
        QSignalSpy spy(remote, signal);
        ASSERT_TRUE(spy.isValid());
        QVariantList arguments = samplesFor(signal);
        bus::invoke(&engine, signal, arguments);
        ASSERT_TRUE(waitFor([&] { return spy.count() > 0; })) << signal.name().toStdString() << " never came back";
        EXPECT_EQ(spy.count(), 1);
        EXPECT_EQ(spy.at(0), arguments) << signal.name().toStdString();
    }
}

TEST_F(EngineBus, ANewDaemonIsAskedForWhatLastsAsItWasLastAsked)
{
    // Two engines in this process, one after the other on the name, as a
    // daemon and the one that replaces it.
    auto first = std::make_unique<test::RecordingFacade>();
    auto firstService = std::make_unique<bus::EngineService>(first.get(), open(u"first"_s));
    ASSERT_TRUE(firstService->publish());
    bus::RemoteEngine *remote = window(u"window"_s);
    QSignalSpy restarted(remote, &bus::RemoteEngine::restarted);

    remote->listPlugins();
    remote->analyse(u"s"_s, u"a"_s, core::kBothChannels);
    remote->analyse(u"s"_s, u"b"_s, 0);
    remote->analyse(u"s"_s, u"a"_s, 1);
    remote->analyse(u"t"_s, u"c"_s, core::kBothChannels);
    remote->rescanPlugins(true);
    remote->describePlugin(u"lv2"_s, u"urn:x"_s);
    remote->describePlugin(u"lv2"_s, u"urn:x"_s);
    ASSERT_TRUE(waitFor([&] { return first->calls.size() == 8; })) << first->calls.size() << " calls arrived";

    // Taken over before the window's revive would start a real daemon.
    firstService.reset();
    close(u"first"_s);
    test::RecordingFacade second;
    bus::EngineService secondService(&second, open(u"second"_s));
    ASSERT_TRUE(secondService.publish());
    ASSERT_TRUE(waitFor([&] { return restarted.count() > 0; })) << "the window never saw the new daemon";
    idle(300);

    // **Each session's spectrum from the block it was last asked for**, of
    // the channel it was last asked for, the
    // plugins listed and a plugin described again (once), and a rescan not
    // repeated.
    using Calls = std::vector<std::pair<QString, QVariantList>>;
    EXPECT_EQ(second.calls,
              (Calls{
                  {u"listPlugins"_s, {}},
                  {u"analyse"_s, {u"s"_s, u"a"_s, 1}},
                  {u"analyse"_s, {u"t"_s, u"c"_s, core::kBothChannels}},
                  {u"describePlugin"_s, {u"lv2"_s, u"urn:x"_s}},
              }));
}

TEST_F(EngineBus, OnlyTheFacadeIsServed)
{
    test::RecordingFacade engine;
    bus::EngineService service(&engine, open(u"service"_s));
    ASSERT_TRUE(service.publish());
    const QDBusConnection caller = open(u"caller"_s);

    const auto callFails = [&](const QString &member, const QVariantList &arguments, QDBusError::ErrorType error) {
        QDBusMessage call =
            QDBusMessage::createMethodCall(bus::kServiceName, bus::kObjectPath, bus::kInterfaceName, member);
        call.setArguments(arguments);
        QDBusPendingCall pending = caller.asyncCall(call);
        EXPECT_TRUE(waitFor([&] { return pending.isFinished(); })) << member.toStdString();
        return pending.isError() && pending.error().type() == error;
    };
    // A slot of the object that is not the interface's.
    EXPECT_TRUE(callFails(u"NotOnTheBus"_s, {}, QDBusError::UnknownMethod));
    // One of the interface's, with the wrong number of arguments: not its
    // published signature (ADR-0020).
    EXPECT_TRUE(callFails(u"CreateSession"_s, {u"a"_s, u"b"_s}, QDBusError::InvalidArgs));
    // QObject's own.
    EXPECT_TRUE(callFails(u"DeleteLater"_s, {}, QDBusError::UnknownMethod));
    idle(100);
    EXPECT_TRUE(engine.calls.empty()) << engine.calls.front().first.toStdString() << " was called";

    // What introspection says is the interface's XML. **Asked without
    // blocking**: the service answers on this thread, which a blocking call
    // would keep from turning until the call had timed out.
    QDBusPendingReply<QString> introspection = caller.asyncCall(QDBusMessage::createMethodCall(
        bus::kServiceName, bus::kObjectPath, u"org.freedesktop.DBus.Introspectable"_s, u"Introspect"_s));
    ASSERT_TRUE(waitFor([&] { return introspection.isFinished(); }));
    ASSERT_TRUE(introspection.isValid()) << introspection.error().message().toStdString();
    EXPECT_TRUE(introspection.value().contains(u"<method name=\"CreateSession\">"_s));
}

TEST_F(EngineBus, TheFirstCallStartsTheDaemon)
{
    ASSERT_FALSE(daemonRunning());
    bus::RemoteEngine *remote = window(u"window"_s);
    EXPECT_FALSE(remote->isConnected());
    QSignalSpy connected(remote, &bus::RemoteEngine::connected);
    QSignalSpy listed(remote, &core::EngineFacade::sessionsListed);

    remote->listSessions();

    EXPECT_TRUE(waitFor([&] { return listed.count() > 0; })) << "nothing answered";
    EXPECT_TRUE(daemonRunning());
    EXPECT_TRUE(remote->isConnected());
    EXPECT_EQ(connected.count(), 1);
}

TEST_F(EngineBus, KillingTheWindowDoesNotStopTheSession)
{
    // **The mock engine**, since there is no PipeWire here to run a real
    // session in; what is under test is who decides it stops.
    QProcess *daemon = startDaemon({u"--mock"_s, u"--idle-exit"_s, u"1"_s});
    const qint64 pid = daemonPid();
    // Kept from leaving while the window starts, which may take longer
    // than the second it waits.
    bus::RemoteEngine *keeper = window(u"keeper"_s);
    QSignalSpy listed(keeper, &core::EngineFacade::sessionsListed);
    keeper->listSessions();
    ASSERT_TRUE(waitFor([&] { return listed.count() > 0; }));

    QProcess client;
    client.setProcessChannelMode(QProcess::SeparateChannels);
    client.start(QStringLiteral(RB_BUS_CLIENT), {u"Outlives the window"_s});
    ASSERT_TRUE(client.waitForStarted(5000));
    QString sessionId;
    ASSERT_TRUE(waitFor([&] {
        const QString line = QString::fromUtf8(client.readLine()).trimmed();
        if (line.startsWith("running "_L1)) {
            sessionId = line.mid(8);
        }
        return !sessionId.isEmpty();
    })) << "the window never had a running session";
    // Now the session is all that keeps it.
    engines.clear();
    close(u"keeper"_s);

    ASSERT_TRUE(signalProcess(client.processId(), SIGKILL));
    ASSERT_TRUE(client.waitForFinished(5000));
    // Longer than the daemon waits when nobody is using it.
    idle(2500);

    EXPECT_EQ(daemon->state(), QProcess::Running) << "the daemon went with the window";
    EXPECT_EQ(daemonPid(), pid);
    // **A window opened afterwards finds it running**, and gets a tab for it.
    bus::RemoteEngine *next = window(u"next window"_s);
    QSignalSpy opened(next, &core::EngineFacade::sessionOpened);
    QSignalSpy states(next, &core::EngineFacade::stateChanged);
    next->openSession(sessionId);
    next->getSessionState(sessionId);
    ASSERT_TRUE(waitFor([&] { return states.count() > 0; }));
    EXPECT_EQ(states.at(0), (QVariantList{sessionId, QString{core::sessionState::running}}));
    EXPECT_EQ(opened.count(), 1);
}

TEST_F(EngineBus, TheDaemonLeavesOnceNobodyUsesItAndNothingRuns)
{
    QProcess *daemon = startDaemon({u"--mock"_s, u"--idle-exit"_s, u"1"_s});
    bus::RemoteEngine *remote = window(u"window"_s);
    QSignalSpy listed(remote, &core::EngineFacade::sessionsListed);
    remote->listSessions();
    ASSERT_TRUE(waitFor([&] { return listed.count() > 0; }));

    // A window that is open keeps it, doing nothing or not.
    idle(2000);
    EXPECT_EQ(daemon->state(), QProcess::Running) << "the daemon left a window that was still open";

    engines.clear();
    close(u"window"_s);
    EXPECT_TRUE(daemon->waitForFinished(5000)) << "the daemon stayed with nobody using it";
    EXPECT_EQ(daemon->exitStatus(), QProcess::NormalExit);
    EXPECT_EQ(daemon->exitCode(), 0);
}

TEST_F(EngineBus, AfterACrashTheWindowIsBackWithItsSessionsStopped)
{
    // Started by the bus, as in use: the real engine, which keeps its
    // sessions on disk, with no PipeWire behind it.
    bus::RemoteEngine *remote = window(u"window"_s);
    QSignalSpy opened(remote, &core::EngineFacade::sessionOpened);
    QSignalSpy snapshots(remote, &core::EngineFacade::sessionSnapshot);
    QSignalSpy states(remote, &core::EngineFacade::stateChanged);
    QSignalSpy disconnected(remote, &bus::RemoteEngine::disconnected);
    QSignalSpy restarted(remote, &bus::RemoteEngine::restarted);
    remote->createSession(u"Survives a crash"_s);
    ASSERT_TRUE(waitFor([&] { return opened.count() > 0 && snapshots.count() > 0; }));
    const QString sessionId = opened.at(0).at(0).toString();
    EXPECT_EQ(writtenDown(), QStringList{sessionId});
    const qint64 first = daemonPid();
    ASSERT_GT(first, 0);
    snapshots.clear();
    states.clear();

    ASSERT_TRUE(signalProcess(first, SIGKILL));

    ASSERT_TRUE(waitFor([&] { return restarted.count() > 0; })) << "the window never came back";
    EXPECT_EQ(disconnected.count(), 1);
    EXPECT_NE(daemonPid(), first);
    // **The same session, stopped**, without the window being told it has
    // a second tab for it.
    ASSERT_TRUE(waitFor([&] { return snapshots.count() > 0 && states.count() > 0; }));
    EXPECT_EQ(snapshots.at(0).at(0).toString(), sessionId);
    EXPECT_EQ(states.at(0), (QVariantList{sessionId, QString{core::sessionState::idle}}));
    idle(300);
    EXPECT_EQ(opened.count(), 1);
    EXPECT_EQ(restarted.count(), 1);
    EXPECT_EQ(writtenDown(), QStringList{sessionId});
}

TEST_F(EngineBus, TheDaemonKeepsSessionsWhereTheWindowDoes)
{
    // **One set of sessions**, whichever process has the engine: a window
    // that moves to the daemon finds what it made in-process, and the
    // other way round. The engine's own paths are the application's
    // (`$XDG_DATA_HOME/reverblabr/...`), so the daemon has to be it.
    startDaemon({u"--idle-exit"_s, u"0"_s});
    bus::RemoteEngine *remote = window(u"window"_s);
    QSignalSpy opened(remote, &core::EngineFacade::sessionOpened);
    remote->createSession(u"Where"_s);
    ASSERT_TRUE(waitFor([&] { return opened.count() > 0; }));
    const QString sessionId = opened.at(0).at(0).toString();
    remote->saveSession(sessionId);

    const QString expected = home.filePath(u"data/reverblabr/sessions/%1%2"_s.arg(sessionId, core::kSessionFileSuffix));
    EXPECT_TRUE(waitFor([&] { return QFile::exists(expected); })) << "not in " << expected.toStdString();
    EXPECT_FALSE(QDir(home.filePath(u"data/reverblabrd"_s)).exists()) << "the daemon keeps data of its own";
}

TEST_F(EngineBus, ADaemonOpensWhatWasOpenUnlessInSafeMode)
{
    // A session made and left open, then the daemon stopped as systemd
    // stops it.
    QProcess *daemon = startDaemon({u"--idle-exit"_s, u"0"_s});
    bus::RemoteEngine *maker = window(u"maker"_s);
    QSignalSpy made(maker, &core::EngineFacade::sessionOpened);
    maker->createSession(u"Opened again"_s);
    ASSERT_TRUE(waitFor([&] { return made.count() > 0; }));
    const QString sessionId = made.at(0).at(0).toString();
    engines.clear();
    close(u"maker"_s);
    daemon->terminate();
    ASSERT_TRUE(daemon->waitForFinished(5000));
    EXPECT_EQ(daemon->exitCode(), 0);
    ASSERT_TRUE(waitFor([this] { return !daemonRunning(); }));
    EXPECT_EQ(writtenDown(), QStringList{sessionId}) << "stopping the daemon forgot what was open";

    // Somebody listening, who has asked for nothing.
    bus::RemoteEngine *listener = window(u"listener"_s);
    QSignalSpy opened(listener, &core::EngineFacade::sessionOpened);

    daemon = startDaemon({u"--idle-exit"_s, u"0"_s, u"--safe-mode"_s});
    idle(1000);
    EXPECT_EQ(opened.count(), 0) << "safe mode opened a session";
    daemon->terminate();
    ASSERT_TRUE(daemon->waitForFinished(5000));
    ASSERT_TRUE(waitFor([this] { return !daemonRunning(); }));
    EXPECT_EQ(writtenDown(), QStringList{sessionId}) << "safe mode forgot what was open";

    daemon = startDaemon({u"--idle-exit"_s, u"0"_s});
    ASSERT_TRUE(waitFor([&] { return opened.count() > 0; })) << "the daemon did not open what was open";
    EXPECT_EQ(opened.at(0).at(0).toString(), sessionId);
}

TEST_F(EngineBus, TelemetryIsSharedBesideTheFacadeAndOnlyForWhatIsOpen)
{
    test::RecordingFacade engine;
    bus::EngineService service(&engine, open(u"service"_s));
    core::PublishedSlot published;
    service.setTelemetry(
        [&](const QString &sessionId) { return sessionId == "open"_L1 ? published.shareReadOnly() : -1; });
    ASSERT_TRUE(service.publish());
    const QDBusConnection caller = open(u"caller"_s);
    const auto share = [&](const QVariantList &arguments) {
        QDBusMessage call = QDBusMessage::createMethodCall(
            bus::kServiceName, bus::kObjectPath, bus::kTelemetryInterfaceName, u"Share"_s);
        call.setArguments(arguments);
        QDBusPendingCall pending = caller.asyncCall(call);
        EXPECT_TRUE(waitFor([&] { return pending.isFinished(); }));
        return pending;
    };

    const QDBusPendingReply<QDBusUnixFileDescriptor> shared = share({u"open"_s});
    ASSERT_FALSE(shared.isError()) << shared.error().message().toStdString();
    auto mapped = core::SharedTelemetry::map(::dup(shared.value().fileDescriptor()));
    ASSERT_TRUE(mapped.has_value());
    core::TelemetrySnapshot snapshot;
    snapshot.frame = 5;
    published.slot().store(snapshot);
    EXPECT_EQ(mapped->slot().load().frame, 5U) << "not the memory the session publishes in";

    EXPECT_EQ(share({u"closed"_s}).error().name(), bus::kNoTelemetryError);
    EXPECT_EQ(share({}).error().type(), QDBusError::UnknownMethod);
    EXPECT_EQ(share({42}).error().type(), QDBusError::UnknownMethod);
    idle(100);
    EXPECT_TRUE(engine.calls.empty()) << "the facade was called for telemetry";

    QDBusPendingReply<QString> introspection = caller.asyncCall(QDBusMessage::createMethodCall(
        bus::kServiceName, bus::kObjectPath, u"org.freedesktop.DBus.Introspectable"_s, u"Introspect"_s));
    ASSERT_TRUE(waitFor([&] { return introspection.isFinished(); }));
    EXPECT_TRUE(introspection.value().contains(u"<interface name=\"io.github.reverblabr.Telemetry\">"_s));
    EXPECT_TRUE(introspection.value().contains(u"<method name=\"CreateSession\">"_s));
}

TEST_F(EngineBus, AWindowOnTheDaemonReadsTheAnalysedBlocksBandValuesAndSoundBeforeIt)
{
    // Across the bus: the analysed block's 64 band values, the spectrum
    // before it and the ear it was asked of, in the memory the window maps.
    // **The engine behind the service is the mock**, in this process: there
    // is no PipeWire in this sandbox for the real one, and what is under
    // test is the way from the engine's memory to the window's, which is
    // the same bytes whichever engine wrote them.
    core::MockEngine engine;
    bus::EngineService service(&engine, open(u"service"_s));
    service.setTelemetry([&](const QString &sessionId) { return engine.shareTelemetry(sessionId); });
    ASSERT_TRUE(service.publish());
    bus::RemoteEngine *remote = window(u"window"_s);
    QSignalSpy opened(remote, &core::EngineFacade::sessionOpened);
    QSignalSpy moved(remote, &bus::RemoteEngine::telemetryChanged);
    remote->createSession(u"Hearing"_s);
    ASSERT_TRUE(waitFor([&] { return opened.count() > 0; }));
    const QString sessionId = opened.at(0).at(0).toString();

    QVariantList values;
    for (int band = 0; band < static_cast<int>(rt::kMaxBandValues); ++band) {
        values.append(-30.0 + 0.5 * band);
    }
    engine.setFakeBandValues(sessionId, u"hc"_s, values);
    engine.setFakeInputSpectrum(sessionId, u"hc"_s, QVariantList(core::kSpectrumBands, -20.0));
    (void)remote->telemetryOf(sessionId);
    ASSERT_TRUE(waitFor([&] { return moved.count() > 0; })) << "the memory was never mapped";
    const core::TelemetrySlot *slot = remote->telemetryOf(sessionId);
    ASSERT_NE(slot, nullptr);

    remote->analyse(sessionId, u"hc"_s, 1);
    remote->start(sessionId);
    ASSERT_TRUE(waitFor([&] {
        const core::TelemetrySnapshot snapshot = slot->load();
        return snapshot.running && snapshot.bandValueCount == rt::kMaxBandValues;
    })) << "the band values never reached the window";
    const core::TelemetrySnapshot snapshot = slot->load();
    EXPECT_STREQ(snapshot.analysedId, "hc");
    EXPECT_EQ(snapshot.analysedChannel, 1) << "the ear was lost on the way";
    for (std::size_t band = 0; band < rt::kMaxBandValues; ++band) {
        EXPECT_FLOAT_EQ(snapshot.bandValues[band], -30.0f + 0.5f * static_cast<float>(band)) << band;
    }
    ASSERT_EQ(snapshot.inputSpectrumBands, core::kSpectrumBands);
    EXPECT_FLOAT_EQ(snapshot.inputSpectrum[40], -20.0f);

    // Nobody analysed: none of it.
    remote->analyse(sessionId, QString{}, core::kBothChannels);
    ASSERT_TRUE(waitFor([&] {
        const core::TelemetrySnapshot now = slot->load();
        return now.bandValueCount == 0 && now.inputSpectrumBands == 0 && now.analysedId[0] == '\0';
    })) << "the band values stayed after nobody asked";
    remote->stop(sessionId);
}

TEST_F(EngineBus, TheWindowReadsTheDaemonsLevelsWithNoMessageAFrame)
{
    // **The mock daemon publishes sixty snapshots a second** while a
    // session runs, as the real sampler does, with no PipeWire to need.
    (void)startDaemon({u"--mock"_s, u"--idle-exit"_s, u"0"_s});
    bus::RemoteEngine *remote = window(u"window"_s);
    QSignalSpy opened(remote, &core::EngineFacade::sessionOpened);
    QSignalSpy moved(remote, &bus::RemoteEngine::telemetryChanged);
    EXPECT_EQ(remote->telemetryOf(u"not open"_s), nullptr);
    remote->createSession(u"Levels"_s);
    ASSERT_TRUE(waitFor([&] { return opened.count() > 0; }));
    const QString sessionId = opened.at(0).at(0).toString();
    (void)remote->telemetryOf(sessionId);
    ASSERT_TRUE(waitFor([&] { return moved.count() > 0; })) << "the memory was never mapped";
    const core::TelemetrySlot *slot = remote->telemetryOf(sessionId);
    ASSERT_NE(slot, nullptr);
    remote->start(sessionId);
    ASSERT_TRUE(waitFor([&] { return slot->load().running; })) << "the daemon's snapshots never arrived";

    // **Watched with dbus-monitor**, the check a shared-memory transport
    // calls for: every message on this bus, while the window reads a frame
    // every 16 ms for a second.
    QProcess monitor;
    monitor.setProcessChannelMode(QProcess::SeparateChannels);
    monitor.start(u"dbus-monitor"_s, {u"--session"_s, u"--profile"_s});
    ASSERT_TRUE(monitor.waitForStarted(5000));
    idle(500);
    (void)monitor.readAllStandardOutput();
    const std::uint64_t first = slot->load().frame;
    QElapsedTimer second;
    second.start();
    std::uint64_t reads = 0;
    while (second.elapsed() < 1000) {
        // **As the window reads**: asking where, then reading there.
        const core::TelemetrySlot *now = remote->telemetryOf(sessionId);
        reads += now != nullptr && now->load().running ? 1 : 0;
        idle(16);
    }
    const std::uint64_t last = slot->load().frame;
    idle(200);
    const QByteArray traffic = monitor.readAllStandardOutput();
    monitor.kill();
    monitor.waitForFinished(5000);

    EXPECT_GE(last - first, 30U) << "the snapshots did not keep coming";
    EXPECT_GE(reads, 30U);
    qsizetype messages = 0;
    for (const QByteArray &line : traffic.split('\n')) {
        messages +=
            line.startsWith("sig") || line.startsWith("mc") || line.startsWith("mr") || line.startsWith("err") ? 1 : 0;
    }
    EXPECT_EQ(messages, 0) << traffic.toStdString();
}

TEST_F(EngineBus, NoMappingOrDescriptorOutlivesTheWindowThatHadIt)
{
    (void)startDaemon({u"--mock"_s, u"--idle-exit"_s, u"0"_s});
    bus::RemoteEngine *maker = window(u"maker"_s);
    QSignalSpy made(maker, &core::EngineFacade::sessionOpened);
    maker->createSession(u"Reconnected"_s);
    ASSERT_TRUE(waitFor([&] { return made.count() > 0; }));
    const QString sessionId = made.at(0).at(0).toString();
    const qint64 daemon = daemonPid();
    ASSERT_GT(daemon, 0);
    idle(200);
    const int daemonDescriptors = descriptorsOf(daemon);
    const int ownDescriptors = descriptorsOf(::getpid());
    const qsizetype mappings = telemetryMappings();

    for (int round = 0; round < 10; ++round) {
        const QString name = u"window %1"_s.arg(round);
        auto remote = std::make_unique<bus::RemoteEngine>(open(name));
        QSignalSpy opened(remote.get(), &core::EngineFacade::sessionOpened);
        QSignalSpy moved(remote.get(), &bus::RemoteEngine::telemetryChanged);
        remote->openSession(sessionId);
        ASSERT_TRUE(waitFor([&] { return opened.count() > 0; }));
        (void)remote->telemetryOf(sessionId);
        ASSERT_TRUE(waitFor([&] { return moved.count() > 0; })) << "round " << round;
        ASSERT_NE(remote->telemetryOf(sessionId), nullptr);
        EXPECT_EQ(telemetryMappings(), mappings + 1);
        remote.reset();
        close(name);
    }

    EXPECT_EQ(telemetryMappings(), mappings) << "a window's mapping outlived it";
    EXPECT_TRUE(waitFor([&] { return descriptorsOf(::getpid()) == ownDescriptors; }, 3000))
        << descriptorsOf(::getpid()) << " descriptors here, " << ownDescriptors << " before";
    EXPECT_TRUE(waitFor([&] { return descriptorsOf(daemon) == daemonDescriptors; }, 3000))
        << descriptorsOf(daemon) << " descriptors in the daemon, " << daemonDescriptors << " before";
}

TEST_F(EngineBus, AMappingGoesWithItsSessionOrItsDaemonAndComesBackWithTheNext)
{
    // Started by the bus: the real engine, which keeps its sessions on
    // disk and opens them again in the daemon that comes next.
    bus::RemoteEngine *remote = window(u"window"_s);
    QSignalSpy opened(remote, &core::EngineFacade::sessionOpened);
    QSignalSpy closed(remote, &core::EngineFacade::sessionClosed);
    remote->createSession(u"Mapped"_s);
    ASSERT_TRUE(waitFor([&] { return opened.count() > 0; }));
    const QString sessionId = opened.at(0).at(0).toString();
    // What a reader finds when it is told to look again.
    std::vector<bool> answers;
    QObject::connect(remote, &bus::RemoteEngine::telemetryChanged, remote, [&](const QString &id) {
        EXPECT_EQ(id, sessionId);
        answers.push_back(remote->telemetryOf(id) != nullptr);
    });
    const qsizetype mappings = telemetryMappings();
    (void)remote->telemetryOf(sessionId);
    ASSERT_TRUE(waitFor([&] { return answers.size() == 1; }));
    EXPECT_EQ(telemetryMappings(), mappings + 1);

    ASSERT_TRUE(signalProcess(daemonPid(), SIGKILL));
    ASSERT_TRUE(waitFor([&] { return answers.size() >= 2; })) << "the mapping of a daemon gone was kept";
    ASSERT_TRUE(waitFor([&] { return answers.size() >= 3; }, 15000)) << "never mapped in the next daemon";
    EXPECT_NE(remote->telemetryOf(sessionId), nullptr);
    EXPECT_EQ(telemetryMappings(), mappings + 1) << "the old daemon's memory is still mapped";

    remote->closeSession(sessionId);
    ASSERT_TRUE(waitFor([&] { return closed.count() > 0; }));
    idle(200);
    EXPECT_EQ(remote->telemetryOf(sessionId), nullptr);
    EXPECT_EQ(telemetryMappings(), mappings);
    EXPECT_EQ(answers, (std::vector<bool>{true, false, true, false}));
}

namespace
{

// **The public API as a script sees it** (docs/dbus-api.md): the
// names written out as a third party reads them in the docs, not taken from
// rb::bus, and plain QDBusMessages rather than RemoteEngine.
const QString kPublicService = u"io.github.reverblabr.Engine"_s;
const QString kPublicPath = u"/io/github/reverblabr/Engine"_s;
const QString kPublicInterface = u"io.github.reverblabr.Engine1"_s;

/// What a script hears on the bus: every signal it listens to, in order.
class Heard : public QObject
{
    Q_OBJECT

public:
    Heard(QDBusConnection connection, const QStringList &signalNames)
    {
        for (const QString &name : signalNames) {
            EXPECT_TRUE(
                connection.connect(kPublicService, kPublicPath, kPublicInterface, name, this, SLOT(take(QDBusMessage))))
                << name.toStdString();
        }
    }

    /// The first signal named \a member that \a match accepts, heard from
    /// \a from on.
    [[nodiscard]] std::optional<QDBusMessage>
    find(const QString &member, const std::function<bool(const QVariantList &)> &match, std::size_t from = 0) const
    {
        for (std::size_t index = from; index < messages.size(); ++index) {
            if (messages[index].member() == member && match(messages[index].arguments())) {
                return messages[index];
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool
    has(const QString &member, const std::function<bool(const QVariantList &)> &match, std::size_t from = 0) const
    {
        return find(member, match, from).has_value();
    }

    std::vector<QDBusMessage> messages;

public Q_SLOTS:
    void take(const QDBusMessage &message)
    {
        messages.push_back(message);
    }
};

/// A call as busctl makes it: blocking until the engine says it has taken
/// it, which is all a call on this API ever answers.
QDBusMessage callPublic(QDBusConnection connection, const QString &member, const QVariantList &arguments)
{
    QDBusMessage message = QDBusMessage::createMethodCall(kPublicService, kPublicPath, kPublicInterface, member);
    message.setArguments(arguments);
    return connection.call(message, QDBus::Block, 5000);
}

/// The released surface (dbus/io.github.reverblabr.Engine1.api), as
/// "method Name(in s a, …)" and "signal Name(s a, …)" lines.
QStringList releasedMembers()
{
    QFile file(QStringLiteral(RB_ENGINE_API_PIN));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QStringList members;
    for (const QString &line : QString::fromUtf8(file.readAll()).split(u'\n')) {
        if (line.startsWith("method "_L1) || line.startsWith("signal "_L1)) {
            members.append(line.trimmed());
        }
    }
    return members;
}

} // namespace

TEST_F(EngineBus, AScriptDrivesASessionWithNothingButTheDocumentedCalls)
{
    (void)startDaemon({u"--mock"_s, u"--idle-exit"_s, u"0"_s});
    QDBusConnection script = open(u"script"_s);
    ASSERT_TRUE(script.isConnected());
    Heard heard(script,
                {u"SessionOpened"_s,
                 u"SessionSnapshot"_s,
                 u"SessionPatch"_s,
                 u"StateChanged"_s,
                 u"SessionsListed"_s,
                 u"SessionClosed"_s,
                 u"Error"_s});
    // The match rules are in place once the bus has answered a call.
    ASSERT_EQ(callPublic(script, u"ListSessions"_s, {}).type(), QDBusMessage::ReplyMessage);

    // **A name with spaces and more than ASCII**, as a person types one.
    const QString name = u"Evening radio — Ünïcode"_s;
    ASSERT_EQ(callPublic(script, u"CreateSession"_s, {name}).type(), QDBusMessage::ReplyMessage);
    ASSERT_TRUE(waitFor([&] { return heard.has(u"SessionOpened"_s, [](const QVariantList &) { return true; }); }))
        << "CreateSession was never answered by SessionOpened";
    const QString sessionId =
        heard.find(u"SessionOpened"_s, [](const QVariantList &) { return true; })->arguments().at(0).toString();
    ASSERT_TRUE(waitFor([&] {
        return heard.has(u"SessionSnapshot"_s, [&](const QVariantList &a) { return a.at(0) == sessionId; });
    })) << "no snapshot followed SessionOpened";

    // **The docs' own example**: a Volume block, at half gain.
    const QString edit = uR"({"op":"addBlock","block":{"id":"quieter","type":"effect.volume","pos":[0,0]}})"_s;
    ASSERT_EQ(callPublic(script, u"ApplyEdit"_s, {sessionId, edit}).type(), QDBusMessage::ReplyMessage);
    ASSERT_TRUE(waitFor([&] {
        return heard.has(u"SessionPatch"_s, [&](const QVariantList &a) {
            return a.at(0) == sessionId && a.at(2).toString().contains(u"quieter"_s);
        });
    })) << "the edit never came back as a patch";
    ASSERT_EQ(callPublic(script, u"SetParam"_s, {sessionId, u"quieter"_s, u"gain"_s, u"0.5"_s}).type(),
              QDBusMessage::ReplyMessage);

    // Run it, ask how it is, stop it: each answered by StateChanged.
    const auto stateIs = [&](const QString &state, std::size_t from = 0) {
        return heard.has(
            u"StateChanged"_s, [&](const QVariantList &a) { return a.at(0) == sessionId && a.at(1) == state; }, from);
    };
    ASSERT_EQ(callPublic(script, u"Start"_s, {sessionId}).type(), QDBusMessage::ReplyMessage);
    ASSERT_TRUE(waitFor([&] { return stateIs(u"running"_s); })) << "Start never said running";
    const std::size_t afterStart = heard.messages.size();
    ASSERT_EQ(callPublic(script, u"GetSessionState"_s, {sessionId}).type(), QDBusMessage::ReplyMessage);
    EXPECT_TRUE(waitFor([&] { return stateIs(u"running"_s, afterStart); }))
        << "GetSessionState was not answered by StateChanged";
    ASSERT_EQ(callPublic(script, u"Stop"_s, {sessionId}).type(), QDBusMessage::ReplyMessage);
    ASSERT_TRUE(waitFor([&] { return stateIs(u"idle"_s); })) << "Stop never said idle";

    // The list has it, by id and by the name as it was given.
    const std::size_t beforeList = heard.messages.size();
    ASSERT_EQ(callPublic(script, u"ListSessions"_s, {}).type(), QDBusMessage::ReplyMessage);
    ASSERT_TRUE(waitFor([&] {
        return heard.has(
            u"SessionsListed"_s,
            [&](const QVariantList &a) {
                for (const QJsonValue &row : QJsonDocument::fromJson(a.at(0).toString().toUtf8()).array()) {
                    if (row["id"_L1].toString() == sessionId && row["name"_L1].toString() == name) {
                        return true;
                    }
                }
                return false;
            },
            beforeList);
    })) << "SessionsListed did not carry the session under its name";

    // **A failure is the Error signal with its code**, not a D-Bus error.
    const QString nobody = u"00000000-0000-0000-0000-000000000000"_s;
    EXPECT_EQ(callPublic(script, u"OpenSession"_s, {nobody}).type(), QDBusMessage::ReplyMessage);
    EXPECT_TRUE(waitFor([&] {
        return heard.has(u"Error"_s, [](const QVariantList &a) { return a.at(0) == u"session.notFound"_s; });
    })) << "an unknown session was not session.notFound";

    // **A call that makes no sense is refused on the spot**: a member there
    // is not, as UnknownMethod; arguments that are not the member's
    // published signature, as InvalidArgs, whether or not they would
    // convert (an integer would make a fine "42").
    const QDBusMessage unknown = callPublic(script, u"NoSuchMethod"_s, {});
    EXPECT_EQ(unknown.errorName(), u"org.freedesktop.DBus.Error.UnknownMethod"_s);
    const QDBusMessage wrongType = callPublic(script, u"Start"_s, {42});
    EXPECT_EQ(wrongType.type(), QDBusMessage::ErrorMessage) << "Start(i) was taken as Start(s)";
    EXPECT_EQ(wrongType.errorName(), u"org.freedesktop.DBus.Error.InvalidArgs"_s);
    const QDBusMessage tooFew = callPublic(script, u"Start"_s, {});
    EXPECT_EQ(tooFew.errorName(), u"org.freedesktop.DBus.Error.InvalidArgs"_s);
    EXPECT_FALSE(heard.has(u"StateChanged"_s, [&](const QVariantList &a) { return a.at(0) != sessionId; }))
        << "a refused call reached the engine";

    ASSERT_EQ(callPublic(script, u"CloseSession"_s, {sessionId}).type(), QDBusMessage::ReplyMessage);
    EXPECT_TRUE(waitFor([&] {
        return heard.has(u"SessionClosed"_s, [&](const QVariantList &a) { return a.at(0) == sessionId; });
    })) << "CloseSession was not answered by SessionClosed";
}

TEST_F(EngineBus, BusctlIntrospectionShowsExactlyTheReleasedInterface)
{
    // **The tool the docs name**, where there is one: busctl, pointed at
    // this private bus rather than --user.
    const QString busctl = QStandardPaths::findExecutable(u"busctl"_s);
    if (busctl.isEmpty()) {
        GTEST_SKIP() << "busctl is not installed";
    }
    (void)startDaemon({u"--mock"_s, u"--idle-exit"_s, u"0"_s});
    const QStringList released = releasedMembers();
    ASSERT_GE(released.size(), 70) << "cannot read " << RB_ENGINE_API_PIN;

    QProcess process;
    process.start(busctl,
                  {u"--address=%1"_s.arg(QString::fromUtf8(qgetenv("DBUS_SESSION_BUS_ADDRESS"))),
                   u"introspect"_s,
                   u"--xml-interface"_s,
                   kPublicService,
                   kPublicPath});
    ASSERT_TRUE(process.waitForFinished(10000));
    ASSERT_EQ(process.exitCode(), 0) << process.readAllStandardError().toStdString();
    const QString xml = QString::fromUtf8(process.readAllStandardOutput());
    const qsizetype start = xml.indexOf(u"<interface name=\"%1\">"_s.arg(kPublicInterface));
    ASSERT_GE(start, 0) << "busctl does not show " << kPublicInterface.toStdString();
    const QString interface = xml.mid(start, xml.indexOf(u"</interface>"_s, start) - start);

    // Every member, rebuilt from what busctl shows, in the pin's own words.
    QStringList shown;
    static const QRegularExpression member(uR"re(<(method|signal) name="(\w+)"\s*(/>|>(.*?)</\1>))re"_s,
                                           QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression argument(uR"re(<arg ([^>]*?)/?>)re"_s);
    static const QRegularExpression attribute(uR"re((\w+)="([^"]*)")re"_s);
    for (auto it = member.globalMatch(interface); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        const bool method = match.captured(1) == "method"_L1;
        QStringList arguments;
        for (auto args = argument.globalMatch(match.captured(4)); args.hasNext();) {
            QMap<QString, QString> values;
            for (auto attrs = attribute.globalMatch(args.next().captured(1)); attrs.hasNext();) {
                const QRegularExpressionMatch pair = attrs.next();
                values.insert(pair.captured(1), pair.captured(2));
            }
            const QString typed = values.value(u"type"_s) + u' ' + values.value(u"name"_s);
            arguments.append(method ? values.value(u"direction"_s, u"in"_s) + u' ' + typed : typed);
        }
        shown.append(u"%1 %2(%3)"_s.arg(match.captured(1), match.captured(2), arguments.join(u", "_s)));
    }
    shown.sort();
    QStringList expected = released;
    expected.sort();
    EXPECT_EQ(shown, expected) << "busctl shows " << shown.size() << " members, " << expected.size() << " are released";
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    // For UpdateActivationEnvironment's a{ss}.
    qDBusRegisterMetaType<QMap<QString, QString>>();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

#include "test_enginebus.moc"
