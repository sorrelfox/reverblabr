// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The sessions' timers as systemd user units: the text of each, and the
// units written, enabled, restarted, stopped and removed as the timers
// change.
//
// **Against a service manager of the test's own**, on a private bus
// (tests/bus/CMakeLists.txt), and into a unit directory of the test's own:
// the manager a person's desktop runs is never asked, and their
// ~/.config/systemd/user is never written. The test refuses to run on the
// real session bus.

#include "core/editcommand.h"
#include "core/enginefacade.h"
#include "core/mockengine.h"
#include "core/timers.h"
#include "timerschedule.h"
#include "timerunits.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
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

QString readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

core::Timer weekdays()
{
    core::Timer timer;
    timer.id = u"morning"_s;
    timer.repeat = core::TimerRepeat::Weekdays;
    timer.time = QTime(7, 30);
    timer.durationSeconds = 5400;
    return timer;
}

} // namespace

// --- the text of the units ---

TEST(TimerUnits, TheTimerUnitElapsesAtTheTimersTimesToTheSecond)
{
    const QString unit = daemon::timerunits::timerUnit(weekdays(), u"Morning radio"_s);
    EXPECT_TRUE(unit.startsWith(QLatin1StringView(daemon::timerunits::kWrittenBy) + u'\n')) << "marked as ours";
    EXPECT_TRUE(unit.contains(u"\nDescription=Reverblabr timer: Morning radio\n"_s)) << unit.toStdString();
    EXPECT_TRUE(unit.contains(u"\n[Timer]\nOnCalendar=Mon..Fri *-*-* 07:30:00\nAccuracySec=1s\nPersistent=false\n"_s))
        << unit.toStdString();
    EXPECT_TRUE(unit.contains(u"\nUnit=reverblabr-timer-morning.service\n"_s));
    EXPECT_TRUE(unit.contains(u"\n[Install]\nWantedBy=timers.target\n"_s));
}

TEST(TimerUnits, TheServiceRunsTimerFireOnce)
{
    const QString unit = daemon::timerunits::serviceUnit(weekdays(), u"Morning radio"_s, u"/usr/bin/reverblabr-ctl"_s);
    EXPECT_TRUE(
        unit.contains(u"\n[Service]\nType=oneshot\nExecStart=\"/usr/bin/reverblabr-ctl\" timer-fire morning\n"_s))
        << unit.toStdString();
    EXPECT_FALSE(unit.contains(u"[Install]"_s)) << "started by its timer, never at login";
}

// **A session's name is the person's**, and a unit file reads it as
// configuration: one line, no specifier, no continuation; and a path
// with spaces or `$` is still one word, taken as it is.
TEST(TimerUnits, ANameOrAPathCannotChangeWhatTheUnitSays)
{
    const QString name = u"50% louder\n[Service]\nExecStart=/bin/false \\"_s;
    const QString timer = daemon::timerunits::timerUnit(weekdays(), name);
    EXPECT_TRUE(timer.contains(u"\nDescription=Reverblabr timer: 50%% louder [Service] ExecStart=/bin/false /\n"_s))
        << timer.toStdString();
    EXPECT_EQ(timer.count(u"\n[Service]"_s), 0) << "only ever inside a line, never a section of its own";
    EXPECT_EQ(timer.count(u"\nExecStart"_s), 0);

    const QString service = daemon::timerunits::serviceUnit(weekdays(), name, u"/opt/My Apps/$HOME/%n/ctl"_s);
    EXPECT_TRUE(service.contains(u"\nExecStart=\"/opt/My Apps/$$HOME/%%n/ctl\" timer-fire morning\n"_s))
        << service.toStdString();
}

TEST(TimerUnits, OnlyOurFileNamesGiveATimerId)
{
    using daemon::timerunits::timerIdOf;
    EXPECT_EQ(timerIdOf(u"reverblabr-timer-morning.timer"_s), u"morning"_s);
    EXPECT_EQ(timerIdOf(u"reverblabr-timer-morning.service"_s), u"morning"_s);
    EXPECT_TRUE(timerIdOf(u"reverblabr-timer-morning.timer~"_s).isEmpty());
    EXPECT_TRUE(timerIdOf(u"reverblabrd.service"_s).isEmpty());
    EXPECT_TRUE(timerIdOf(u"reverblabr-timer-.timer"_s).isEmpty());
    EXPECT_TRUE(timerIdOf(u"reverblabr-timer-a.b.timer"_s).isEmpty());
}

// --- the units, against a manager ---

/// The user service manager's calls the schedule makes, and nothing else.
/// Each is written down in order; enabling a unit whose file is not there
/// is refused, as systemd refuses it.
class FakeManager : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.systemd1.Manager")

public:
    QString directory;
    QStringList calls;

public Q_SLOTS:
    QDBusObjectPath StopUnit(const QString &name, const QString &mode)
    {
        calls.append(u"stop "_s + name + u' ' + mode);
        return QDBusObjectPath(u"/org/freedesktop/systemd1/job/1"_s);
    }

    QDBusObjectPath RestartUnit(const QString &name, const QString &mode)
    {
        calls.append(u"restart "_s + name + u' ' + mode);
        return QDBusObjectPath(u"/org/freedesktop/systemd1/job/2"_s);
    }

    bool EnableUnitFiles(const QStringList &files, bool runtime, bool force, const QDBusMessage &message)
    {
        EXPECT_FALSE(runtime) << "enabled for good, not until the next boot";
        EXPECT_FALSE(force) << "never over somebody else's unit";
        for (const QString &file : files) {
            if (!QFile::exists(QDir(directory).filePath(file))) {
                message.setDelayedReply(true);
                QDBusConnection::connectToBus(QDBusConnection::SessionBus, u"fake-manager"_s)
                    .send(message.createErrorReply(u"org.freedesktop.DBus.Error.FileNotFound"_s, file));
                return false;
            }
        }
        calls.append(u"enable "_s + files.join(u','));
        return true;
    }

    void DisableUnitFiles(const QStringList &files, bool runtime)
    {
        EXPECT_FALSE(runtime);
        calls.append(u"disable "_s + files.join(u','));
    }

    void Reload()
    {
        calls.append(u"reload"_s);
    }
};

class TimerScheduleTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const QByteArray address = qgetenv("DBUS_SESSION_BUS_ADDRESS");
        ASSERT_FALSE(address.isEmpty());
        ASSERT_FALSE(address.contains(QByteArray("/run/user/")))
            << "this is the real session bus; run the test through ctest";
        ASSERT_TRUE(units.isValid());
        manager.directory = units.path();
        QDBusConnection fake = QDBusConnection::connectToBus(QDBusConnection::SessionBus, u"fake-manager"_s);
        ASSERT_TRUE(fake.isConnected());
        ASSERT_TRUE(fake.registerObject(u"/org/freedesktop/systemd1"_s, &manager, QDBusConnection::ExportAllSlots));
        ASSERT_TRUE(fake.registerService(u"org.freedesktop.systemd1"_s));

        engine.createSession(u"Morning radio"_s);
        ASSERT_EQ(opened.count(), 1);
        session = opened.at(0).at(0).toString();
    }

    void TearDown() override
    {
        schedule.reset();
        QDBusConnection fake = QDBusConnection::connectToBus(QDBusConnection::SessionBus, u"fake-manager"_s);
        fake.unregisterObject(u"/org/freedesktop/systemd1"_s);
        fake.unregisterService(u"org.freedesktop.systemd1"_s);
        QDBusConnection::disconnectFromBus(u"fake-manager"_s);
    }

    void startSchedule()
    {
        schedule = std::make_unique<daemon::TimerSchedule>(
            &engine, QDBusConnection::sessionBus(), units.path(), u"/usr/bin/reverblabr-ctl"_s, [this] {
                return engine.sessionListing();
            });
    }

    /// Waits for the schedule to look at the units and finish with them.
    bool settle()
    {
        QSignalSpy settled(schedule.get(), &daemon::TimerSchedule::settled);
        return settled.wait(5000);
    }

    void apply(const core::EditCommand &command)
    {
        engine.applyEdit(
            session, QString::fromUtf8(QJsonDocument(core::editCommandToJson(command)).toJson(QJsonDocument::Compact)));
    }

    QString path(const QString &file) const
    {
        return QDir(units.path()).filePath(file);
    }

    QTemporaryDir units;
    FakeManager manager;
    core::MockEngine engine;
    QSignalSpy opened{&engine, &core::EngineFacade::sessionOpened};
    QString session;
    std::unique_ptr<daemon::TimerSchedule> schedule;
};

// **A timer switched on becomes two units**, written, read by the manager,
// enabled for the next login and started, in that order.
TEST_F(TimerScheduleTest, ATimerThatIsOnIsWrittenReloadedEnabledAndStarted)
{
    startSchedule();
    ASSERT_TRUE(settle());
    EXPECT_TRUE(manager.calls.isEmpty()) << "no timers, nothing to ask";

    apply(core::SetTimer{core::timerToJson(weekdays())});
    ASSERT_TRUE(settle());
    EXPECT_EQ(manager.calls,
              (QStringList{u"reload"_s,
                           u"enable reverblabr-timer-morning.timer"_s,
                           u"restart reverblabr-timer-morning.timer replace"_s}));
    EXPECT_TRUE(readAll(path(u"reverblabr-timer-morning.timer"_s)).contains(u"OnCalendar=Mon..Fri *-*-* 07:30:00\n"_s));
    EXPECT_TRUE(readAll(path(u"reverblabr-timer-morning.service"_s))
                    .contains(u"ExecStart=\"/usr/bin/reverblabr-ctl\" timer-fire morning\n"_s));

    // **Nothing changed, nothing asked**: listing the sessions again,
    // or editing something else, leaves the units alone.
    manager.calls.clear();
    QSignalSpy settled(schedule.get(), &daemon::TimerSchedule::settled);
    apply(core::SetNotes{u"Weekday mornings."_s});
    engine.listSessions();
    ASSERT_EQ(settled.count(), 1) << "a list is looked at as it comes";
    QCoreApplication::processEvents();
    EXPECT_TRUE(manager.calls.isEmpty()) << manager.calls.join(u"; "_s).toStdString();
}

// **Edited, the units say the new times** and the timer is started again,
// so its next time is worked out from them.
TEST_F(TimerScheduleTest, EditingATimerRewritesItsUnitsAndRestartsIt)
{
    apply(core::SetTimer{core::timerToJson(weekdays())});
    startSchedule();
    ASSERT_TRUE(settle());
    manager.calls.clear();

    core::Timer later = weekdays();
    later.time = QTime(8, 15);
    later.repeat = core::TimerRepeat::Days;
    later.days = 0b1000001; // Monday and Sunday
    apply(core::SetTimer{core::timerToJson(later)});
    ASSERT_TRUE(settle());
    EXPECT_EQ(manager.calls,
              (QStringList{u"reload"_s,
                           u"enable reverblabr-timer-morning.timer"_s,
                           u"restart reverblabr-timer-morning.timer replace"_s}));
    EXPECT_TRUE(readAll(path(u"reverblabr-timer-morning.timer"_s)).contains(u"OnCalendar=Mon,Sun *-*-* 08:15:00\n"_s));

    // The session's new name is in its units' descriptions.
    manager.calls.clear();
    apply(core::Rename{QString(), u"Early radio"_s});
    ASSERT_TRUE(settle());
    EXPECT_TRUE(readAll(path(u"reverblabr-timer-morning.timer"_s)).contains(u"Reverblabr timer: Early radio\n"_s));
}

// **Switched off or deleted, its units go**: stopped and disabled through
// the manager first, then their files.
TEST_F(TimerScheduleTest, ATimerSwitchedOffOrDeletedIsStoppedDisabledAndRemoved)
{
    core::Timer second = weekdays();
    second.id = u"evening"_s;
    second.repeat = core::TimerRepeat::Daily;
    second.time = QTime(19, 0);
    apply(core::SetTimer{core::timerToJson(weekdays())});
    apply(core::SetTimer{core::timerToJson(second)});
    startSchedule();
    ASSERT_TRUE(settle());
    ASSERT_TRUE(QFile::exists(path(u"reverblabr-timer-evening.timer"_s)));
    manager.calls.clear();

    core::Timer off = weekdays();
    off.enabled = false;
    apply(core::SetTimer{core::timerToJson(off)});
    ASSERT_TRUE(settle());
    EXPECT_EQ(manager.calls,
              (QStringList{u"stop reverblabr-timer-morning.timer replace"_s,
                           u"disable reverblabr-timer-morning.timer"_s,
                           u"reload"_s}));
    EXPECT_FALSE(QFile::exists(path(u"reverblabr-timer-morning.timer"_s)));
    EXPECT_FALSE(QFile::exists(path(u"reverblabr-timer-morning.service"_s)));
    EXPECT_TRUE(QFile::exists(path(u"reverblabr-timer-evening.timer"_s))) << "the other timer is left as it is";

    manager.calls.clear();
    apply(core::RemoveTimer{u"evening"_s});
    ASSERT_TRUE(settle());
    EXPECT_EQ(manager.calls,
              (QStringList{u"stop reverblabr-timer-evening.timer replace"_s,
                           u"disable reverblabr-timer-evening.timer"_s,
                           u"reload"_s}));
    EXPECT_FALSE(QFile::exists(path(u"reverblabr-timer-evening.timer"_s)));
}

// **Only what was written here**: a unit somebody wrote by hand under one
// of our names is never touched, and a unit left behind by a session
// deleted while the engine was away goes as soon as the engine looks.
TEST_F(TimerScheduleTest, OnlyOurOwnUnitsAreEverRemoved)
{
    {
        QFile mine(path(u"reverblabr-timer-handmade.timer"_s));
        ASSERT_TRUE(mine.open(QIODevice::WriteOnly));
        mine.write("[Timer]\nOnCalendar=daily\n");
        QFile left(path(u"reverblabr-timer-gone.timer"_s));
        ASSERT_TRUE(left.open(QIODevice::WriteOnly));
        left.write(QByteArray(daemon::timerunits::kWrittenBy) + "\n[Timer]\nOnCalendar=daily\n");
        QFile leftService(path(u"reverblabr-timer-gone.service"_s));
        ASSERT_TRUE(leftService.open(QIODevice::WriteOnly));
        leftService.write(QByteArray(daemon::timerunits::kWrittenBy) + "\n[Service]\n");
    }
    ASSERT_TRUE(QDir(units.path()).mkpath(u"timers.target.wants"_s));
    ASSERT_TRUE(QFile::link(path(u"reverblabr-timer-gone.timer"_s),
                            path(u"timers.target.wants/reverblabr-timer-gone.timer"_s)));

    startSchedule();
    ASSERT_TRUE(settle());
    EXPECT_TRUE(QFile::exists(path(u"reverblabr-timer-handmade.timer"_s)));
    EXPECT_FALSE(QFile::exists(path(u"reverblabr-timer-gone.timer"_s)));
    EXPECT_FALSE(QFile::exists(path(u"reverblabr-timer-gone.service"_s)));
    EXPECT_FALSE(QFileInfo(path(u"timers.target.wants/reverblabr-timer-gone.timer"_s)).isSymLink());
    EXPECT_EQ(manager.calls,
              (QStringList{u"stop reverblabr-timer-gone.timer replace"_s,
                           u"disable reverblabr-timer-gone.timer"_s,
                           u"reload"_s}));
}

// **No service manager on the bus** (a desktop without a systemd user
// instance): the files are still kept in step, and nothing waits for an
// answer that will not come.
TEST_F(TimerScheduleTest, WithNoManagerTheFilesAreStillKeptInStep)
{
    QDBusConnection fake = QDBusConnection::connectToBus(QDBusConnection::SessionBus, u"fake-manager"_s);
    fake.unregisterObject(u"/org/freedesktop/systemd1"_s);
    fake.unregisterService(u"org.freedesktop.systemd1"_s);

    startSchedule();
    ASSERT_TRUE(settle());
    apply(core::SetTimer{core::timerToJson(weekdays())});
    ASSERT_TRUE(settle());
    EXPECT_TRUE(QFile::exists(path(u"reverblabr-timer-morning.timer"_s)));
    apply(core::RemoveTimer{u"morning"_s});
    ASSERT_TRUE(settle());
    EXPECT_FALSE(QFile::exists(path(u"reverblabr-timer-morning.timer"_s)));
    EXPECT_TRUE(manager.calls.isEmpty());
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

#include "test_timerschedule.moc"
