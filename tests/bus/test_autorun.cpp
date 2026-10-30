// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Auto Run (ADR-0021): the sessions set to run at login start with the
// background engine, and the engine's user unit is enabled exactly while
// there is one.
//
// **Against a service manager of the test's own**, on a private bus
// (tests/bus/CMakeLists.txt): the manager a person's desktop runs is never
// asked, let alone told to enable anything. The test refuses to run on the
// real session bus.

#include "autorun.h"
#include "autostartunit.h"
#include "blocks/builtinblocks.h"
#include "core/editcommand.h"
#include "core/engine.h"
#include "core/enginefacade.h"
#include "core/mockengine.h"
#include "core/sessionstore.h"
#include "dsp/blockregistry.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

using namespace rb;
using namespace Qt::StringLiterals;
using State = daemon::AutostartUnit::State;

namespace
{

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

void idle(int milliseconds)
{
    (void)waitFor([] { return false; }, milliseconds);
}

const QString kUnit = QLatin1StringView(daemon::AutostartUnit::kUnitName);

} // namespace

/// The user service manager's four calls Auto Run makes, and nothing else.
/// Enabling answers only the flag, not systemd's list of changes, since
/// AutostartUnit reads nothing of the answer but whether it is an error.
class FakeManager : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.systemd1.Manager")

public:
    bool installed = true;
    bool masked = false;
    bool enabled = false;
    int enables = 0;
    /// Every EnableUnitFiles, refused or not.
    int enableCalls = 0;
    int disables = 0;
    int reloads = 0;

public Q_SLOTS:
    QString GetUnitFileState(const QString &file, const QDBusMessage &message)
    {
        if (!installed || file != kUnit) {
            refuse(message, u"org.freedesktop.DBus.Error.FileNotFound"_s, u"No such file or directory"_s);
            return {};
        }
        if (masked) {
            return u"masked"_s;
        }
        return enabled ? u"enabled"_s : u"disabled"_s;
    }

    bool EnableUnitFiles(const QStringList &files, bool runtime, bool force, const QDBusMessage &message)
    {
        Q_UNUSED(force)
        ++enableCalls;
        if (!installed || files != QStringList{kUnit}) {
            refuse(message, u"org.freedesktop.DBus.Error.FileNotFound"_s, u"No such file or directory"_s);
            return false;
        }
        if (masked) {
            refuse(message, u"org.freedesktop.systemd1.UnitMasked"_s, u"Unit file is masked."_s);
            return false;
        }
        EXPECT_FALSE(runtime) << "enabled for good, not until the next boot";
        enabled = true;
        ++enables;
        return true;
    }

    void DisableUnitFiles(const QStringList &files, bool runtime, const QDBusMessage &message)
    {
        if (!installed || files != QStringList{kUnit}) {
            refuse(message, u"org.freedesktop.DBus.Error.FileNotFound"_s, u"No such file or directory"_s);
            return;
        }
        EXPECT_FALSE(runtime);
        enabled = false;
        ++disables;
    }

    void Reload()
    {
        ++reloads;
    }

private:
    static void refuse(const QDBusMessage &message, const QString &name, const QString &text)
    {
        message.setDelayedReply(true);
        QDBusConnection::connectToBus(QDBusConnection::SessionBus, u"fake-manager"_s)
            .send(message.createErrorReply(name, text));
    }
};

/// Every test has the manager, when it has one, on a connection of its own,
/// and Auto Run on the ordinary one.
class AutoRunTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const QByteArray address = qgetenv("DBUS_SESSION_BUS_ADDRESS");
        ASSERT_FALSE(address.isEmpty());
        ASSERT_FALSE(address.contains(QByteArray("/run/user/")))
            << "this is the real session bus; run the test through ctest";
    }

    void TearDown() override
    {
        if (m_managerUp) {
            QDBusConnection fake = QDBusConnection::connectToBus(QDBusConnection::SessionBus, u"fake-manager"_s);
            fake.unregisterObject(u"/org/freedesktop/systemd1"_s);
            fake.unregisterService(u"org.freedesktop.systemd1"_s);
        }
        QDBusConnection::disconnectFromBus(u"fake-manager"_s);
    }

    /// Puts the fake manager on the bus, as systemd's user instance would be.
    void startManager()
    {
        QDBusConnection fake = QDBusConnection::connectToBus(QDBusConnection::SessionBus, u"fake-manager"_s);
        ASSERT_TRUE(fake.isConnected());
        ASSERT_TRUE(fake.registerObject(u"/org/freedesktop/systemd1"_s, &manager, QDBusConnection::ExportAllSlots));
        ASSERT_TRUE(fake.registerService(u"org.freedesktop.systemd1"_s));
        m_managerUp = true;
    }

    static QString setAutoRun(const QString &value)
    {
        return QString::fromUtf8(
            QJsonDocument(QJsonObject{{"op"_L1, u"setSessionSetting"_s}, {"key"_L1, u"autoRun"_s}, {"value"_L1, value}})
                .toJson(QJsonDocument::Compact));
    }

    FakeManager manager;

private:
    bool m_managerUp = false;
};

// **The unit follows the setting**: enabled once a session is set to run at
// login, and reloaded as `systemctl enable` does; disabled again once none
// is.
TEST_F(AutoRunTest, ASessionSetToRunAtLoginEnablesTheUnitAndTurningItOffDisablesIt)
{
    startManager();
    core::MockEngine engine;
    QSignalSpy opened(&engine, &core::EngineFacade::sessionOpened);
    engine.createSession(u"Radio"_s);
    ASSERT_EQ(opened.count(), 1);
    const QString id = opened.at(0).at(0).toString();

    daemon::AutostartUnit unit(QDBusConnection::sessionBus());
    daemon::AutoRun autoRun(&engine, &unit, [&engine] { return engine.sessionListing(); });
    ASSERT_TRUE(waitFor([&] { return unit.state() == State::Disabled; })) << "the unit was never asked about";
    idle(300);
    EXPECT_EQ(manager.enables + manager.disables, 0) << "nothing to change while no session wants it";

    engine.applyEdit(id, setAutoRun(u"login"_s));
    ASSERT_TRUE(waitFor([&] { return unit.state() == State::Enabled; })) << "the unit was never enabled";
    EXPECT_TRUE(manager.enabled);
    EXPECT_EQ(manager.enables, 1);
    EXPECT_GE(manager.reloads, 1) << "enabled without reloading the manager";
    EXPECT_EQ(autoRun.wanted(), std::optional<bool>(true));

    engine.applyEdit(id, setAutoRun(u"off"_s));
    ASSERT_TRUE(waitFor([&] { return unit.state() == State::Disabled; })) << "the unit was left enabled";
    EXPECT_FALSE(manager.enabled);
    EXPECT_EQ(manager.disables, 1);
    idle(300);
    EXPECT_EQ(manager.enables, 1) << "asked again and again";
}

// **A unit left enabled, with no session that wants it**, is disabled when
// the engine starts: by a session since deleted, or by an older version.
TEST_F(AutoRunTest, AUnitLeftEnabledWithNoSessionThatWantsItIsDisabled)
{
    startManager();
    manager.enabled = true;
    core::MockEngine engine;
    engine.createSession(u"Radio"_s);

    daemon::AutostartUnit unit(QDBusConnection::sessionBus());
    daemon::AutoRun autoRun(&engine, &unit, [&engine] { return engine.sessionListing(); });
    ASSERT_TRUE(waitFor([&] { return manager.disables == 1; })) << "the stale unit was left enabled";
    EXPECT_TRUE(waitFor([&] { return unit.state() == State::Disabled; }));
}

// **Masked is somebody's decision**: said, never undone from here.
TEST_F(AutoRunTest, AMaskedUnitIsLeftAlone)
{
    startManager();
    manager.masked = true;
    core::MockEngine engine;
    QSignalSpy opened(&engine, &core::EngineFacade::sessionOpened);
    engine.createSession(u"Radio"_s);
    daemon::AutostartUnit unit(QDBusConnection::sessionBus());
    daemon::AutoRun autoRun(&engine, &unit, [&engine] { return engine.sessionListing(); });
    engine.applyEdit(opened.at(0).at(0).toString(), setAutoRun(u"login"_s));

    ASSERT_TRUE(waitFor([&] { return unit.state() == State::Masked; }));
    idle(400);
    EXPECT_EQ(manager.enableCalls, 0) << "tried to enable a masked unit";
}

// **Not installed** (a build tree): said, and nothing is enabled.
TEST_F(AutoRunTest, WithoutTheUnitInstalledNothingIsEnabled)
{
    startManager();
    manager.installed = false;
    core::MockEngine engine;
    QSignalSpy opened(&engine, &core::EngineFacade::sessionOpened);
    engine.createSession(u"Radio"_s);
    daemon::AutostartUnit unit(QDBusConnection::sessionBus());
    daemon::AutoRun autoRun(&engine, &unit, [&engine] { return engine.sessionListing(); });
    engine.applyEdit(opened.at(0).at(0).toString(), setAutoRun(u"login"_s));

    ASSERT_TRUE(waitFor([&] { return unit.state() == State::NotInstalled; })) << int(unit.state());
    idle(400);
    EXPECT_EQ(manager.enableCalls, 0) << "tried to enable a unit that is not there";
}

// **No user service manager on the bus**: said, and nothing breaks.
TEST_F(AutoRunTest, WithoutAServiceManagerTheStateSaysSo)
{
    core::MockEngine engine;
    daemon::AutostartUnit unit(QDBusConnection::sessionBus());
    daemon::AutoRun autoRun(&engine, &unit, [&engine] { return engine.sessionListing(); });
    EXPECT_TRUE(waitFor([&] { return unit.state() == State::NoManager; })) << int(unit.state());
}

// **Looking is not announcing**: however often Auto Run looks at the
// sessions, no SessionsListed goes out, which on the bus would reach every
// window.
TEST_F(AutoRunTest, LookingAtTheSessionsSaysNothingOnTheBus)
{
    startManager();
    core::MockEngine engine;
    QSignalSpy opened(&engine, &core::EngineFacade::sessionOpened);
    QSignalSpy listed(&engine, &core::EngineFacade::sessionsListed);
    engine.createSession(u"Radio"_s);
    daemon::AutostartUnit unit(QDBusConnection::sessionBus());
    daemon::AutoRun autoRun(&engine, &unit, [&engine] { return engine.sessionListing(); });
    autoRun.start();
    engine.applyEdit(opened.at(0).at(0).toString(), setAutoRun(u"login"_s));
    ASSERT_TRUE(waitFor([&] { return unit.state() == State::Enabled; }));
    EXPECT_EQ(listed.count(), 0);
}

/// A real engine on a folder of its own, with no PipeWire behind it: a
/// session it is asked to start fails to run, which is not what is tested.
class AutoRunEngineTest : public AutoRunTest
{
protected:
    void SetUp() override
    {
        AutoRunTest::SetUp();
        ASSERT_TRUE(folder.isValid());
        ASSERT_TRUE(blocks::registerBuiltinBlocks(registry));
        engine = std::make_unique<core::Engine>(registry, core::SessionStore{folder.path()});
    }

    /// Makes a session called \a name set to \a autoRun, and leaves it open
    /// or closes it.
    QString makeSession(const QString &name, const QString &autoRun, bool keepOpen)
    {
        QSignalSpy opened(engine.get(), &core::EngineFacade::sessionOpened);
        engine->createSession(name);
        EXPECT_EQ(opened.count(), 1);
        const QString id = opened.at(0).at(0).toString();
        engine->applyEdit(id, setAutoRun(autoRun));
        engine->saveSession(id);
        if (!keepOpen) {
            engine->closeSession(id);
        }
        return id;
    }

    QTemporaryDir folder;
    dsp::BlockRegistry registry;
    std::unique_ptr<core::Engine> engine;
};

// **At start, every session set to run at login is started**: one only on
// disk is opened first, one already open is started where it is, and one
// set to off is left alone. Without start() — safe mode — none is.
TEST_F(AutoRunEngineTest, AtStartTheSessionsSetToRunAtLoginAreOpenedAndStarted)
{
    startManager();
    const QString radio = makeSession(u"Radio"_s, u"login"_s, false);
    const QString show = makeSession(u"Show"_s, u"login"_s, true);
    const QString call = makeSession(u"Call"_s, u"off"_s, false);
    QSignalSpy opened(engine.get(), &core::EngineFacade::sessionOpened);

    daemon::AutostartUnit unit(QDBusConnection::sessionBus());
    daemon::AutoRun autoRun(engine.get(), &unit, [this] { return engine->sessionListing(); });
    QSignalSpy started(&autoRun, &daemon::AutoRun::started);
    idle(300);
    EXPECT_EQ(started.count(), 0) << "started without being asked to: safe mode would start sessions";

    autoRun.start();
    ASSERT_TRUE(waitFor([&] { return started.count() == 2; })) << started.count() << " started";
    QStringList ids;
    for (const QList<QVariant> &entry : started) {
        ids.append(entry.at(0).toString());
    }
    ids.sort();
    QStringList expected{radio, show};
    expected.sort();
    EXPECT_EQ(ids, expected);
    EXPECT_TRUE(std::ranges::any_of(opened, [&](const QList<QVariant> &entry) { return entry.at(0) == radio; }))
        << "the session only on disk was never opened";
    EXPECT_FALSE(std::ranges::any_of(opened, [&](const QList<QVariant> &entry) { return entry.at(0) == call; }))
        << "a session set to off was opened";
    EXPECT_TRUE(waitFor([&] { return unit.state() == State::Enabled; }));
}

// **A session deleted without being open** says nothing, so the unit is put
// right by the next list anybody asks for — the Sessions window's, after
// deleting it.
TEST_F(AutoRunEngineTest, ASessionDeletedWhileClosedIsNoticedFromTheNextList)
{
    startManager();
    const QString radio = makeSession(u"Radio"_s, u"login"_s, false);
    daemon::AutostartUnit unit(QDBusConnection::sessionBus());
    daemon::AutoRun autoRun(engine.get(), &unit, [this] { return engine->sessionListing(); });
    ASSERT_TRUE(waitFor([&] { return unit.state() == State::Enabled; }));

    engine->deleteSession(radio);
    engine->listSessions();
    EXPECT_TRUE(waitFor([&] { return unit.state() == State::Disabled; })) << "left enabled for a deleted session";
    EXPECT_EQ(manager.disables, 1);
}

// **When Auto Run runs is read from the window's settings file**, as
// KConfig writes it (test_autorunsetting.cpp holds the window to that text),
// among the window's other groups. Nothing written, no file, or a word this
// version does not know is the default, once a login.
TEST(AutoRunSettingFile, EveryEngineStartIsReadFromTheWindowsSettings)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(u"reverblabrrc"_s);
    EXPECT_FALSE(daemon::autoRunEveryEngineStart(path)) << "no file";
    EXPECT_FALSE(daemon::autoRunEveryEngineStart(QString()));

    const auto write = [&path](const QByteArray &text) {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        ASSERT_EQ(file.write(text), text.size());
    };
    ASSERT_NO_FATAL_FAILURE(write("[Appearance]\nMode=system\n\n[AutoRun]\nStart=every-engine-start\n"));
    EXPECT_TRUE(daemon::autoRunEveryEngineStart(path));
    ASSERT_NO_FATAL_FAILURE(write("[AutoRun]\nStart=once-a-login\n"));
    EXPECT_FALSE(daemon::autoRunEveryEngineStart(path));
    ASSERT_NO_FATAL_FAILURE(write("[AutoRun]\nStart=something-later\n"));
    EXPECT_FALSE(daemon::autoRunEveryEngineStart(path));
    ASSERT_NO_FATAL_FAILURE(write("[Appearance]\nMode=system\n"));
    EXPECT_FALSE(daemon::autoRunEveryEngineStart(path));
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

#include "test_autorun.moc"
