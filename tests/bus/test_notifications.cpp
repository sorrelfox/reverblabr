// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Desktop notifications as the desktop receives them: the window's,
// through KNotifications, and the daemon's, straight to the notification
// server, both against **a notification server of the test's own** on a
// private bus, so that nothing here reaches anybody's desktop.

#include "blocks/recorder.h"
#include "bus/enginebus.h"
#include "bus/remoteengine.h"
#include "core/editcommand.h"
#include "core/mockengine.h"
#include "desktopnotifier.h"
#include "fileactions.h"
#include "freedesktopsender.h"
#include "notifications.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusVirtualObject>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
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

void idle(int milliseconds)
{
    (void)waitFor([] { return false; }, milliseconds);
}

const QString kPath = u"/org/freedesktop/Notifications"_s;
const QString kInterface = u"org.freedesktop.Notifications"_s;

/// **The desktop's notification server, played by the test**, on a
/// connection of its own: every Notify is kept, and a test can press an
/// action the way a person would.
class FakeNotificationServer : public QDBusVirtualObject
{
public:
    struct Shown {
        uint id = 0;
        QString application;
        QString summary;
        QString body;
        QStringList actions;
        QVariantMap hints;

        /// The key of the action whose words are \a words, or empty.
        [[nodiscard]] QString actionCalled(const QString &words) const
        {
            for (qsizetype index = 0; index + 1 < actions.size(); index += 2) {
                if (actions.at(index + 1) == words && actions.at(index) != "default"_L1) {
                    return actions.at(index);
                }
            }
            return {};
        }
    };

    FakeNotificationServer()
        : m_bus(QDBusConnection::connectToBus(QDBusConnection::SessionBus, u"fake-notification-server"_s))
    {
    }

    ~FakeNotificationServer() override
    {
        m_bus.unregisterService(kInterface);
        m_bus.unregisterObject(kPath);
        QDBusConnection::disconnectFromBus(u"fake-notification-server"_s);
    }

    [[nodiscard]] bool start()
    {
        return m_bus.isConnected() && m_bus.registerVirtualObject(kPath, this) && m_bus.registerService(kInterface);
    }

    void press(uint id, const QString &action)
    {
        QDBusMessage pressed = QDBusMessage::createSignal(kPath, kInterface, u"ActionInvoked"_s);
        pressed << id << action;
        m_bus.send(pressed);
    }

    QList<Shown> shown;

    [[nodiscard]] QString introspect(const QString &path) const override
    {
        Q_UNUSED(path)
        return {};
    }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        if (message.interface() != kInterface) {
            return false;
        }
        const QString member = message.member();
        if (member == "Notify"_L1) {
            const QVariantList args = message.arguments();
            Shown notification;
            notification.id = ++m_next;
            notification.application = args.value(0).toString();
            notification.summary = args.value(3).toString();
            notification.body = args.value(4).toString();
            notification.actions = args.value(5).toStringList();
            notification.hints = qdbus_cast<QVariantMap>(args.value(6));
            shown.append(notification);
            connection.send(message.createReply(QVariant::fromValue(notification.id)));
            return true;
        }
        if (member == "GetCapabilities"_L1) {
            connection.send(message.createReply(
                QVariant(QStringList{u"actions"_s, u"body"_s, u"body-markup"_s, u"icon-static"_s, u"persistence"_s})));
            return true;
        }
        if (member == "GetServerInformation"_L1) {
            connection.send(message.createReply({u"fake"_s, u"Reverblabr tests"_s, u"1"_s, u"1.2"_s}));
            return true;
        }
        if (member == "CloseNotification"_L1) {
            connection.send(message.createReply());
            return true;
        }
        return false;
    }

private:
    QDBusConnection m_bus;
    uint m_next = 0;
};

[[nodiscard]] QString json(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

class Notifications : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // **Refuse to run anywhere but a private bus**: this test owns
        // org.freedesktop.Notifications, and must never take it from a
        // desktop.
        const QByteArray address = qgetenv("DBUS_SESSION_BUS_ADDRESS");
        ASSERT_FALSE(address.isEmpty());
        ASSERT_FALSE(address.contains(QByteArray("/run/user/")))
            << "this is the real session bus; run it through ctest";
        ASSERT_TRUE(server.start()) << "another notification server on this bus";
        ui::FileActions::setRunnerForTesting(
            [this](
                ui::FileActions::Action action, const QUrl &url, QWindow *window, const ui::FileActions::Done &done) {
                Q_UNUSED(window)
                fileActions.append({action, url.toLocalFile()});
                done(QString{});
            });
    }

    void TearDown() override
    {
        ui::FileActions::setRunnerForTesting({});
    }

    FakeNotificationServer server;
    QList<std::pair<ui::FileActions::Action, QString>> fileActions;
};

} // namespace

TEST_F(Notifications, TheDaemonShowsAFinishedRecordingWithItsActions)
{
    notify::NotificationPolicy policy;
    notify::FreedesktopSender sender(QDBusConnection::sessionBus(), [](notify::Event) { return true; });
    QList<std::pair<QString, QString>> opened;
    sender.setOpenerForTesting(
        [&opened](const QString &action, const QString &file) { opened.append({action, file}); });
    QObject::connect(&policy, &notify::NotificationPolicy::notify, &sender, &notify::FreedesktopSender::send);

    policy.recordingFileFinished(u"s"_s, u"rec"_s, u"/recordings/Take 1.flac"_s, json({{u"seconds"_s, 42}}));
    ASSERT_TRUE(waitFor([&] { return server.shown.size() == 1; }));
    const FakeNotificationServer::Shown shown = server.shown.at(0);
    EXPECT_EQ(shown.application, u"Reverblabr"_s);
    EXPECT_EQ(shown.summary, u"Recording finished"_s);
    EXPECT_TRUE(shown.body.contains("Take 1.flac"_L1)) << shown.body.toStdString();
    EXPECT_EQ(shown.hints.value(u"desktop-entry"_s).toString(), u"io.github.reverblabr.Reverblabr"_s);
    ASSERT_EQ(shown.actionCalled(u"Show in Folder"_s), u"show"_s);
    ASSERT_EQ(shown.actionCalled(u"Open"_s), u"open"_s);

    server.press(shown.id, u"show"_s);
    ASSERT_TRUE(waitFor([&] { return opened.size() == 1; }));
    server.press(shown.id, u"default"_s);
    ASSERT_TRUE(waitFor([&] { return opened.size() == 2; }));
    EXPECT_EQ(opened.at(0), std::make_pair(u"show"_s, u"/recordings/Take 1.flac"_s));
    EXPECT_EQ(opened.at(1), std::make_pair(u"open"_s, u"/recordings/Take 1.flac"_s));
}

TEST_F(Notifications, TheDaemonSaysNothingThatIsNotItsToSay)
{
    notify::NotificationPolicy policy;
    notify::FreedesktopSender sender(QDBusConnection::sessionBus(), [](notify::Event) { return false; });
    QObject::connect(&policy, &notify::NotificationPolicy::notify, &sender, &notify::FreedesktopSender::send);
    policy.error(u"plugin.crashed"_s, json({{u"block"_s, u"p"_s}, {u"plugin"_s, u"clap:gate"_s}}));
    idle(300);
    EXPECT_TRUE(server.shown.isEmpty());
}

TEST_F(Notifications, TheWindowSendsThroughKNotificationsAndItsActionsWork)
{
    core::MockEngine engine;
    std::optional<ui::DesktopNotifier> notifier(std::in_place, &engine);
    // **Held while the window is there**, which is what keeps the daemon
    // quiet.
    const QString held = QString::fromLatin1(notify::kWindowNotifierName.data(),
                                             static_cast<qsizetype>(notify::kWindowNotifierName.size()));
    EXPECT_TRUE(QDBusConnection::sessionBus().interface()->isServiceRegistered(held).value());

    Q_EMIT engine.recordingFileFinished(u"s"_s, u"rec"_s, u"/recordings/Take 2.wav"_s, json({{u"seconds"_s, 3}}));
    Q_EMIT engine.error(u"plugin.crashed"_s, json({{u"block"_s, u"p"_s}, {u"plugin"_s, u"clap:gate"_s}}));
    Q_EMIT engine.error(u"session.notSaved"_s, json({{u"session"_s, u"x"_s}, {u"reason"_s, u"y"_s}}));
    ASSERT_TRUE(waitFor([&] { return server.shown.size() >= 2; }))
        << "KNotifications did not reach the server; is reverblabr.notifyrc on XDG_DATA_DIRS?";
    idle(300);
    ASSERT_EQ(server.shown.size(), 2) << "an error the window shows itself is not the desktop's";

    const FakeNotificationServer::Shown recording = server.shown.at(0);
    EXPECT_EQ(recording.summary, u"Recording finished"_s);
    EXPECT_TRUE(recording.body.contains("Take 2.wav"_L1)) << recording.body.toStdString();
    EXPECT_EQ(server.shown.at(1).summary, u"Plugin stopped"_s);

    const QString show = recording.actionCalled(u"Show in Folder"_s);
    const QString open = recording.actionCalled(u"Open"_s);
    ASSERT_FALSE(show.isEmpty()) << recording.actions.join(u'|').toStdString();
    ASSERT_FALSE(open.isEmpty()) << recording.actions.join(u'|').toStdString();
    server.press(recording.id, show);
    ASSERT_TRUE(waitFor([&] { return fileActions.size() == 1; }));
    EXPECT_EQ(fileActions.at(0), std::make_pair(ui::FileActions::Action::ShowInFolder, u"/recordings/Take 2.wav"_s));

    notifier.reset();
    EXPECT_TRUE(waitFor([&] { return !QDBusConnection::sessionBus().interface()->isServiceRegistered(held).value(); }))
        << "a closed window still said it was notifying";
}

TEST_F(Notifications, TheDaemonNotifiesOnlyWhileNoWindowDoes)
{
    // The daemon itself, on this bus, with the mock engine: a Recorder in a
    // session that is started and stopped finishes a file.
    QTemporaryDir home;
    ASSERT_TRUE(home.isValid());
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (const char *folder : {"data", "state", "config", "cache"}) {
        ASSERT_TRUE(QDir(home.path()).mkpath(QString::fromLatin1(folder)));
    }
    environment.insert(u"XDG_DATA_HOME"_s, home.filePath(u"data"_s));
    environment.insert(u"XDG_STATE_HOME"_s, home.filePath(u"state"_s));
    environment.insert(u"XDG_CONFIG_HOME"_s, home.filePath(u"config"_s));
    environment.insert(u"XDG_CACHE_HOME"_s, home.filePath(u"cache"_s));
    QProcess daemon;
    daemon.setProcessEnvironment(environment);
    daemon.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    daemon.start(QStringLiteral(RB_DAEMON), {u"--mock"_s, u"--idle-exit"_s, u"0"_s});
    ASSERT_TRUE(daemon.waitForStarted(5000));
    ASSERT_TRUE(waitFor([] {
        return QDBusConnection::sessionBus().interface()->isServiceRegistered(bus::kServiceName).value();
    })) << "the daemon never took its name";

    bus::RemoteEngine remote;
    QSignalSpy opened(&remote, &core::EngineFacade::sessionOpened);
    QSignalSpy states(&remote, &core::EngineFacade::stateChanged);
    remote.createSession(u"Notified"_s);
    ASSERT_TRUE(waitFor([&] { return opened.count() > 0; }));
    const QString sessionId = opened.at(0).at(0).toString();
    // **A Recorder, and a file name, of its own for each run**: the same file
    // twice would be held back as said once already (once a minute), and a
    // run could not tell whether the daemon kept quiet for the window. The
    // mock names a file by the session and the second, so two runs within
    // one would share one.
    const auto addRecorder = [&](const QString &id) {
        core::BlockInstance recorder;
        recorder.id = id;
        recorder.params.insert(u"fileName"_s, id);
        recorder.type = QString::fromUtf8(blocks::Recorder::kTypeId.data(),
                                          static_cast<qsizetype>(blocks::Recorder::kTypeId.size()));
        core::AddBlock add;
        add.block = recorder;
        remote.applyEdit(sessionId, QString::fromUtf8(QJsonDocument(core::editCommandToJson(add)).toJson()));
    };
    const auto removeRecorder = [&](const QString &id) {
        core::RemoveBlock remove;
        remove.blockId = id;
        remote.applyEdit(sessionId, QString::fromUtf8(QJsonDocument(core::editCommandToJson(remove)).toJson()));
    };
    addRecorder(u"first"_s);

    const auto runOnce = [&] {
        states.clear();
        remote.start(sessionId);
        EXPECT_TRUE(waitFor([&] {
            for (const QVariantList &state : std::as_const(states)) {
                if (state.at(1).toString() == "running"_L1) {
                    return true;
                }
            }
            return false;
        }));
        remote.stop(sessionId);
        EXPECT_TRUE(waitFor([&] {
            for (const QVariantList &state : std::as_const(states)) {
                if (state.at(1).toString() == "idle"_L1) {
                    return true;
                }
            }
            return false;
        }));
    };

    // No window: the daemon says it, though this test is its client.
    runOnce();
    ASSERT_TRUE(waitFor([&] { return server.shown.size() == 1; })) << "the daemon said nothing with no window open";
    EXPECT_EQ(server.shown.at(0).summary, u"Recording finished"_s);
    EXPECT_EQ(server.shown.at(0).actionCalled(u"Open"_s), u"open"_s);

    // A window: it says it itself, and the daemon keeps quiet. **Its
    // connection is closed only once nothing refers to it**, so the handle
    // lives in a scope of its own.
    {
        QDBusConnection window = QDBusConnection::connectToBus(QDBusConnection::SessionBus, u"window"_s);
        ASSERT_TRUE(window.registerService(QString::fromLatin1(
            notify::kWindowNotifierName.data(), static_cast<qsizetype>(notify::kWindowNotifierName.size()))));
    }
    removeRecorder(u"first"_s);
    addRecorder(u"second"_s);
    runOnce();
    idle(500);
    EXPECT_EQ(server.shown.size(), 1) << "the daemon spoke over an open window";

    // The window closed: the daemon speaks again.
    QDBusConnection::disconnectFromBus(u"window"_s);
    const QString held = QString::fromLatin1(notify::kWindowNotifierName.data(),
                                             static_cast<qsizetype>(notify::kWindowNotifierName.size()));
    ASSERT_TRUE(waitFor([&] { return !QDBusConnection::sessionBus().interface()->isServiceRegistered(held).value(); }))
        << "the window's name outlived its connection";
    removeRecorder(u"second"_s);
    addRecorder(u"third"_s);
    runOnce();
    ASSERT_TRUE(waitFor([&] { return server.shown.size() == 2; })) << "the daemon stayed quiet after the window went";
    EXPECT_TRUE(server.shown.at(0).body.contains("first"_L1)) << server.shown.at(0).body.toStdString();
    EXPECT_TRUE(server.shown.at(1).body.contains("third"_L1)) << server.shown.at(1).body.toStdString();

    daemon.terminate();
    EXPECT_TRUE(daemon.waitForFinished(5000));
}

int main(int argc, char **argv)
{
    // KNotifications draws nothing here, but wants a GUI application; the
    // platform is offscreen (tests/bus/CMakeLists.txt).
    QGuiApplication application(argc, argv);
    // The component KNotifications looks its reverblabr.notifyrc up by, as
    // the window's KAboutData makes it.
    QCoreApplication::setApplicationName(u"reverblabr"_s);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
