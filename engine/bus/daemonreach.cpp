// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "bus/daemonreach.h"

#include "bus/enginebus.h"

#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QEventLoop>
#include <QProcess>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace rb::bus
{

namespace
{

/// Starts the daemon with \a start and waits up to \a timeout for somebody to
/// take the name, which is the daemon once it has opened what it had open.
DaemonReachResult
startAndWait(const QDBusConnection &bus, std::chrono::milliseconds timeout, const DaemonStarter &start)
{
    const QString name(kServiceName);
    QDBusServiceWatcher watcher(name, bus, QDBusServiceWatcher::WatchForRegistration);
    QEventLoop loop;
    QObject::connect(&watcher, &QDBusServiceWatcher::serviceRegistered, &loop, &QEventLoop::quit);
    QTimer::singleShot(timeout, &loop, &QEventLoop::quit);
    if (!start()) {
        return {DaemonReach::Refused, u"the engine's program could not be run"_s};
    }
    // **Asked again after the start**: a daemon quicker than the watcher's
    // match rule took the name without a signal anybody saw.
    if (!bus.interface()->isServiceRegistered(name).value()) {
        loop.exec();
    }
    if (bus.interface()->isServiceRegistered(name).value()) {
        return {DaemonReach::Reached, {}};
    }
    return {DaemonReach::Refused, u"the engine did not take its name in time"_s};
}

} // namespace

DaemonStarter detachedStarter(const QString &program, const QStringList &arguments)
{
    return [program, arguments] { return QProcess::startDetached(program, arguments); };
}

DaemonReachResult
reachDaemon(const QDBusConnection &bus, std::chrono::milliseconds timeout, const DaemonStarter &startItself)
{
    QDBusConnectionInterface *const daemon = bus.isConnected() ? bus.interface() : nullptr;
    if (daemon == nullptr) {
        return {DaemonReach::NoSessionBus, bus.lastError().message()};
    }
    if (daemon->isServiceRegistered(kServiceName).value()) {
        return {DaemonReach::Reached, {}};
    }
    // **A starter goes first, before the bus is asked**: in a Flatpak, an
    // activation file on the host's bus belongs to a package installed beside
    // it, and would start that engine, outside the sandbox (ADR-0034).
    if (startItself) {
        return startAndWait(bus, timeout, startItself);
    }
    // **Asked before starting**, so that a build nobody has installed is told
    // apart from a daemon that would not start: the first is where anybody
    // running from a build tree stands, and needs a different sentence.
    const QDBusReply<QStringList> activatable = daemon->activatableServiceNames();
    if (!activatable.isValid()) {
        return {DaemonReach::Refused, activatable.error().name() + u": "_s + activatable.error().message()};
    }
    if (!activatable.value().contains(QString(kServiceName))) {
        return {DaemonReach::NotInstalled, {}};
    }
    QDBusMessage start = QDBusMessage::createMethodCall(
        u"org.freedesktop.DBus"_s, u"/org/freedesktop/DBus"_s, u"org.freedesktop.DBus"_s, u"StartServiceByName"_s);
    start << QString(kServiceName) << 0U;
    const QDBusMessage reply = bus.call(start, QDBus::Block, static_cast<int>(timeout.count()));
    if (reply.type() == QDBusMessage::ErrorMessage) {
        return {DaemonReach::Refused, reply.errorName() + u": "_s + reply.errorMessage()};
    }
    return {DaemonReach::Reached, {}};
}

EngineChoice chooseEngine(const EngineRequest &request, const std::function<DaemonReachResult()> &reach)
{
    if (request.mock) {
        return {EngineKind::Mock, {}};
    }
    if (request.inProcess) {
        return {EngineKind::InProcess, {}};
    }
    if (request.daemon) {
        return {EngineKind::Daemon, {}};
    }
    const DaemonReachResult result = reach();
    if (result.reach == DaemonReach::Reached) {
        return {EngineKind::Daemon, {}};
    }
    return {EngineKind::InProcess, reasonOf(result.reach)};
}

QString reasonOf(DaemonReach reach)
{
    switch (reach) {
    case DaemonReach::Reached:
        return {};
    case DaemonReach::NoSessionBus:
        return u"noSessionBus"_s;
    case DaemonReach::NotInstalled:
        return u"daemonNotInstalled"_s;
    case DaemonReach::Refused:
        return u"daemonRefused"_s;
    }
    return {};
}

} // namespace rb::bus
