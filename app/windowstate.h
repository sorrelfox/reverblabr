// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <KConfigGroup>
#include <KSharedConfig>
#include <QObject>
#include <QQmlEngine>
#include <QSize>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace rb::ui
{

/// How big the window was and where the splitter sat, remembered between
/// runs.
///
/// **In the state file, not the settings file.** KConfig keeps the two
/// apart on purpose: a window size is something the application noticed,
/// not something somebody chose, and it has no business in a file a person
/// might copy between machines or put under version control.
class WindowState : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// What the window should open at. Zero means nothing has been
    /// remembered yet and the window picks its own size.
    Q_PROPERTY(int windowWidth READ windowWidth CONSTANT)
    Q_PROPERTY(int windowHeight READ windowHeight CONSTANT)
    /// How wide the sidebar was. Zero means the sidebar picks its own.
    Q_PROPERTY(int sidebarWidth READ sidebarWidth CONSTANT)
    /// Whether the session is shown as a list rather than on the canvas:
    /// the View menu's and the Info tab's "Show as list", for
    /// somebody who cannot see the canvas or cannot aim at it. **Written as
    /// it is chosen**, and the same in every session: it is how this person
    /// works, not what a session is.
    Q_PROPERTY(bool listShown READ listShown WRITE setListShown NOTIFY listShownChanged)

public:
    explicit WindowState(QObject *parent = nullptr);

    [[nodiscard]] int windowWidth() const;
    [[nodiscard]] int windowHeight() const;
    [[nodiscard]] int sidebarWidth() const;

    /// Whether the classic menu bar shows: Ctrl+M's choice, kept for
    /// each kind of window, because the two start from opposite answers. A
    /// window KWin decorates has had its menu bar from the start and keeps
    /// it; the Reverblabr title bar holds the menus behind a button, as the
    /// prototype has no menu bar, until somebody asks for the bar.
    [[nodiscard]] Q_INVOKABLE bool menuBarShown(bool ownTitleBar) const;
    Q_INVOKABLE void setMenuBarShown(bool ownTitleBar, bool shown);

    [[nodiscard]] bool listShown() const;
    void setListShown(bool shown);

    /// Which of the block library's categories are folded away, by the
    /// engine's stable names. **Written as they are folded**, not when the
    /// window closes: folding one is a thing a person does now and again,
    /// not a hundred times while dragging an edge.
    [[nodiscard]] Q_INVOKABLE QStringList collapsedCategories() const;
    Q_INVOKABLE void setCollapsedCategories(const QStringList &categories);

    /// The settings panels pulled off their blocks in one session, and
    /// where each was left: a list of `{blockId, x, y}`, the position
    /// in the canvas's viewport. **The state file, not the session file**:
    /// where a panel was left on this screen is something the application
    /// noticed, like the window's size, and not part of what the session
    /// does. Written as it changes; empty forgets the session's entry.
    [[nodiscard]] Q_INVOKABLE QVariantList detachedPanels(const QString &sessionId) const;
    Q_INVOKABLE void setDetachedPanels(const QString &sessionId, const QVariantList &panels);

    /// How big one of the other windows was, by the name it keeps its size
    /// under (the Virtual Devices window, for one). An empty size means
    /// nothing has been remembered yet and the window picks its own.
    [[nodiscard]] Q_INVOKABLE QSize windowSize(const QString &window) const;
    /// Writes that window's size down, when it closes, as save() does the
    /// main window's; a window not on the screen is not remembered.
    Q_INVOKABLE void saveWindowSize(const QString &window, int width, int height);

    /// How a table was laid out: its columns in the order they sit,
    /// each `{key, shown, width}`, and what it was sorted by, as
    /// `{columns, sortColumn, descending}`. **The state file, not the
    /// settings file**, like a window's size: which columns somebody dragged
    /// where is something the application noticed. Empty when nothing has
    /// been remembered, or what was written makes no sense.
    [[nodiscard]] Q_INVOKABLE QVariantMap tableLayout(const QString &table) const;
    /// Writes a table's layout down, as it changes.
    Q_INVOKABLE void setTableLayout(const QString &table, const QVariantMap &layout);

    /// Writes it down. **Called when the window closes**, not on every
    /// resize: a drag of a window edge is a hundred resizes, and a hundred
    /// writes to a file in a person's home directory.
    Q_INVOKABLE void save(int windowWidth, int windowHeight, int sidebarWidth);

    /// Where to read and write. A test gives its own, so that running the
    /// tests does not reach into whoever is running them.
    static void setConfig(const KSharedConfig::Ptr &config);

Q_SIGNALS:
    void listShownChanged();

private:
    [[nodiscard]] KConfigGroup group() const;
};

} // namespace rb::ui
