// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "appearance.h"
#include "applicationicon.h"
#include "appshell.h"
#include "blocks/builtinblocks.h"
#include "buildversion.h"
#include "bus/daemonreach.h"
#include "bus/remoteengine.h"
#include "core/engine.h"
#include "core/engineinfo.h"
#include "core/logring.h"
#include "core/mockengine.h"
#include "desktopnotifier.h"
#include "dsp/blockregistry.h"
#include "globalshortcutregistry.h"
#include "globalshortcuts.h"
#include "launchrequests.h"
#include "texts.h"
#include "trayicon.h"
#include "traystate.h"

#include <KAboutData>
#include <KDBusService>
#include <KLocalizedQmlContext>
#include <KLocalizedString>
#include <KWindowSystem>
#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>
#include <QVersionNumber>
#include <QtQml/QQmlExtensionPlugin>

#include <memory>

Q_IMPORT_QML_PLUGIN(io_github_reverblabrPlugin)
Q_IMPORT_QML_PLUGIN(ReverblabrPlugin)

using namespace Qt::StringLiterals;

int main(int argc, char *argv[])
{
    // **QML's garbage is collected all at once on Qt 6.10.** Its collector,
    // working a little at a time, crashed in two of six runs of the QML
    // tests' walk through every panel on Ubuntu 26.04's Qt 6.10.2, and in
    // none of six with the collection done at once; Qt 6.11 has not been
    // seen to.
    // Read when the first QML engine starts, so it is set before anything,
    // and only where the user has not chosen for themselves.
    if (QLibraryInfo::version() < QVersionNumber(6, 11) && !qEnvironmentVariableIsSet("QV4_GC_TIMELIMIT")) {
        qputenv("QV4_GC_TIMELIMIT", "0");
    }
    // QApplication rather than QGuiApplication: the native Breeze controls
    // (org.kde.desktop) draw through the widget style.
    QApplication app(argc, argv);
    // **The last lines logged, for a bug report**: kept from here
    // on, and still sent wherever they went before.
    rb::core::LogRing::install();

    KLocalizedString::setApplicationDomain("reverblabr");

    KAboutData about(u"reverblabr"_s,
                     i18nc("@title", "Reverblabr"),
                     QString::fromUtf8(rb::versionFull()),
                     i18nc("@info", "Capture, process and route audio on your desktop"),
                     KAboutLicense::MPL_V2,
                     i18nc("@info:credit", "© 2026 Reverblabr contributors"));
    about.setDesktopFileName(u"io.github.reverblabr.Reverblabr"_s);
    // **Our own name on the bus, not KDE's**: KDBusService below
    // registers as the reversed organization domain and the application's
    // name, io.github.reverblabr.reverblabr beside the engine's
    // io.github.reverblabr.Engine, where KAboutData's default would put it
    // under org.kde. Nothing else reads the domain: the settings files are
    // named after the application.
    about.setOrganizationDomain(QByteArrayLiteral("reverblabr.github.io"));
    // **Not KDE's tracker**, which is where KAboutData points by default
    // and is not where this project's bugs go. The same address the
    // translation template carries.
    about.setBugAddress("https://github.com/sorrelfox/reverblabr/issues");
    about.addComponent(u"PipeWire"_s,
                       i18nc("@info", "Audio and video server"),
                       rb::core::engineInfo().pipeWireLibrary,
                       u"https://pipewire.org"_s,
                       KAboutLicense::MIT);
    KAboutData::setApplicationData(about);
    // The launcher, the task manager and the tray show the same picture; a
    // build that has not been installed shows it too.
    QApplication::setWindowIcon(rb::ui::applicationIcon());

    // **The control style, once, before there is a window**: the
    // desktop's Breeze controls to follow the system, our own for the
    // Reverblabr appearances. Qt cannot restyle a window that is open, so a
    // change of appearance reaches the controls at the next start. Read
    // once the application has its name, which is what names the settings
    // file the choice is in.
    rb::ui::Appearance::useStyleFor(rb::ui::Appearance::storedMode());
    // **A window with no frame is round at the corners**, which takes
    // a surface that can be transparent there; it has to be asked for before
    // the window is made.
    if (rb::ui::Appearance::ownStyle()) {
        QQuickWindow::setDefaultAlphaBuffer(true);
    }

    // **The appearance before anything is drawn**: it loads the
    // bundled faces and, unless the desktop is to decide, makes Noto Sans
    // the application's font, so the first frame is already the right one.
    rb::ui::Appearance::instance();

    QCommandLineParser parser;
    const QCommandLineOption mockOption(
        u"mock"_s, i18nc("@info:shell", "Run against the mock engine, with no PipeWire and no audio"));
    parser.addOption(mockOption);
    const QCommandLineOption inProcessOption(
        u"in-process"_s,
        i18nc("@info:shell", "Run the engine inside this window, so its sessions stop when the window closes"));
    parser.addOption(inProcessOption);
    const QCommandLineOption daemonOption(
        u"daemon"_s,
        i18nc("@info:shell",
              "Use the engine in the background service even if it can't be reached yet, rather than running one "
              "in this window"));
    parser.addOption(daemonOption);
    rb::ui::LaunchRequests::addOptions(parser);
    about.setupCommandLine(&parser);
    parser.process(app);
    about.processCommandLine(&parser);

    // **One Reverblabr a desktop**. A second launch, such as the
    // desktop entry's New Session, hands its arguments to the running one
    // and ends here, before it has chosen an engine or made a window; the
    // running one does what they ask. Two windows would mean two trays, two
    // sets of global shortcuts and, without the daemon, two engines. **Not
    // with --mock**, which is for trying the window out beside a real one.
    // Set once the window is made, below; a launch handed over before then
    // has no window to bring forward yet, and its request waits in the queue.
    QPointer<QWindow> mainWindow;
    std::unique_ptr<KDBusService> service;
    if (!parser.isSet(mockOption)) {
        service = std::make_unique<KDBusService>(KDBusService::Unique);
        QObject::connect(service.get(),
                         &KDBusService::activateRequested,
                         &app,
                         [&mainWindow](const QStringList &arguments, const QString &) {
                             // The launch's activation token, which KDBusService
                             // has put where KWindowSystem looks: without it a
                             // Wayland compositor will not let the window come
                             // forward for a launch the person made.
                             if (mainWindow) {
                                 KWindowSystem::updateStartupId(mainWindow);
                             }
                             rb::ui::LaunchRequests::instance()->deliver(arguments);
                         });
    }

    // **The window cannot tell which of these it has**, which is the whole
    // point of the facade: the interface can be built and demonstrated on a
    // machine with no audio server at all.
    rb::dsp::BlockRegistry registry;
    if (!rb::blocks::registerBuiltinBlocks(registry)) {
        return 1;
    }
    // **The engine in reverblabrd unless there is none to be had**
    // (ADR-0017): asked once, before there is a window, and the bus starts
    // the daemon if it is installed and not running. A window that cannot
    // reach it runs its own engine, and says so, rather than coming up with
    // nothing behind it — which is where a build that has not been installed
    // always stands.
    const rb::bus::EngineChoice choice =
        rb::bus::chooseEngine({.mock = parser.isSet(mockOption),
                               .inProcess = parser.isSet(inProcessOption),
                               .daemon = parser.isSet(daemonOption)},
                              [] {
                                  const rb::bus::DaemonReachResult reached =
                                      rb::bus::reachDaemon(QDBusConnection::sessionBus());
                                  if (reached.reach != rb::bus::DaemonReach::Reached) {
                                      const QString why = reached.detail.isEmpty()
                                          ? rb::bus::reasonOf(reached.reach)
                                          : rb::bus::reasonOf(reached.reach) + u": "_s + reached.detail;
                                      qWarning().noquote() << u"The background engine can't be reached (%1); running "
                                                              u"the engine in this window"_s.arg(why);
                                  }
                                  return reached;
                              });
    if (!choice.fallbackReason.isEmpty()) {
        rb::ui::AppShell::setStartupNotice(
            u"engine.daemonUnreachable"_s,
            QString::fromUtf8(
                QJsonDocument(QJsonObject{{u"reason"_s, choice.fallbackReason}}).toJson(QJsonDocument::Compact)));
    }

    std::unique_ptr<rb::core::Engine> engineImplementation;
    std::unique_ptr<rb::core::MockEngine> mockImplementation;
    std::unique_ptr<rb::bus::RemoteEngine> remoteImplementation;
    if (choice.kind == rb::bus::EngineKind::Mock) {
        mockImplementation = std::make_unique<rb::core::MockEngine>();
        rb::core::MockEngine *mock = mockImplementation.get();
        rb::ui::AppShell::setEngine(mock, [mock](const QString &sessionId) { return mock->telemetryOf(sessionId); });
    } else if (choice.kind == rb::bus::EngineKind::Daemon) {
        // **The engine in reverblabrd** (ADR-0017), reached over the
        // session bus and started by the first call. Its levels are read
        // from memory the daemon shares, mapped once a session is open
        // (ADR-0018); the meters are told when a mapping comes or goes.
        remoteImplementation = std::make_unique<rb::bus::RemoteEngine>();
        rb::bus::RemoteEngine *remote = remoteImplementation.get();
        QObject::connect(remote, &rb::bus::RemoteEngine::telemetryChanged, &app, &rb::ui::AppShell::telemetryMoved);
        rb::ui::AppShell::setEngine(remote,
                                    [remote](const QString &sessionId) { return remote->telemetryOf(sessionId); });
    } else {
        // An engine of the window's own: no Auto Run, and the Info
        // tab says so.
        rb::ui::AppShell::setEngineInBackground(false);
        engineImplementation = std::make_unique<rb::core::Engine>(registry);
        engineImplementation->connectToPipeWire();
        rb::core::Engine *real = engineImplementation.get();
        // Telemetry deliberately does not travel through the facade
        // ([ADR-0003]); where to read it is handed over separately.
        rb::ui::AppShell::setEngine(real, [real](const QString &sessionId) { return real->telemetryOf(sessionId); });
    }

    // **What the engine says that is worth a notification**, sent
    // through KNotifications while the window is open.
    rb::notify::bindCatalog();
    const rb::ui::DesktopNotifier notifier(rb::ui::AppShell::engine());

    // **Reverblabr in the system tray**. The icon shows what the tray
    // state says and passes its clicks to the window through it. **With a
    // tray, closing the window hides it** and the application goes on in the
    // tray, its sessions with it; with none, closing ends the application,
    // since a hidden window could never be brought back.
    // Made here, after the engine is chosen, so it follows that one.
    rb::ui::TrayState *tray = rb::ui::TrayState::instance();
    const rb::ui::TrayIcon trayIcon(tray);
    QApplication::setQuitOnLastWindowClosed(!tray->isAvailable());
    QObject::connect(tray, &rb::ui::TrayState::availableChanged, &app, [tray] {
        QApplication::setQuitOnLastWindowClosed(!tray->isAvailable());
    });

    // **Global shortcuts**, in System Settings → Shortcuts and working
    // while the window is in the background or in the tray. Registered here
    // and only here, so a test never binds anything on a desktop.
    const rb::ui::GlobalShortcutRegistry shortcuts(rb::ui::GlobalShortcuts::instance());

    QQmlApplicationEngine engine;
    KLocalization::setupLocalizedContext(&engine);
    engine.loadFromModule("io.github.reverblabr", "Main");
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    mainWindow = qobject_cast<QWindow *>(engine.rootObjects().constFirst());
    // **This launch's own request**, such as the desktop entry's New Session
    // when Reverblabr was not running yet: once the window is listening.
    QTimer::singleShot(0, &app, [] { rb::ui::LaunchRequests::instance()->deliver(QCoreApplication::arguments()); });
    return app.exec();
}
