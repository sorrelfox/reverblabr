// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// reverblabrd: the engine as a background service (ADR-0017). It serves the
// engine interface on the session bus, keeps every session open and running
// whatever the windows do, and exits by itself once nobody is using it.

#include "autorun.h"
#include "autostartunit.h"
#include "blocks/builtinblocks.h"
#include "buildversion.h"
#include "bus/enginebus.h"
#include "bus/engineservice.h"
#include "core/engine.h"
#include "core/logring.h"
#include "core/mockengine.h"
#include "dsp/blockregistry.h"
#include "freedesktopsender.h"
#include "notifications.h"
#include "opensessions.h"
#include "sessionrunner.h"
#include "texts.h"
#include "timerschedule.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDir>
#include <QLoggingCategory>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QTimer>
#include <sys/socket.h>
#include <unistd.h>

#include <csignal>
#include <functional>
#include <memory>
#include <optional>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcDaemon, "reverblabr.daemon")

namespace
{

/// The write end of the pipe a stop signal is said on: a signal handler may
/// do almost nothing, and writing a byte is one of the things it may.
int stopPipe[2] = {-1, -1};

void onStopSignal(int)
{
    const char byte = 1;
    (void)::write(stopPipe[0], &byte, 1);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // The last lines logged, which GetDiagnostics hands to a bug report;
    // the journal still gets every one.
    rb::core::LogRing::install();
    // **The application's name, not the process's**: the engine keeps its
    // sessions, presets, recordings and plugin cache under it
    // (`$XDG_DATA_HOME/reverblabr/...`), and they are the same ones whether
    // the engine runs here or in the window.
    QCoreApplication::setApplicationName(u"reverblabr"_s);
    QCoreApplication::setOrganizationDomain(u"reverblabr.github.io"_s);
    QCoreApplication::setApplicationVersion(QString::fromUtf8(rb::versionFull()));

    QCommandLineParser parser;
    parser.setApplicationDescription(u"The Reverblabr engine, as a service on the session bus."_s);
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption safeMode(u"safe-mode"_s, u"Open no sessions at start: every session stays stopped."_s);
    const QCommandLineOption mock(u"mock"_s, u"Serve the mock engine: no PipeWire and no audio (for tests)."_s);
    const QCommandLineOption idleExit(u"idle-exit"_s,
                                      u"Exit after this many seconds with no client and nothing running (0: never)."_s,
                                      u"seconds"_s,
                                      u"60"_s);
    const QCommandLineOption stateFile(u"state-file"_s, u"Where the open sessions are written down."_s, u"path"_s);
    const QCommandLineOption window(u"window"_s,
                                    u"The program KRunner's Open starts to show a session (for tests)."_s,
                                    u"path"_s,
                                    QStringLiteral(RB_WINDOW_EXEC));
    parser.addOptions({safeMode, mock, idleExit, stateFile, window});
    parser.process(app);

    rb::dsp::BlockRegistry registry;
    if (!rb::blocks::registerBuiltinBlocks(registry)) {
        return 1;
    }
    std::unique_ptr<rb::core::EngineFacade> engine;
    // **Where a session's levels are**, handed to a window as shared memory
    // rather than sent (ADR-0018); outside the facade, as telemetry always
    // is (ADR-0003).
    std::function<int(const QString &)> shareTelemetry;
    // The sessions as a list, read without being announced, for Auto Run.
    std::function<QString()> sessionListing;
    if (parser.isSet(mock)) {
        auto fake = std::make_unique<rb::core::MockEngine>();
        shareTelemetry = [mock = fake.get()](const QString &sessionId) { return mock->shareTelemetry(sessionId); };
        sessionListing = [mock = fake.get()] { return mock->sessionListing(); };
        engine = std::move(fake);
    } else {
        auto real = std::make_unique<rb::core::Engine>(registry);
        real->connectToPipeWire();
        shareTelemetry = [real = real.get()](const QString &sessionId) { return real->shareTelemetry(sessionId); };
        sessionListing = [real = real.get()] { return real->sessionListing(); };
        engine = std::move(real);
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qCCritical(lcDaemon) << "no session bus:" << bus.lastError().message();
        return 1;
    }
    // **The sessions in KRunner** (ADR-0023), on an object of their own:
    // served before the name is taken, so a query that started the daemon
    // finds it there.
    rb::daemon::SessionRunner runner(engine.get(), sessionListing, parser.value(window));
    if (!runner.publish(bus)) {
        qCWarning(lcDaemon) << "could not serve the KRunner runner at" << rb::daemon::kRunnerPath;
    }
    rb::bus::EngineService service(engine.get(), bus);
    service.setTelemetry(shareTelemetry);
    if (!service.publish()) {
        qCCritical(lcDaemon) << "another engine already has" << rb::bus::kServiceName << "on this bus";
        return 1;
    }

    // **Notifications while no window is open**: the same events, in
    // the same words, as the window sends through KNotifications — straight
    // to the desktop's notification server here, since the daemon has no KDE
    // Frameworks (ADR-0017). **A window sends its own**, and holds
    // notify::kWindowNotifierName while it does, so this stays quiet while
    // anybody holds it — a command-line client does not.
    rb::notify::bindCatalog();
    rb::notify::NotificationPolicy notifications;
    QObject::connect(
        engine.get(), &rb::core::EngineFacade::error, &notifications, &rb::notify::NotificationPolicy::error);
    QObject::connect(
        engine.get(), &rb::core::EngineFacade::notice, &notifications, &rb::notify::NotificationPolicy::notice);
    QObject::connect(engine.get(),
                     &rb::core::EngineFacade::recordingFileFinished,
                     &notifications,
                     &rb::notify::NotificationPolicy::recordingFileFinished);
    const QString windowNotifier = QString::fromLatin1(rb::notify::kWindowNotifierName.data(),
                                                       static_cast<qsizetype>(rb::notify::kWindowNotifierName.size()));
    rb::notify::FreedesktopSender sender(bus, [bus, windowNotifier](rb::notify::Event event) {
        return !bus.interface()->isServiceRegistered(windowNotifier).value() && rb::notify::popupWanted(event);
    });
    QObject::connect(
        &notifications, &rb::notify::NotificationPolicy::notify, &sender, &rb::notify::FreedesktopSender::send);

    const QString statePath = parser.isSet(stateFile)
        ? parser.value(stateFile)
        : QDir(QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation))
              .filePath(u"reverblabr/daemon.json"_s);
    rb::daemon::OpenSessions openSessions(engine.get(), statePath);
    // **Auto Run** (ADR-0021): the sessions set to run at login start
    // with the first engine of the login, whatever brings it up: the unit
    // it keeps enabled while any session wants it, a window, a script or a
    // KRunner query. **Once a login** (ADR-0023): an engine started again
    // after it exited when idle leaves alone what the person stopped, and
    // only one after a crash starts them again. Safe mode starts nothing, but
    // the unit is still kept in step with the settings.
    rb::daemon::AutostartUnit autostartUnit(bus);
    rb::daemon::AutoRun autoRun(engine.get(), &autostartUnit, sessionListing);
    const rb::daemon::AutoRunMarker autoRunMarker(rb::daemon::AutoRunMarker::defaultPath());
    // **The sessions' timers**, as systemd user timers whose service runs
    // `reverblabr-ctl timer-fire`, installed beside this program; that call
    // is what brings the engine up when the time comes. Kept in step in
    // safe mode too: only a timer's firing starts anything. **Never for
    // the mock engine**: a test's or a developer's pretend sessions must not
    // touch the person's systemd timers.
    std::optional<rb::daemon::TimerSchedule> timerSchedule;
    if (!parser.isSet(mock)) {
        timerSchedule.emplace(engine.get(),
                              bus,
                              rb::daemon::TimerSchedule::userUnitDirectory(),
                              QDir(QCoreApplication::applicationDirPath()).filePath(u"reverblabr-ctl"_s),
                              sessionListing);
    }
    if (parser.isSet(safeMode)) {
        qCInfo(lcDaemon) << "safe mode: opening no sessions";
    } else {
        openSessions.restore();
        if (autoRunMarker.due()) {
            autoRun.start();
        } else {
            qCInfo(lcDaemon) << "Auto Run has run in this login already: starting no session";
        }
        autoRunMarker.running();
    }

    // **Gone once nobody needs it**: no client on the bus and nothing
    // running, for as long as --idle-exit says.
    QTimer idle;
    idle.setSingleShot(true);
    idle.setInterval(std::chrono::seconds(parser.value(idleExit).toInt()));
    QObject::connect(&idle, &QTimer::timeout, &app, [] {
        qCInfo(lcDaemon) << "nobody is using the engine; exiting";
        QCoreApplication::quit();
    });
    const bool exitsWhenIdle = parser.value(idleExit).toInt() > 0;
    QObject::connect(&service, &rb::bus::EngineService::idleChanged, &app, [&idle, exitsWhenIdle](bool isIdle) {
        if (isIdle && exitsWhenIdle) {
            idle.start();
        } else {
            idle.stop();
        }
    });
    if (service.isIdle() && exitsWhenIdle) {
        idle.start();
    }

    // **Stopped like a service**: systemd's SIGTERM, or ^C, ends the event
    // loop, so every session is written and closed on the way out rather
    // than cut off.
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, stopPipe) == 0) {
        std::signal(SIGTERM, onStopSignal);
        std::signal(SIGINT, onStopSignal);
    }
    QSocketNotifier stopping(stopPipe[1], QSocketNotifier::Read);
    QObject::connect(&stopping, &QSocketNotifier::activated, &app, [] { QCoreApplication::quit(); });

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&openSessions, &autoRunMarker, &parser, &safeMode] {
        openSessions.freeze();
        if (!parser.isSet(safeMode)) {
            autoRunMarker.exited();
        }
    });
    qCInfo(lcDaemon) << "serving" << rb::bus::kServiceName << (parser.isSet(mock) ? "(mock engine)" : "");
    const int result = app.exec();
    openSessions.freeze();
    return result;
}
