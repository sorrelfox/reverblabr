// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// **Reverblabr's global shortcuts**, registered through the real
// KGlobalAccel with a kglobalaccel of the test's own on a private bus, in a
// PID namespace of its own (tests/bus/CMakeLists.txt). It must never run on
// a desktop's bus: it would put Reverblabr's actions in that desktop's
// System Settings.
//
// What is registered is read from the fake daemon, and the person's side is
// played by it too: a key pressed, a key set in System Settings.

#include "core/editcommand.h"
#include "core/mockengine.h"
#include "globalshortcutregistry.h"
#include "globalshortcuts.h"
#include "windowstate.h"

#include <KGlobalAccel>
#include <KSharedConfig>
#include <QAction>
#include <QApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QKeySequence>
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

const QString kService = u"org.kde.kglobalaccel"_s;
const QString kPath = u"/kglobalaccel"_s;
const QString kTestInterface = u"org.reverblabr.Test"_s;
/// What the application's name makes the component (main() below).
const QString kComponent = u"reverblabr"_s;

class Shortcuts : public ::testing::Test
{
protected:
    /// **One daemon for the whole run**, as on a desktop: KGlobalAccel is a
    /// process-wide singleton that remembers what it registered.
    static void SetUpTestSuite()
    {
        daemon = new QProcess;
        daemon->setProcessChannelMode(QProcess::ForwardedErrorChannel);
        daemon->start(QStringLiteral(RB_FAKE_KGLOBALACCEL));
        (void)daemon->waitForStarted(5000);
        (void)waitFor([] { return QDBusConnection::sessionBus().interface()->isServiceRegistered(kService).value(); });
    }

    static void TearDownTestSuite()
    {
        daemon->terminate();
        if (!daemon->waitForFinished(5000)) {
            daemon->kill();
            daemon->waitForFinished(5000);
        }
        delete daemon;
        daemon = nullptr;
    }

    void SetUp() override
    {
        // **Refuse to run anywhere but a private bus**: registered on a
        // desktop's bus, these would be in that desktop's System Settings.
        const QByteArray address = qgetenv("DBUS_SESSION_BUS_ADDRESS");
        ASSERT_FALSE(address.isEmpty());
        ASSERT_FALSE(address.contains(QByteArray("/run/user/")))
            << "this is the real session bus; run it through ctest";
        ASSERT_TRUE(QDBusConnection::sessionBus().interface()->isServiceRegistered(kService).value())
            << "the test's kglobalaccel did not start";
        ASSERT_TRUE(probe.isConnected());
        // The sessions come from the Sessions window's list, which reads its
        // columns from a state file: one of the test's own.
        ASSERT_TRUE(state.isValid());
        ui::WindowState::setConfig(
            KSharedConfig::openConfig(state.filePath(u"reverblabrstaterc"_s), KConfig::SimpleConfig));
    }

    void TearDown() override
    {
        ui::WindowState::setConfig({});
        QDBusConnection::disconnectFromBus(u"shortcuts-probe"_s);
    }

    /// Calls the daemon's test interface and waits, turning the event loop
    /// meanwhile: this thread answers the daemon's signals.
    [[nodiscard]] QDBusMessage ask(const QString &member, const QVariantList &arguments = {})
    {
        QDBusMessage message = QDBusMessage::createMethodCall(kService, kPath, kTestInterface, member);
        message.setArguments(arguments);
        QDBusPendingCall pending = probe.asyncCall(message, 5000);
        (void)waitFor([&pending] { return pending.isFinished(); });
        return pending.reply();
    }

    [[nodiscard]] QStringList registered()
    {
        return ask(u"Registered"_s).arguments().value(0).toStringList();
    }

    /// The daemon's line for \a actionId, or empty.
    [[nodiscard]] QString entryOf(const QString &actionId)
    {
        const QString prefix = kComponent + u'/' + actionId + u'\t';
        for (const QString &line : registered()) {
            if (line.startsWith(prefix)) {
                return line.mid(prefix.size());
            }
        }
        return {};
    }

    void press(const QString &actionId)
    {
        ASSERT_EQ(ask(u"Press"_s, {kComponent, actionId}).type(), QDBusMessage::ReplyMessage);
    }

    [[nodiscard]] static QString openSession(core::MockEngine &engine, const QString &name)
    {
        QSignalSpy opened(&engine, &core::EngineFacade::sessionOpened);
        engine.createSession(name);
        return opened.isEmpty() ? QString{} : opened.at(0).at(0).toString();
    }

    [[nodiscard]] static bool running(core::MockEngine &engine, const QString &sessionId)
    {
        QSignalSpy state(&engine, &core::EngineFacade::stateChanged);
        engine.getSessionState(sessionId);
        return !state.isEmpty() && state.constLast().at(1).toString() == core::sessionState::running;
    }

    [[nodiscard]] static QString sessionAction(const QString &sessionId)
    {
        return QString(ui::GlobalShortcuts::kSessionPrefix) + sessionId;
    }

    static inline QProcess *daemon = nullptr;
    QDBusConnection probe = QDBusConnection::connectToBus(QDBusConnection::SessionBus, u"shortcuts-probe"_s);
    QTemporaryDir state;
};

} // namespace

TEST_F(Shortcuts, EveryActionIsInSystemSettingsWithNoDefaultKeyAndAPressComesBack)
{
    core::MockEngine engine;
    const QString podcast = openSession(engine, u"Podcast"_s);
    const QString call = openSession(engine, u"Call"_s);
    ui::GlobalShortcuts shortcuts(&engine);
    const ui::GlobalShortcutRegistry registry(&shortcuts);

    ASSERT_TRUE(waitFor([&] { return !entryOf(sessionAction(call)).isEmpty(); }))
        << "the sessions' actions never registered";
    for (const ui::GlobalShortcuts::Action &action : ui::GlobalShortcuts::fixedActions()) {
        EXPECT_EQ(entryOf(action.id), action.text + u"\tactive"_s) << action.id.toStdString();
    }
    EXPECT_EQ(entryOf(sessionAction(podcast)), u"Run or stop “Podcast”\tactive"_s);
    EXPECT_EQ(entryOf(sessionAction(call)), u"Run or stop “Call”\tactive"_s);
    // **No default keys**: whatever key the person picks, none was forced on
    // them.
    EXPECT_EQ(ask(u"DefaultKeyCount"_s).arguments().value(0).toInt(), 0);

    press(sessionAction(podcast));
    ASSERT_TRUE(waitFor([&] { return running(engine, podcast); })) << "a press did not run the session";
    EXPECT_FALSE(running(engine, call));

    QSignalSpy shown(&shortcuts, &ui::GlobalShortcuts::showRequested);
    press(QString(ui::GlobalShortcuts::kShowWindow));
    ASSERT_TRUE(waitFor([&] { return shown.count() == 1; })) << "Show Reverblabr was not passed on";

    QSignalSpy block(&shortcuts, &ui::GlobalShortcuts::blockActionRequested);
    press(QString(ui::GlobalShortcuts::kTimeShiftBack));
    ASSERT_TRUE(waitFor([&] { return block.count() == 1; }));
    EXPECT_EQ(block.at(0).at(0).toString(), QString(ui::GlobalShortcuts::kTimeShiftBack));
}

TEST_F(Shortcuts, AKeySetInSystemSettingsIsTheKeyTheApplicationHas)
{
    core::MockEngine engine;
    ui::GlobalShortcuts shortcuts(&engine);
    const ui::GlobalShortcutRegistry registry(&shortcuts);
    QAction *show = registry.action(QString(ui::GlobalShortcuts::kShowWindow));
    ASSERT_NE(show, nullptr);
    ASSERT_TRUE(waitFor([&] { return !entryOf(QString(ui::GlobalShortcuts::kShowWindow)).isEmpty(); }));

    const QKeySequence key(Qt::META | Qt::SHIFT | Qt::Key_F9);
    ASSERT_EQ(ask(u"Rebind"_s, {kComponent, QString(ui::GlobalShortcuts::kShowWindow), key[0].toCombined()}).type(),
              QDBusMessage::ReplyMessage);
    ASSERT_TRUE(waitFor([&] { return KGlobalAccel::self()->shortcut(show) == QList<QKeySequence>{key}; }))
        << "the key set in System Settings did not reach the application";
}

TEST_F(Shortcuts, ARenamedSessionKeepsItsKeyAndADeletedOneTakesItsKeyWithIt)
{
    core::MockEngine engine;
    const QString kept = openSession(engine, u"Morning"_s);
    const QString deleted = openSession(engine, u"Old"_s);
    ui::GlobalShortcuts shortcuts(&engine);
    const ui::GlobalShortcutRegistry registry(&shortcuts);
    ASSERT_TRUE(waitFor([&] { return !entryOf(sessionAction(deleted)).isEmpty(); }));

    const QKeySequence key(Qt::META | Qt::SHIFT | Qt::Key_F10);
    ASSERT_EQ(ask(u"Rebind"_s, {kComponent, sessionAction(kept), key[0].toCombined()}).type(),
              QDBusMessage::ReplyMessage);
    ASSERT_TRUE(waitFor([&] {
        return KGlobalAccel::self()->shortcut(registry.action(sessionAction(kept))) == QList<QKeySequence>{key};
    }));

    engine.applyEdit(kept,
                     QString::fromUtf8(QJsonDocument(core::editCommandToJson(core::Rename{QString{}, u"Breakfast"_s}))
                                           .toJson(QJsonDocument::Compact)));
    ASSERT_TRUE(waitFor([&] { return entryOf(sessionAction(kept)) == u"Run or stop “Breakfast”\tactive"_s; }))
        << "System Settings still shows " << entryOf(sessionAction(kept)).toStdString();
    EXPECT_EQ(KGlobalAccel::self()->shortcut(registry.action(sessionAction(kept))), QList<QKeySequence>{key})
        << "the renamed session lost its key";

    engine.deleteSession(deleted);
    ASSERT_TRUE(waitFor([&] {
        return ask(u"Removed"_s)
            .arguments()
            .value(0)
            .toStringList()
            .contains(kComponent + u'/' + sessionAction(deleted));
    })) << "the deleted session's key was kept";
    EXPECT_TRUE(entryOf(sessionAction(deleted)).isEmpty());
    EXPECT_EQ(registry.action(sessionAction(deleted)), nullptr);
}

int main(int argc, char **argv)
{
    // QAction takes a GUI application; the platform is offscreen
    // (tests/bus/CMakeLists.txt). KGlobalAccel names the component after
    // the application, as main() does for the real one.
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(u"reverblabr"_s);
    QGuiApplication::setApplicationDisplayName(u"Reverblabr"_s);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
