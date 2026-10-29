// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What is worth a notification, and in what words.

#include "notifications.h"
#include "texts.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTest>
#include <gtest/gtest.h>

using namespace rb::notify;
using namespace std::chrono_literals;
using namespace Qt::StringLiterals;

namespace
{

void needApplication()
{
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char name[] = "reverblabr-notify-test";
        static char *argv[] = {name, nullptr};
        static QCoreApplication application(argc, argv);
    }
}

[[nodiscard]] QString json(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

/// A policy whose waits are short enough for a test, and what it said.
class Said : public ::testing::Test
{
protected:
    void SetUp() override
    {
        needApplication();
        QObject::connect(&policy, &NotificationPolicy::notify, &policy, [this](const Notification &notification) {
            said.append(notification);
        });
    }

    /// Turns the event loop for \a time, so the policy's waits can end.
    static void wait(std::chrono::milliseconds time)
    {
        QTest::qWait(static_cast<int>(time.count()));
    }

    static constexpr std::chrono::milliseconds kGrace{60};
    NotificationPolicy policy{NotificationPolicy::Delays{kGrace, kGrace, 60s}};
    QList<Notification> said;
};

[[nodiscard]] QJsonObject deviceArgs(const QString &block = u"mic"_s)
{
    return {{u"block"_s, block}, {u"session"_s, u"Podcast"_s}, {u"device"_s, u"USB Microphone"_s}};
}

[[nodiscard]] QJsonObject captureArgs()
{
    return {{u"block"_s, u"app"_s}, {u"session"_s, u"Podcast"_s}, {u"application"_s, u"Firefox"_s}};
}

} // namespace

TEST_F(Said, TheErrorsWorthTellingBecomeTheirEvents)
{
    policy.error(u"plugin.crashed"_s,
                 json({{u"block"_s, u"eq"_s}, {u"plugin"_s, u"lv2:http://lsp-plug.in/plugins/lv2/comp_mono"_s}}));
    policy.error(u"plugin.stoppedResponding"_s, json({{u"block"_s, u"gate"_s}, {u"plugin"_s, u"clap:gate"_s}}));
    policy.error(u"recording.lowDiskSpace"_s,
                 json({{u"block"_s, u"rec"_s}, {u"file"_s, u"/tmp/Take 1.flac"_s}, {u"megabytes"_s, u"512"_s}}));
    policy.error(u"routing.feedbackLoop"_s, json({{u"path"_s, u"a > b > a"_s}}));
    // The window shows these on the canvas; they are not the desktop's.
    policy.error(u"session.notSaved"_s, json({{u"session"_s, u"x"_s}, {u"reason"_s, u"y"_s}}));
    policy.error(u"edit.refused"_s, json({{u"reason"_s, u"y"_s}}));

    ASSERT_EQ(said.size(), 4);
    EXPECT_EQ(said.at(0).event, Event::PluginFailed);
    EXPECT_TRUE(said.at(0).text.contains("comp_mono"_L1)) << said.at(0).text.toStdString();
    EXPECT_FALSE(said.at(0).text.contains("http"_L1)) << "a plugin's id is not its name";
    EXPECT_EQ(said.at(1).event, Event::PluginFailed);
    EXPECT_NE(said.at(1).text, said.at(0).text) << "stopped responding is not crashed";
    EXPECT_EQ(said.at(2).event, Event::LowDiskSpace);
    EXPECT_TRUE(said.at(2).text.contains("Take 1.flac"_L1));
    EXPECT_EQ(said.at(3).event, Event::FeedbackLoop);
    for (const Notification &notification : std::as_const(said)) {
        EXPECT_TRUE(notification.file.isEmpty()) << "only a recording has actions";
    }
}

TEST_F(Said, AFinishedRecordingIsItsFileAndHowLongItIs)
{
    policy.recordingFileFinished(u"s"_s, u"rec"_s, u"/home/someone/Music/Take 3.wav"_s, json({{u"seconds"_s, 65.2}}));
    ASSERT_EQ(said.size(), 1);
    EXPECT_EQ(said.at(0).event, Event::RecordingFinished);
    EXPECT_EQ(said.at(0).file, u"/home/someone/Music/Take 3.wav"_s);
    EXPECT_TRUE(said.at(0).text.contains("Take 3.wav"_L1)) << said.at(0).text.toStdString();
    EXPECT_TRUE(said.at(0).text.contains("1:05"_L1)) << said.at(0).text.toStdString();
    EXPECT_FALSE(said.at(0).text.contains("/home"_L1)) << "the name, not where it is";
}

TEST_F(Said, ADeviceBackWithinTheGraceWasNeverLost)
{
    policy.notice(u"device.lost"_s, json(deviceArgs()));
    wait(kGrace / 3);
    policy.notice(u"device.returned"_s, json(deviceArgs()));
    wait(kGrace * 3);
    EXPECT_TRUE(said.isEmpty()) << "a USB device re-enumerating is not news";

    policy.notice(u"device.lost"_s, json(deviceArgs()));
    EXPECT_TRUE(said.isEmpty()) << "not before the grace is over";
    wait(kGrace * 3);
    ASSERT_EQ(said.size(), 1);
    EXPECT_EQ(said.at(0).event, Event::DeviceLost);
    EXPECT_TRUE(said.at(0).text.contains("USB Microphone"_L1));
    EXPECT_TRUE(said.at(0).text.contains("Podcast"_L1));
}

TEST_F(Said, AnApplicationIsPlayingAgainOnlyAfterItsLossWasSaid)
{
    // A window that connects to the daemon after an application's loss hears
    // it come back without having said it went: that is not news here.
    policy.notice(u"capture.regained"_s, json(captureArgs()));
    EXPECT_TRUE(said.isEmpty()) << "playing again, without ever having stopped";

    // Quiet for a moment between two sounds: nothing either way.
    policy.notice(u"capture.lost"_s, json(captureArgs()));
    wait(kGrace / 3);
    policy.notice(u"capture.regained"_s, json(captureArgs()));
    wait(kGrace * 3);
    EXPECT_TRUE(said.isEmpty());

    policy.notice(u"capture.lost"_s, json(captureArgs()));
    wait(kGrace * 3);
    policy.notice(u"capture.regained"_s, json(captureArgs()));
    ASSERT_EQ(said.size(), 2);
    EXPECT_EQ(said.at(0).event, Event::CaptureLost);
    EXPECT_EQ(said.at(1).event, Event::CaptureRegained);
    EXPECT_TRUE(said.at(1).text.contains("Firefox"_L1));

    // Playing again twice is still one "again".
    policy.notice(u"capture.regained"_s, json(captureArgs()));
    EXPECT_EQ(said.size(), 2);
}

TEST_F(Said, TheSameThingAboutTheSameBlockIsSaidOnceAMinute)
{
    const QJsonObject eq{{u"block"_s, u"eq"_s}, {u"plugin"_s, u"clap:eq"_s}};
    const QJsonObject gate{{u"block"_s, u"gate"_s}, {u"plugin"_s, u"clap:gate"_s}};
    policy.error(u"plugin.crashed"_s, json(eq));
    policy.error(u"plugin.crashed"_s, json(eq));
    policy.error(u"plugin.crashed"_s, json(gate));
    EXPECT_EQ(said.size(), 2) << "a plugin crashing on every restart is one notification a minute";

    // Every file is its own news.
    policy.recordingFileFinished(u"s"_s, u"rec"_s, u"/a.wav"_s, json({{u"seconds"_s, 1}}));
    policy.recordingFileFinished(u"s"_s, u"rec"_s, u"/b.wav"_s, json({{u"seconds"_s, 1}}));
    EXPECT_EQ(said.size(), 4);
}

TEST_F(Said, EveryEventHasWordsAndNothingLeftToFillIn)
{
    policy.recordingFileFinished(u"s"_s, u"rec"_s, u"/a.wav"_s, json({{u"seconds"_s, 3725}}));
    policy.notice(u"device.lost"_s, json(deviceArgs()));
    policy.notice(u"capture.lost"_s, json(captureArgs()));
    policy.error(u"plugin.crashed"_s, json({{u"block"_s, u"p"_s}, {u"plugin"_s, u"clap:x"_s}}));
    policy.error(u"recording.lowDiskSpace"_s,
                 json({{u"block"_s, u"r"_s}, {u"file"_s, u"/a.wav"_s}, {u"megabytes"_s, u"900"_s}}));
    policy.error(u"routing.feedbackLoop"_s, json({{u"path"_s, u"x"_s}}));
    wait(kGrace * 3);
    policy.notice(u"capture.regained"_s, json(captureArgs()));

    QSet<Event> events;
    static const QRegularExpression placeholder(u"%[0-9]"_s);
    for (const Notification &notification : std::as_const(said)) {
        events.insert(notification.event);
        EXPECT_FALSE(notification.title.isEmpty());
        EXPECT_FALSE(notification.text.isEmpty());
        EXPECT_FALSE(notification.text.contains(placeholder)) << notification.text.toStdString();
    }
    EXPECT_EQ(events.size(), static_cast<qsizetype>(Event::Count)) << "every event can be said";
    EXPECT_TRUE(said.at(0).text.contains("1:02:05"_L1)) << said.at(0).text.toStdString();
}

TEST(NotifyTexts, AMessageWithNoCatalogIsItsEnglish)
{
    EXPECT_EQ(translate(RB_I18NC("@info test", "Left as it is")), u"Left as it is"_s);
    EXPECT_EQ(openActionText(), u"Open"_s);
    EXPECT_EQ(showInFolderActionText(), u"Show in Folder"_s);
}

TEST(NotifyRc, EveryEventIsInSystemSettingsAndNothingElse)
{
    QFile file(QStringLiteral(RB_NOTIFYRC));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QSet<QString> sections;
    QString section;
    QSet<QString> keys;
    QHash<QString, QSet<QString>> keysOf;
    QString desktopEntry;
    for (const QString &line : QString::fromUtf8(file.readAll()).split(u'\n')) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(u'[')) {
            section = trimmed.mid(1, trimmed.size() - 2);
            sections.insert(section);
        } else if (trimmed.contains(u'=') && !trimmed.startsWith(u'#')) {
            const QString key = trimmed.section(u'=', 0, 0);
            keysOf[section].insert(key);
            if (section == "Global"_L1 && key == "DesktopEntry"_L1) {
                desktopEntry = trimmed.section(u'=', 1);
            }
        }
    }
    QSet<QString> wanted{u"Global"_s};
    for (std::size_t index = 0; index < static_cast<std::size_t>(Event::Count); ++index) {
        wanted.insert(u"Event/"_s + eventId(static_cast<Event>(index)));
    }
    EXPECT_EQ(sections, wanted);
    for (const QString &name : std::as_const(sections)) {
        if (name == "Global"_L1) {
            continue;
        }
        for (const char *key : {"Name", "Comment", "Action"}) {
            EXPECT_TRUE(keysOf.value(name).contains(QLatin1StringView(key))) << name.toStdString() << " " << key;
        }
    }
    // The desktop file System Settings finds the application by.
    EXPECT_EQ(desktopEntry, u"io.github.reverblabr.Reverblabr"_s);
    EXPECT_TRUE(QFile::exists(QStringLiteral(RB_APP_DIR) + u'/' + desktopEntry + u".desktop"_s));
}

TEST(NotifyRc, WhatThePersonChoseInSystemSettingsIsWhatTheDaemonDoes)
{
    needApplication();
    // The test's own config folder (tests/unit/CMakeLists.txt), never the
    // real one.
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    ASSERT_TRUE(config.contains("test-home"_L1)) << config.toStdString();
    ASSERT_TRUE(QDir().mkpath(config));
    QFile mine(config + u"/reverblabr.notifyrc"_s);
    ASSERT_TRUE(mine.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
    mine.write(
        "# What System Settings writes\n"
        "[Event/deviceLost]\nAction=\n\n"
        "[Event/captureLost]\nAction=Sound|Popup\n");
    mine.close();

    EXPECT_FALSE(popupWanted(Event::DeviceLost)) << "switched off";
    EXPECT_TRUE(popupWanted(Event::CaptureLost));
    EXPECT_TRUE(popupWanted(Event::PluginFailed)) << "left alone";
    ASSERT_TRUE(mine.remove());
    EXPECT_TRUE(popupWanted(Event::DeviceLost));
}
