// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// **Reverblabr's icon in the system tray**, the real KStatusNotifierItem,
// against a StatusNotifierWatcher of the test's own on a private bus, in a
// PID namespace of its own (tests/bus/CMakeLists.txt). It must never run on
// a desktop's bus: it would put an icon in that desktop's tray.
//
// What the item says is read the way a tray reads it, over the bus, from a
// connection of the test's own; the clicks are made the way a tray makes
// them, by calling Activate and SecondaryActivate.

#include "core/editcommand.h"
#include "core/mockengine.h"
#include "trayicon.h"
#include "traystate.h"

#include <KStatusNotifierItem>
#include <QAction>
#include <QApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusVariant>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QIcon>
#include <QJsonDocument>
#include <QMenu>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <gtest/gtest.h>

#include <memory>

using namespace rb;
using namespace Qt::StringLiterals;

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

const QString kWatcherService = u"org.kde.StatusNotifierWatcher"_s;
const QString kWatcherPath = u"/StatusNotifierWatcher"_s;
const QString kItemPath = u"/StatusNotifierItem"_s;
const QString kItemInterface = u"org.kde.StatusNotifierItem"_s;

/// The desktop entry's `Icon=`: what the tray's icon has to be too.
[[nodiscard]] QString desktopIcon()
{
    QFile entry(QStringLiteral(RB_DESKTOP_FILE));
    if (!entry.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    for (const QByteArray &line : entry.readAll().split('\n')) {
        if (line.startsWith("Icon=")) {
            return QString::fromUtf8(line.mid(5)).trimmed();
        }
    }
    return {};
}

class Tray : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // **Refuse to run anywhere but a private bus**: an item registered
        // on a desktop's bus is an icon in that desktop's tray.
        const QByteArray address = qgetenv("DBUS_SESSION_BUS_ADDRESS");
        ASSERT_FALSE(address.isEmpty());
        ASSERT_FALSE(address.contains(QByteArray("/run/user/")))
            << "this is the real session bus; run it through ctest";
        ASSERT_TRUE(probe.isConnected());
    }

    void TearDown() override
    {
        if (watcher) {
            stopWatcher();
        }
        QDBusConnection::disconnectFromBus(u"tray-probe"_s);
    }

    /// The tray's watcher, in a process of its own; with \a host, a tray is
    /// registered with it that would show the icon.
    [[nodiscard]] bool startWatcher(bool host = true)
    {
        watcher = std::make_unique<QProcess>();
        watcher->setProcessChannelMode(QProcess::ForwardedErrorChannel);
        watcher->start(QStringLiteral(RB_FAKE_WATCHER), host ? QStringList{} : QStringList{u"--no-host"_s});
        if (!watcher->waitForStarted(5000)) {
            return false;
        }
        return waitFor([this] { return probe.interface()->isServiceRegistered(kWatcherService).value(); });
    }

    void stopWatcher()
    {
        watcher->terminate();
        if (!watcher->waitForFinished(5000)) {
            watcher->kill();
            watcher->waitForFinished(5000);
        }
        watcher.reset();
    }

    /// Calls and waits, turning the event loop meanwhile: **the item answers
    /// from this thread**, so a call that blocked it would never be answered.
    [[nodiscard]] QDBusMessage call(const QDBusMessage &message)
    {
        QDBusPendingCall pending = probe.asyncCall(message, 5000);
        (void)waitFor([&pending] { return pending.isFinished(); });
        return pending.reply();
    }

    [[nodiscard]] QVariant
    getProperty(const QString &service, const QString &path, const QString &interface, const QString &name)
    {
        QDBusMessage get =
            QDBusMessage::createMethodCall(service, path, u"org.freedesktop.DBus.Properties"_s, u"Get"_s);
        get << interface << name;
        const QDBusMessage reply = call(get);
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            return {};
        }
        return reply.arguments().constFirst().value<QDBusVariant>().variant();
    }

    /// The service of the item that registered with the watcher, or empty.
    [[nodiscard]] QString registeredItem()
    {
        const QStringList items =
            getProperty(kWatcherService, kWatcherPath, kWatcherService, u"RegisteredStatusNotifierItems"_s)
                .toStringList();
        return items.value(0);
    }

    [[nodiscard]] QVariant item(const QString &name)
    {
        return getProperty(itemService, kItemPath, kItemInterface, name);
    }

    /// Whether the item's \a name, an `a(iiay)` of pictures, has any.
    [[nodiscard]] bool hasPicture(const QString &name)
    {
        const QVariant value = item(name);
        if (!value.canConvert<QDBusArgument>()) {
            return false;
        }
        const auto pictures = value.value<QDBusArgument>();
        pictures.beginArray();
        const bool any = !pictures.atEnd();
        pictures.endArray();
        return any;
    }

    /// Waits for the item to register and remembers where it is.
    [[nodiscard]] bool itemRegistered()
    {
        if (!waitFor([this] { return !registeredItem().isEmpty(); })) {
            return false;
        }
        itemService = registeredItem();
        return true;
    }

    [[nodiscard]] static QString openSession(core::MockEngine &engine, const QString &name)
    {
        QSignalSpy opened(&engine, &core::EngineFacade::sessionOpened);
        engine.createSession(name);
        return opened.isEmpty() ? QString{} : opened.at(0).at(0).toString();
    }

    QDBusConnection probe = QDBusConnection::connectToBus(QDBusConnection::SessionBus, u"tray-probe"_s);
    std::unique_ptr<QProcess> watcher;
    QString itemService;
};

} // namespace

TEST_F(Tray, OnceInstalledTheTrayShowsTheLaunchersIconByName)
{
    // **The icon installed as Reverblabr installs it**: the drawing
    // under the application's id in a hicolor theme of the test's own.
    QTemporaryDir icons;
    ASSERT_TRUE(icons.isValid());
    const QString theme = icons.filePath(u"hicolor"_s);
    ASSERT_TRUE(QDir().mkpath(theme + u"/scalable/apps"_s));
    QFile index(theme + u"/index.theme"_s);
    ASSERT_TRUE(index.open(QIODevice::WriteOnly));
    index.write(
        "[Icon Theme]\nName=Hicolor\nDirectories=scalable/apps\n\n"
        "[scalable/apps]\nSize=128\nMinSize=8\nMaxSize=512\nType=Scalable\n");
    index.close();
    ASSERT_TRUE(QFile::copy(QStringLiteral(RB_APP_ICON_SVG), theme + u"/scalable/apps/"_s + desktopIcon() + u".svg"_s));
    const QStringList searchPaths = QIcon::themeSearchPaths();
    QIcon::setThemeSearchPaths(QStringList{icons.path()} + searchPaths);
    ASSERT_TRUE(QIcon::hasThemeIcon(desktopIcon())) << "the test's theme was not found";

    ASSERT_TRUE(startWatcher());
    core::MockEngine engine;
    ui::TrayState state(&engine);
    const ui::TrayIcon icon(&state);
    ASSERT_TRUE(itemRegistered());
    EXPECT_EQ(item(u"IconName"_s).toString(), desktopIcon()) << "the tray and the launcher show different icons";
    QIcon::setThemeSearchPaths(searchPaths);
}

TEST_F(Tray, TheIconRegistersWithTheTrayAndSaysWhetherASessionRuns)
{
    ASSERT_TRUE(startWatcher());
    core::MockEngine engine;
    const QString id = openSession(engine, u"Evening"_s);
    ui::TrayState state(&engine);
    const ui::TrayIcon icon(&state);
    ASSERT_TRUE(itemRegistered()) << "the item never registered with the tray's watcher";
    EXPECT_TRUE(state.isAvailable()) << "a tray with a host was not taken for one";

    // **Not installed here**, so the tray is given Reverblabr's picture
    // rather than a name its theme has nothing under.
    ASSERT_FALSE(QIcon::hasThemeIcon(desktopIcon())) << "this machine has Reverblabr's icon installed";
    EXPECT_EQ(item(u"IconName"_s).toString(), QString{}) << "the tray was given a name its theme cannot find";
    EXPECT_TRUE(hasPicture(u"IconPixmap"_s)) << "the tray was given no picture";
    EXPECT_EQ(item(u"Title"_s).toString(), u"Reverblabr"_s);
    EXPECT_EQ(item(u"Category"_s).toString(), u"ApplicationStatus"_s);
    ASSERT_TRUE(waitFor([&] { return item(u"Status"_s).toString() == "Passive"_L1; }))
        << "with nothing running the icon is " << item(u"Status"_s).toString().toStdString();

    engine.start(id);
    ASSERT_TRUE(waitFor([&] { return item(u"Status"_s).toString() == "Active"_L1; }))
        << "a running session left the icon " << item(u"Status"_s).toString().toStdString();
    engine.stop(id);
    ASSERT_TRUE(waitFor([&] { return item(u"Status"_s).toString() == "Passive"_L1; }));
}

TEST_F(Tray, ARecorderWritingPutsTheRedDotOnTheIcon)
{
    ASSERT_TRUE(startWatcher());
    core::MockEngine engine;
    const QString id = openSession(engine, u"Show"_s);
    core::BlockInstance recorder;
    recorder.id = u"rec"_s;
    recorder.type = u"output.recorder"_s;
    engine.applyEdit(
        id,
        QString::fromUtf8(
            QJsonDocument(core::editCommandToJson(core::AddBlock{recorder, -1})).toJson(QJsonDocument::Compact)));
    ui::TrayState state(&engine);
    const ui::TrayIcon icon(&state);
    ASSERT_TRUE(itemRegistered());
    EXPECT_EQ(item(u"OverlayIconName"_s).toString(), QString{});

    engine.start(id);
    ASSERT_TRUE(waitFor([&] { return item(u"OverlayIconName"_s).toString() == ui::TrayIcon::kRecordingOverlay; }))
        << "recording, and the icon had no red dot";
    engine.stop(id);
    ASSERT_TRUE(waitFor([&] { return item(u"OverlayIconName"_s).toString().isEmpty(); }))
        << "the red dot stayed after the recording stopped";
}

TEST_F(Tray, SomethingGoneWrongAsksForAttentionUntilItIsSeen)
{
    ASSERT_TRUE(startWatcher());
    core::MockEngine engine;
    const QString id = openSession(engine, u"Plugins"_s);
    ui::TrayState state(&engine);
    const ui::TrayIcon icon(&state);
    ASSERT_TRUE(itemRegistered());
    EXPECT_EQ(item(u"AttentionIconName"_s).toString(), ui::TrayIcon::kAttentionIcon);

    engine.reportError(u"plugin.crashed"_s,
                       u"{\"sessionId\":\"%1\",\"blockId\":\"p\",\"plugin\":\"Reverb\"}"_s.arg(id));
    ASSERT_TRUE(waitFor([&] { return item(u"Status"_s).toString() == "NeedsAttention"_L1; }))
        << "a crashed plugin left the icon " << item(u"Status"_s).toString().toStdString();

    state.acknowledge();
    ASSERT_TRUE(waitFor([&] { return item(u"Status"_s).toString() == "Passive"_L1; }))
        << "seen, and the icon still asks";
}

TEST_F(Tray, ItsClicksAndItsMenuComeBackAsRequests)
{
    ASSERT_TRUE(startWatcher());
    core::MockEngine engine;
    ui::TrayState state(&engine);
    const ui::TrayIcon icon(&state);
    ASSERT_TRUE(itemRegistered());
    QSignalSpy popover(&state, &ui::TrayState::popoverRequested);
    QSignalSpy window(&state, &ui::TrayState::windowToggleRequested);
    QSignalSpy sessions(&state, &ui::TrayState::sessionsWindowRequested);

    // A left click, as a tray makes it.
    QDBusMessage activate = QDBusMessage::createMethodCall(itemService, kItemPath, kItemInterface, u"Activate"_s);
    activate << 10 << 10;
    EXPECT_EQ(call(activate).type(), QDBusMessage::ReplyMessage);
    ASSERT_TRUE(waitFor([&] { return popover.count() == 1; })) << "a left click did not ask for the popover";
    EXPECT_EQ(window.count(), 0) << "a left click toggled the main window instead";

    // A middle click.
    QDBusMessage secondary =
        QDBusMessage::createMethodCall(itemService, kItemPath, kItemInterface, u"SecondaryActivate"_s);
    secondary << 10 << 10;
    EXPECT_EQ(call(secondary).type(), QDBusMessage::ReplyMessage);
    ASSERT_TRUE(waitFor([&] { return window.count() == 1; })) << "a middle click did not toggle the window";

    // The menu: ours first, the standard Quit still there, last.
    QMenu *menu = icon.item()->contextMenu();
    ASSERT_NE(menu, nullptr);
    QAction *show = nullptr;
    QAction *all = nullptr;
    for (QAction *action : menu->actions()) {
        if (action->text() == "Show Reverblabr"_L1) {
            show = action;
        } else if (action->text() == u"All Sessions…"_s) {
            all = action;
        }
    }
    ASSERT_NE(show, nullptr) << "no Show Reverblabr in the menu";
    ASSERT_NE(all, nullptr) << "no All Sessions… in the menu";
    EXPECT_EQ(menu->actions().constFirst(), show);
    EXPECT_NE(menu->actions().constLast(), all) << "the standard actions were lost";
    show->trigger();
    EXPECT_EQ(window.count(), 2);
    all->trigger();
    EXPECT_EQ(sessions.count(), 1);
}

TEST_F(Tray, ThereIsATrayOnlyWhileAHostShowsIt)
{
    core::MockEngine engine;
    ui::TrayState state(&engine);
    const ui::TrayIcon icon(&state);
    EXPECT_FALSE(state.isAvailable()) << "with no watcher at all, a tray was taken to be there";
    EXPECT_FALSE(ui::TrayIcon::hostAvailable(QDBusConnection::sessionBus()));

    // **A watcher with no host is no tray**: nothing would show the icon.
    ASSERT_TRUE(startWatcher(false));
    // Long enough for the watcher's arrival to have been noticed.
    (void)waitFor([] { return false; }, 500);
    EXPECT_FALSE(state.isAvailable()) << "a watcher with no host was taken for a tray";

    QDBusMessage setHost =
        QDBusMessage::createMethodCall(kWatcherService, kWatcherPath, u"org.reverblabr.Test"_s, u"SetHostRegistered"_s);
    setHost << true;
    EXPECT_EQ(call(setHost).type(), QDBusMessage::ReplyMessage);
    ASSERT_TRUE(waitFor([&] { return state.isAvailable(); })) << "a host came and the tray was not taken to be there";

    QDBusMessage dropHost =
        QDBusMessage::createMethodCall(kWatcherService, kWatcherPath, u"org.reverblabr.Test"_s, u"SetHostRegistered"_s);
    dropHost << false;
    EXPECT_EQ(call(dropHost).type(), QDBusMessage::ReplyMessage);
    ASSERT_TRUE(waitFor([&] { return !state.isAvailable(); }))
        << "the host went and the tray was still taken to be there";

    (void)call(setHost);
    ASSERT_TRUE(waitFor([&] { return state.isAvailable(); }));
    // The whole tray gone, as when Plasma's shell restarts.
    stopWatcher();
    ASSERT_TRUE(waitFor([&] { return !state.isAvailable(); })) << "the watcher went and the tray was still there";
}

int main(int argc, char **argv)
{
    // KStatusNotifierItem's menu is a QMenu, which takes a widgets
    // application; the platform is offscreen (tests/bus/CMakeLists.txt).
    QApplication application(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
