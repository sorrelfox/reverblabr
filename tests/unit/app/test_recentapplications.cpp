// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The applications the pickers remember: that what played once is
// still offerable when it has been closed, that it survives a restart, that
// nothing node-scoped is written down, and that a program nobody has run
// for a season stops being offered.

#include "core/mockengine.h"
#include "recentapplications.h"

#include <QStringList>
#include <QTemporaryDir>
#include <QVariantMap>
#include <gtest/gtest.h>

using namespace rb;
using namespace Qt::StringLiterals;

namespace
{

/// A state file of the test's own, and a clock it can move, so that running
/// the tests neither reaches into whoever is running them nor has to wait a
/// month to watch an entry go stale.
class Remembering
{
public:
    Remembering()
    {
        EXPECT_TRUE(m_directory.isValid());
        m_config = KSharedConfig::openConfig(m_directory.filePath(u"state"_s), KConfig::SimpleConfig);
        ui::RecentApplications::setConfig(m_config);
        setNow(QDateTime(QDate(2026, 9, 21), QTime(12, 0), QTimeZone::UTC));
    }

    ~Remembering()
    {
        ui::RecentApplications::setConfig({});
        ui::RecentApplications::setClock({});
    }

    Remembering(const Remembering &) = delete;
    Remembering &operator=(const Remembering &) = delete;

    void setNow(const QDateTime &when)
    {
        m_now = when;
        ui::RecentApplications::setClock([this] { return m_now; });
    }

private:
    QTemporaryDir m_directory;
    KSharedConfig::Ptr m_config;
    QDateTime m_now;
};

[[nodiscard]] QStringList keysOf(const QVariantList &entries)
{
    QStringList keys;
    for (const QVariant &entry : entries) {
        keys.append(entry.toMap().value(u"key"_s).toString());
    }
    return keys;
}

} // namespace

TEST(RecentApplicationsTest, WhatTheInventoryShowedIsWrittenDown)
{
    // **Nobody has to open a picker for this to happen.** The program that
    // was a nuisance is usually closed by the time somebody goes looking
    // for it, so the list fills while the window is doing something else.
    const Remembering file;
    core::MockEngine engine;
    ui::RecentApplications recent;
    recent.attach(&engine);
    engine.addFakeInventory();

    EXPECT_EQ(keysOf(recent.entries()), (QStringList{u"binary:firefox"_s, u"binary:musicplayer"_s}));
    EXPECT_TRUE(recent.knows(u"binary:firefox"_s));
    EXPECT_EQ(recent.entries().at(0).toMap().value(u"name"_s).toString(), u"Firefox"_s);
}

TEST(RecentApplicationsTest, AnApplicationThatHasBeenClosedIsStillThere)
{
    // The criterion, and the whole point: the store outlives the program,
    // because what it holds is a match rule and a name rather than anything
    // belonging to a process.
    const Remembering file;
    {
        core::MockEngine engine;
        ui::RecentApplications recent;
        recent.attach(&engine);
        engine.addFakeInventory();
    }

    const ui::RecentApplications reopened;
    EXPECT_TRUE(reopened.knows(u"binary:firefox"_s));
    EXPECT_EQ(keysOf(reopened.entries()), (QStringList{u"binary:firefox"_s, u"binary:musicplayer"_s}));
}

TEST(RecentApplicationsTest, NothingNodeScopedIsPersisted)
{
    // A `node:` rule names one stream on one boot. Writing it down would be
    // offering somebody a name that stopped meaning anything the moment the
    // machine was switched off.
    const Remembering file;
    ui::RecentApplications recent;
    recent.remember(u"node:alsa_playback.someone"_s, u"Someone"_s);
    recent.remember(u"binary:obs"_s, u"OBS"_s);

    EXPECT_FALSE(recent.knows(u"node:alsa_playback.someone"_s));
    EXPECT_EQ(keysOf(recent.entries()), QStringList{u"binary:obs"_s});

    const ui::RecentApplications reopened;
    EXPECT_EQ(keysOf(reopened.entries()), QStringList{u"binary:obs"_s});
}

TEST(RecentApplicationsTest, AProgramNobodyHasRunForASeasonStopsBeingOffered)
{
    // **A picker is a list somebody reads.** Something uninstalled months
    // ago is a name they no longer recognise, and every one of those makes
    // the ones they do recognise harder to find.
    Remembering file;
    {
        ui::RecentApplications recent;
        recent.remember(u"binary:obs"_s, u"OBS"_s);
        EXPECT_TRUE(recent.knows(u"binary:obs"_s));
    }

    file.setNow(QDateTime(QDate(2026, 9, 21), QTime(12, 0), QTimeZone::UTC)
                    .addDays(ui::RecentApplications::kRememberedDays + 1));
    const ui::RecentApplications later;
    EXPECT_FALSE(later.knows(u"binary:obs"_s));
    EXPECT_TRUE(later.entries().isEmpty());
}

TEST(RecentApplicationsTest, TheFreshestComeFirstAndTheListHasAnEnd)
{
    Remembering file;
    ui::RecentApplications recent;

    const QDateTime start(QDate(2026, 9, 21), QTime(12, 0), QTimeZone::UTC);
    for (int index = 0; index < ui::RecentApplications::kMaxEntries + 4; ++index) {
        file.setNow(start.addSecs(index));
        recent.remember(u"binary:program%1"_s.arg(index), u"Program %1"_s.arg(index));
    }

    const QVariantList entries = recent.entries();
    EXPECT_EQ(entries.size(), ui::RecentApplications::kMaxEntries);
    // The last one written is the first one offered, and the four oldest
    // have been let go.
    EXPECT_EQ(entries.at(0).toMap().value(u"key"_s).toString(),
              u"binary:program%1"_s.arg(ui::RecentApplications::kMaxEntries + 3));
    EXPECT_FALSE(recent.knows(u"binary:program0"_s));
}

TEST(RecentApplicationsTest, SeeingOneAgainMovesItToTheFront)
{
    Remembering file;
    ui::RecentApplications recent;

    const QDateTime start(QDate(2026, 9, 21), QTime(12, 0), QTimeZone::UTC);
    recent.remember(u"binary:obs"_s, u"OBS"_s);
    file.setNow(start.addSecs(60));
    recent.remember(u"binary:firefox"_s, u"Firefox"_s);
    EXPECT_EQ(keysOf(recent.entries()), (QStringList{u"binary:firefox"_s, u"binary:obs"_s}));

    file.setNow(start.addSecs(120));
    recent.remember(u"binary:obs"_s, u"OBS"_s);
    EXPECT_EQ(keysOf(recent.entries()), (QStringList{u"binary:obs"_s, u"binary:firefox"_s}));
}
