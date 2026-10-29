// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "windowstate.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace Qt::StringLiterals;

namespace rb::ui
{

namespace
{

KSharedConfig::Ptr g_config;

constexpr QLatin1StringView kGroup{"MainWindow"};
constexpr QLatin1StringView kWidth{"Width"};
constexpr QLatin1StringView kHeight{"Height"};
constexpr QLatin1StringView kSidebarWidth{"SidebarWidth"};
constexpr QLatin1StringView kCollapsedCategories{"CollapsedBlockCategories"};
constexpr QLatin1StringView kMenuBar{"MenuBar"};
constexpr QLatin1StringView kMenuBarInTitleBar{"MenuBarWithOwnTitleBar"};
constexpr QLatin1StringView kListShown{"SessionAsList"};
constexpr QLatin1StringView kDetachedPanelsGroup{"DetachedPanels"};
/// The other windows, a group each under this one, named by the window.
constexpr QLatin1StringView kWindowsGroup{"Windows"};
/// Tables' column layouts, a JSON entry each, named by the table.
constexpr QLatin1StringView kTablesGroup{"Tables"};

} // namespace

WindowState::WindowState(QObject *parent)
    : QObject(parent)
{
}

void WindowState::setConfig(const KSharedConfig::Ptr &config)
{
    g_config = config;
}

KConfigGroup WindowState::group() const
{
    const KSharedConfig::Ptr config = g_config ? g_config : KSharedConfig::openStateConfig();
    return KConfigGroup(config, kGroup);
}

int WindowState::windowWidth() const
{
    return group().readEntry(kWidth, 0);
}

int WindowState::windowHeight() const
{
    return group().readEntry(kHeight, 0);
}

int WindowState::sidebarWidth() const
{
    return group().readEntry(kSidebarWidth, 0);
}

bool WindowState::menuBarShown(bool ownTitleBar) const
{
    return group().readEntry(ownTitleBar ? kMenuBarInTitleBar : kMenuBar, !ownTitleBar);
}

void WindowState::setMenuBarShown(bool ownTitleBar, bool shown)
{
    KConfigGroup state = group();
    state.writeEntry(ownTitleBar ? kMenuBarInTitleBar : kMenuBar, shown);
    state.sync();
}

bool WindowState::listShown() const
{
    return group().readEntry(kListShown, false);
}

void WindowState::setListShown(bool shown)
{
    if (shown == listShown()) {
        return;
    }
    KConfigGroup state = group();
    state.writeEntry(kListShown, shown);
    state.sync();
    Q_EMIT listShownChanged();
}

QStringList WindowState::collapsedCategories() const
{
    return group().readEntry(kCollapsedCategories, QStringList{});
}

void WindowState::setCollapsedCategories(const QStringList &categories)
{
    KConfigGroup state = group();
    state.writeEntry(kCollapsedCategories, categories);
    state.sync();
}

QVariantList WindowState::detachedPanels(const QString &sessionId) const
{
    const KSharedConfig::Ptr config = g_config ? g_config : KSharedConfig::openStateConfig();
    const QString written = KConfigGroup(config, kDetachedPanelsGroup).readEntry(sessionId, QString{});
    QVariantList panels;
    // **Only what makes sense is given back.** The file is a person's to
    // edit, and a panel with no block or no place is one the canvas cannot
    // put anywhere.
    for (const QJsonValue &entry : QJsonDocument::fromJson(written.toUtf8()).array()) {
        const QJsonObject panel = entry.toObject();
        const QString blockId = panel.value("blockId"_L1).toString();
        if (blockId.isEmpty() || !panel.value("x"_L1).isDouble() || !panel.value("y"_L1).isDouble()) {
            continue;
        }
        panels.append(QVariantMap{{u"blockId"_s, blockId},
                                  {u"x"_s, panel.value("x"_L1).toDouble()},
                                  {u"y"_s, panel.value("y"_L1).toDouble()}});
    }
    return panels;
}

void WindowState::setDetachedPanels(const QString &sessionId, const QVariantList &panels)
{
    if (sessionId.isEmpty()) {
        return;
    }
    const KSharedConfig::Ptr config = g_config ? g_config : KSharedConfig::openStateConfig();
    KConfigGroup state(config, kDetachedPanelsGroup);
    if (panels.isEmpty()) {
        state.deleteEntry(sessionId);
    } else {
        QJsonArray written;
        for (const QVariant &entry : panels) {
            const QVariantMap panel = entry.toMap();
            written.append(QJsonObject{{"blockId"_L1, panel.value(u"blockId"_s).toString()},
                                       {"x"_L1, panel.value(u"x"_s).toDouble()},
                                       {"y"_L1, panel.value(u"y"_s).toDouble()}});
        }
        state.writeEntry(sessionId, QString::fromUtf8(QJsonDocument(written).toJson(QJsonDocument::Compact)));
    }
    state.sync();
}

QSize WindowState::windowSize(const QString &window) const
{
    const KSharedConfig::Ptr config = g_config ? g_config : KSharedConfig::openStateConfig();
    const KConfigGroup state = KConfigGroup(config, kWindowsGroup).group(window);
    const int width = state.readEntry(kWidth, 0);
    const int height = state.readEntry(kHeight, 0);
    // **Both or neither**: half a size is one the file was edited into.
    if (width <= 0 || height <= 0) {
        return {};
    }
    return {width, height};
}

void WindowState::saveWindowSize(const QString &window, int width, int height)
{
    if (window.isEmpty() || width <= 0 || height <= 0) {
        return;
    }
    const KSharedConfig::Ptr config = g_config ? g_config : KSharedConfig::openStateConfig();
    KConfigGroup state = KConfigGroup(config, kWindowsGroup).group(window);
    state.writeEntry(kWidth, width);
    state.writeEntry(kHeight, height);
    state.sync();
}

QVariantMap WindowState::tableLayout(const QString &table) const
{
    const KSharedConfig::Ptr config = g_config ? g_config : KSharedConfig::openStateConfig();
    const QString written = KConfigGroup(config, kTablesGroup).readEntry(table, QString{});
    const QJsonObject layout = QJsonDocument::fromJson(written.toUtf8()).object();
    // **Only columns with a name are given back.** The file is a person's to
    // edit, and a column with no key is one the table cannot place.
    QVariantList columns;
    for (const QJsonValue &entry : layout.value("columns"_L1).toArray()) {
        const QJsonObject column = entry.toObject();
        const QString key = column.value("key"_L1).toString();
        if (key.isEmpty()) {
            continue;
        }
        columns.append(QVariantMap{{u"key"_s, key},
                                   {u"shown"_s, column.value("shown"_L1).toBool(true)},
                                   {u"width"_s, column.value("width"_L1).toInt(0)}});
    }
    if (columns.isEmpty()) {
        return {};
    }
    return QVariantMap{{u"columns"_s, columns},
                       {u"sortColumn"_s, layout.value("sortColumn"_L1).toString()},
                       {u"descending"_s, layout.value("descending"_L1).toBool(false)}};
}

void WindowState::setTableLayout(const QString &table, const QVariantMap &layout)
{
    if (table.isEmpty()) {
        return;
    }
    QJsonArray columns;
    for (const QVariant &entry : layout.value(u"columns"_s).toList()) {
        const QVariantMap column = entry.toMap();
        columns.append(QJsonObject{{"key"_L1, column.value(u"key"_s).toString()},
                                   {"shown"_L1, column.value(u"shown"_s).toBool()},
                                   {"width"_L1, column.value(u"width"_s).toInt()}});
    }
    const QJsonObject written{{"columns"_L1, columns},
                              {"sortColumn"_L1, layout.value(u"sortColumn"_s).toString()},
                              {"descending"_L1, layout.value(u"descending"_s).toBool()}};
    const KSharedConfig::Ptr config = g_config ? g_config : KSharedConfig::openStateConfig();
    KConfigGroup state(config, kTablesGroup);
    state.writeEntry(table, QString::fromUtf8(QJsonDocument(written).toJson(QJsonDocument::Compact)));
    state.sync();
}

void WindowState::save(int windowWidth, int windowHeight, int sidebarWidth)
{
    KConfigGroup state = group();
    // **Nothing about a window that is not on the screen.** A minimised or
    // zero-sized window would otherwise be what it opens at next time.
    if (windowWidth > 0 && windowHeight > 0) {
        state.writeEntry(kWidth, windowWidth);
        state.writeEntry(kHeight, windowHeight);
    }
    if (sidebarWidth > 0) {
        state.writeEntry(kSidebarWidth, sidebarWidth);
    }
    state.sync();
}

} // namespace rb::ui
