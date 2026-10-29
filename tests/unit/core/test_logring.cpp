// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/logring.h"

#include <QLoggingCategory>
#include <QStringList>
#include <gtest/gtest.h>

using namespace rb::core;
using namespace Qt::StringLiterals;

namespace
{

Q_LOGGING_CATEGORY(lcRingTest, "reverblabr.test.ring")

/// What reached the handler that was there before the ring.
QStringList &passedOn()
{
    static QStringList lines;
    return lines;
}

void before(QtMsgType, const QMessageLogContext &, const QString &message)
{
    passedOn().append(message);
}

/// Puts the process's handler back however a test ends.
struct Restore {
    QtMessageHandler original = qInstallMessageHandler(&before);
    ~Restore()
    {
        LogRing::uninstallForTesting();
        qInstallMessageHandler(original);
        passedOn().clear();
    }
};

} // namespace

TEST(LogRingTest, KeepsTheLastLinesAndStillPassesEveryMessageOn)
{
    const Restore restore;
    LogRing::install(3);
    EXPECT_TRUE(LogRing::isInstalled());
    for (int line = 1; line <= 5; ++line) {
        qCInfo(lcRingTest).noquote() << u"line %1"_s.arg(line);
    }

    const QStringList kept = LogRing::lines();
    ASSERT_EQ(kept.size(), 3) << kept.join(u'\n').toStdString();
    EXPECT_TRUE(kept.at(0).endsWith(u"info reverblabr.test.ring: line 3"_s)) << kept.at(0).toStdString();
    EXPECT_TRUE(kept.at(2).endsWith(u"info reverblabr.test.ring: line 5"_s)) << kept.at(2).toStdString();
    // **A copy, not a detour**: the journal still gets every line.
    EXPECT_EQ(passedOn(), (QStringList{u"line 1"_s, u"line 2"_s, u"line 3"_s, u"line 4"_s, u"line 5"_s}));
}

TEST(LogRingTest, AWarningSaysSoAndWhen)
{
    const Restore restore;
    LogRing::install();
    qCWarning(lcRingTest).noquote() << u"the device went away"_s;
    const QStringList kept = LogRing::lines();
    ASSERT_EQ(kept.size(), 1);
    // "2026-09-28T23:10:00.123 warning reverblabr.test.ring: …"
    const QString line = kept.front();
    EXPECT_EQ(line.indexOf(u' '), 23) << line.toStdString();
    EXPECT_TRUE(line.mid(24).startsWith(u"warning reverblabr.test.ring: the device went away"_s)) << line.toStdString();
}

TEST(LogRingTest, NothingIsKeptUntilItIsInstalled)
{
    const Restore restore;
    qCInfo(lcRingTest) << "before";
    EXPECT_FALSE(LogRing::isInstalled());
    EXPECT_TRUE(LogRing::lines().isEmpty());
}
