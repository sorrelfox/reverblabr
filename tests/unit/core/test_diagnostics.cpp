// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/diagnostics.h"
#include "core/mockengine.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QSignalSpy>
#include <gtest/gtest.h>

using namespace rb::core;
using namespace Qt::StringLiterals;

TEST(DiagnosticsTest, EveryRecordingFormatIsListedAndAMissingEncoderSaysWhy)
{
    rb::record::EncoderProbe probe;
    probe.aac = false;
    probe.alac = true;
    const QJsonArray formats = recordingFormatsJson(probe);
    ASSERT_EQ(formats.size(), 8);
    QStringList available;
    QString aacReason;
    for (const QJsonValue &value : formats) {
        const QJsonObject format = value.toObject();
        if (format.value("available"_L1).toBool()) {
            available.append(format.value("id"_L1).toString());
        } else {
            EXPECT_EQ(format.value("id"_L1).toString(), u"aac"_s);
            aacReason = format.value("reason"_L1).toString();
        }
    }
    EXPECT_EQ(available.size(), 7) << available.join(u", "_s).toStdString();
    EXPECT_TRUE(available.contains(u"wav"_s));
    EXPECT_TRUE(available.contains(u"alac"_s));
    EXPECT_FALSE(aacReason.isEmpty()) << "an unavailable format says why";
}

TEST(DiagnosticsTest, ThePluginScanIsCountedByFormatWithWhatItLeftOut)
{
    QList<InstalledPlugin> plugins(3);
    plugins[0].format = u"lv2"_s;
    plugins[1].format = u"clap"_s;
    plugins[2].format = u"lv2"_s;
    const QList<BlockedCandidate> blocked{{u"ladspa"_s, u"/usr/lib64/ladspa/bad.so"_s, u"crashed"_s}};
    const QList<BlockedCandidate> incomplete{{u"clap"_s, u"/usr/lib64/clap/many.clap"_s, u"incomplete"_s}};
    const QJsonObject scan = pluginScanJson(plugins, blocked, true, incomplete);
    EXPECT_TRUE(scan.value("scanning"_L1).toBool());
    EXPECT_EQ(scan.value("total"_L1).toInt(), 3);
    EXPECT_EQ(scan.value("formats"_L1).toObject(), (QJsonObject{{"clap"_L1, 1}, {"lv2"_L1, 2}}));
    const QJsonArray left = scan.value("blocked"_L1).toArray();
    ASSERT_EQ(left.size(), 1);
    EXPECT_EQ(left.at(0).toObject().value("reason"_L1).toString(), u"crashed"_s);
    // A file the scan got only part of the way through.
    EXPECT_EQ(scan.value("incomplete"_L1).toArray(),
              (QJsonArray{QJsonObject{{"format"_L1, u"clap"_s}, {"path"_L1, u"/usr/lib64/clap/many.clap"_s}}}));
}

TEST(DiagnosticsTest, ALateMomentIsWrittenOnTheWallClock)
{
    const qint64 at = QDateTime(QDate(2026, 9, 28), QTime(10, 0, 0, 250)).toMSecsSinceEpoch();
    const QJsonArray events = xrunHistoryJson({XrunEvent{at, 7, 2}});
    ASSERT_EQ(events.size(), 1);
    const QJsonObject event = events.at(0).toObject();
    EXPECT_EQ(event.value("at"_L1).toString(), u"2026-09-28T10:00:00.250"_s);
    EXPECT_EQ(event.value("total"_L1).toInt(), 7);
    EXPECT_EQ(event.value("added"_L1).toInt(), 2);
}

TEST(DiagnosticsTest, TheMockAnswersOnceWithItsSessionsAndWhatATestLaidOverThem)
{
    MockEngine engine;
    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy answered(&engine, &EngineFacade::diagnostics);
    engine.createSession(u"Diagnosed"_s);
    ASSERT_EQ(opened.count(), 1);

    engine.getDiagnostics();
    ASSERT_EQ(answered.count(), 1);
    QJsonObject answer = QJsonDocument::fromJson(answered.back().at(0).toString().toUtf8()).object();
    EXPECT_EQ(answer.value("version"_L1).toInt(), kDiagnosticsVersion);
    EXPECT_EQ(answer.value("engine"_L1).toObject().value("pid"_L1).toInteger(), QCoreApplication::applicationPid());
    const QJsonArray sessions = answer.value("sessions"_L1).toArray();
    ASSERT_EQ(sessions.size(), 1);
    EXPECT_EQ(sessions.at(0).toObject().value("name"_L1).toString(), u"Diagnosed"_s);
    EXPECT_EQ(answer.value("recordingFormats"_L1).toArray().size(), 8);
    EXPECT_TRUE(answer.value("graph"_L1).toObject().contains("links"_L1));

    engine.setFakeDiagnostics(u"{\"sessions\": [], \"extra\": 1}"_s);
    engine.getDiagnostics();
    ASSERT_EQ(answered.count(), 2);
    answer = QJsonDocument::fromJson(answered.back().at(0).toString().toUtf8()).object();
    EXPECT_TRUE(answer.value("sessions"_L1).toArray().isEmpty()) << "a laid-over key replaces the mock's own";
    EXPECT_EQ(answer.value("extra"_L1).toInt(), 1);
    EXPECT_EQ(answer.value("recordingFormats"_L1).toArray().size(), 8) << "and the rest stay";
}
