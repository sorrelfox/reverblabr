// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// How big the window was and where the splitter sat, remembered
// between runs.

#include "windowstate.h"

#include <QTemporaryDir>
#include <gtest/gtest.h>

using namespace rb;
using namespace Qt::StringLiterals;

namespace
{

/// A state file of the test's own, so that running the tests does not reach
/// into whoever is running them.
class StateFile
{
public:
    StateFile()
    {
        EXPECT_TRUE(m_directory.isValid());
        m_config = KSharedConfig::openConfig(m_directory.filePath(u"state"_s), KConfig::SimpleConfig);
        ui::WindowState::setConfig(m_config);
    }

    ~StateFile()
    {
        ui::WindowState::setConfig({});
    }

    StateFile(const StateFile &) = delete;
    StateFile &operator=(const StateFile &) = delete;

private:
    QTemporaryDir m_directory;
    KSharedConfig::Ptr m_config;
};

} // namespace

TEST(WindowStateTest, NothingRememberedIsNothingSaid)
{
    // A first run has no size to restore, and zero is how it says so: the
    // window then picks its own, rather than opening at nothing.
    const StateFile file;
    const ui::WindowState state;
    EXPECT_EQ(state.windowWidth(), 0);
    EXPECT_EQ(state.windowHeight(), 0);
    EXPECT_EQ(state.sidebarWidth(), 0);
}

TEST(WindowStateTest, WhatWasSavedIsWhatOpensAgain)
{
    const StateFile file;
    ui::WindowState state;
    state.save(1280, 800, 260);

    const ui::WindowState reopened;
    EXPECT_EQ(reopened.windowWidth(), 1280);
    EXPECT_EQ(reopened.windowHeight(), 800);
    EXPECT_EQ(reopened.sidebarWidth(), 260);
}

TEST(WindowStateTest, AWindowThatIsNotOnTheScreenIsNotRemembered)
{
    // **Nothing about a window nobody can see.** A minimised window reports
    // no size, and saving that would be the size it opened at next time.
    const StateFile file;
    ui::WindowState state;
    state.save(1280, 800, 260);
    state.save(0, 0, 0);

    const ui::WindowState reopened;
    EXPECT_EQ(reopened.windowWidth(), 1280);
    EXPECT_EQ(reopened.windowHeight(), 800);
    EXPECT_EQ(reopened.sidebarWidth(), 260);
}

TEST(WindowStateTest, DetachedPanelsAreRememberedPerSession)
{
    // Where each detached settings panel was left, for the session it
    // belongs to and no other.
    const StateFile file;
    ui::WindowState state;
    state.setDetachedPanels(u"one"_s,
                            {QVariantMap{{u"blockId"_s, u"b1"_s}, {u"x"_s, 120.5}, {u"y"_s, 40.0}},
                             QVariantMap{{u"blockId"_s, u"b3"_s}, {u"x"_s, 600.0}, {u"y"_s, 300.0}}});

    const ui::WindowState reopened;
    const QVariantList panels = reopened.detachedPanels(u"one"_s);
    ASSERT_EQ(panels.size(), 2);
    EXPECT_EQ(panels.at(0).toMap().value(u"blockId"_s).toString(), u"b1"_s);
    EXPECT_DOUBLE_EQ(panels.at(0).toMap().value(u"x"_s).toDouble(), 120.5);
    EXPECT_DOUBLE_EQ(panels.at(1).toMap().value(u"y"_s).toDouble(), 300.0);
    EXPECT_TRUE(reopened.detachedPanels(u"two"_s).isEmpty()) << "another session got this one's panels";

    // None left is nothing remembered.
    state.setDetachedPanels(u"one"_s, {});
    EXPECT_TRUE(ui::WindowState{}.detachedPanels(u"one"_s).isEmpty());
}

TEST(WindowStateTest, TheMenuBarIsUpUnderKWinsTitleBarAndBehindAButtonUnderOurs)
{
    // Ctrl+M's choice, kept for each kind of window. KDE's windows have
    // had their menu bar from the start; the design's title bar has none.
    const StateFile file;
    ui::WindowState state;
    EXPECT_TRUE(state.menuBarShown(false)) << "a window KWin frames lost its menu bar";
    EXPECT_FALSE(state.menuBarShown(true)) << "the design's title bar grew a menu bar nobody asked for";

    state.setMenuBarShown(true, true);
    state.setMenuBarShown(false, false);
    const ui::WindowState reopened;
    EXPECT_TRUE(reopened.menuBarShown(true));
    EXPECT_FALSE(reopened.menuBarShown(false));
}

TEST(WindowStateTest, TheSessionAsAListIsRemembered)
{
    // "Show as list" stays chosen from one run to the next, and says
    // so as it changes, so the menu, the Info tab and the canvas follow.
    const StateFile file;
    ui::WindowState state;
    EXPECT_FALSE(state.listShown()) << "the canvas is what a first run shows";

    int told = 0;
    QObject::connect(&state, &ui::WindowState::listShownChanged, [&told] { ++told; });
    state.setListShown(true);
    EXPECT_EQ(told, 1) << "nothing following the choice was told";
    state.setListShown(true);
    EXPECT_EQ(told, 1) << "choosing what was already chosen is not a change";
    EXPECT_TRUE(ui::WindowState{}.listShown()) << "the list was forgotten";
    state.setListShown(false);
    EXPECT_FALSE(ui::WindowState{}.listShown());
}

TEST(WindowStateTest, AnotherWindowsSizeIsRememberedUnderItsOwnName)
{
    // The Virtual Devices window opens at the size it was closed at,
    // and neither the main window's size nor another window's is disturbed.
    const StateFile file;
    ui::WindowState state;
    EXPECT_TRUE(state.windowSize(u"VirtualDevices"_s).isEmpty()) << "a size nobody saved";

    state.save(1280, 800, 260);
    state.saveWindowSize(u"VirtualDevices"_s, 640, 480);

    const ui::WindowState reopened;
    EXPECT_EQ(reopened.windowSize(u"VirtualDevices"_s), QSize(640, 480));
    EXPECT_TRUE(reopened.windowSize(u"Other"_s).isEmpty()) << "another window got this one's size";
    EXPECT_EQ(reopened.windowWidth(), 1280) << "the main window's size moved";
    EXPECT_EQ(reopened.windowHeight(), 800);

    // **Nothing about a window that is not on the screen.**
    state.saveWindowSize(u"VirtualDevices"_s, 0, 0);
    EXPECT_EQ(ui::WindowState{}.windowSize(u"VirtualDevices"_s), QSize(640, 480));
}

TEST(WindowStateTest, ATablesColumnsAreRememberedAndNonsenseIsLeftOut)
{
    // Which columns the Sessions window shows, in what order, how wide
    // and what it is sorted by. The file is a person's to edit, so a column
    // with no key is dropped rather than handed to a table that cannot place
    // it, and a layout with no columns at all is no layout.
    const StateFile file;
    ui::WindowState state;
    EXPECT_TRUE(state.tableLayout(u"Sessions"_s).isEmpty()) << "a layout nobody saved";

    state.setTableLayout(
        u"Sessions"_s,
        QVariantMap{{u"columns"_s,
                     QVariantList{QVariantMap{{u"key"_s, u"name"_s}, {u"shown"_s, true}, {u"width"_s, 200}},
                                  QVariantMap{{u"key"_s, QString{}}, {u"shown"_s, true}},
                                  QVariantMap{{u"key"_s, u"level"_s}, {u"shown"_s, false}}}},
                    {u"sortColumn"_s, u"level"_s},
                    {u"descending"_s, true}});
    const QVariantMap layout = ui::WindowState{}.tableLayout(u"Sessions"_s);
    const QVariantList columns = layout.value(u"columns"_s).toList();
    ASSERT_EQ(columns.size(), 2) << "the column with no key was handed back";
    EXPECT_EQ(columns.at(0).toMap().value(u"key"_s).toString(), u"name"_s);
    EXPECT_EQ(columns.at(0).toMap().value(u"width"_s).toInt(), 200);
    EXPECT_FALSE(columns.at(1).toMap().value(u"shown"_s).toBool());
    EXPECT_EQ(layout.value(u"sortColumn"_s).toString(), u"level"_s);
    EXPECT_TRUE(layout.value(u"descending"_s).toBool());
    EXPECT_TRUE(ui::WindowState{}.tableLayout(u"Other"_s).isEmpty()) << "another table got this one's layout";

    state.setTableLayout(u"Sessions"_s, QVariantMap{{u"columns"_s, QVariantList{}}});
    EXPECT_TRUE(ui::WindowState{}.tableLayout(u"Sessions"_s).isEmpty());
}
