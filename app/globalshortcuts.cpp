// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "globalshortcuts.h"

#include "appshell.h"
#include "sessionoverview.h"

#include <KLocalizedString>
#include <QSet>
#include <QVariantMap>

using namespace Qt::StringLiterals;

namespace rb::ui
{

class GlobalShortcuts::Private
{
public:
    core::EngineFacade *facade = nullptr;
    /// **The saved sessions, as the Sessions window lists them**: their
    /// names, whether they run, and the Run that opens one only on disk.
    SessionOverview overview;
    QList<Action> sessionActions;
    bool ready = false;

    [[nodiscard]] static QString textFor(const QString &sessionName)
    {
        return i18nc("@action a global shortcut, %1 is a session's name", "Run or stop “%1”", sessionName);
    }
};

GlobalShortcuts::GlobalShortcuts(core::EngineFacade *facade, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    const auto relisted = [this] {
        if (!d->overview.isReady()) {
            return;
        }
        QList<Action> actions;
        QSet<QString> present;
        for (int row = 0; row < d->overview.count(); ++row) {
            const QVariantMap entry = d->overview.rowAt(row);
            const QString id = entry.value(u"id"_s).toString();
            actions.append({QString(kSessionPrefix) + id, Private::textFor(entry.value(u"name"_s).toString())});
            present.insert(actions.constLast().id);
        }
        if (d->ready && actions == d->sessionActions) {
            return;
        }
        // **Gone from the list is gone from disk**: the list is replaced
        // whole each time the engine answers, never shown half-read. **Except
        // when nothing is left**: a list that comes back empty is as likely a
        // sessions folder that could not be read as a person who deleted
        // every session, and forgetting would lose every key they set. Those
        // actions are only let go, which keeps their keys.
        QStringList gone;
        if (!actions.isEmpty()) {
            for (const Action &action : std::as_const(d->sessionActions)) {
                if (!present.contains(action.id)) {
                    gone.append(action.id);
                }
            }
        }
        d->sessionActions = actions;
        d->ready = true;
        // Forgotten first, while the registry still has the action to take
        // the key off.
        for (const QString &id : std::as_const(gone)) {
            Q_EMIT forgotten(id);
        }
        Q_EMIT actionsChanged();
    };
    connect(&d->overview, &QAbstractItemModel::modelReset, this, relisted);
    connect(&d->overview, &QAbstractItemModel::rowsInserted, this, relisted);
    connect(&d->overview, &QAbstractItemModel::rowsRemoved, this, relisted);
    connect(&d->overview, &QAbstractItemModel::dataChanged, this, relisted);
    connect(&d->overview, &SessionOverview::countChanged, this, relisted);
    attach(facade);
}

GlobalShortcuts::~GlobalShortcuts() = default;

GlobalShortcuts *GlobalShortcuts::instance()
{
    static GlobalShortcuts *shortcuts = new GlobalShortcuts(AppShell::engine());
    return shortcuts;
}

GlobalShortcuts *GlobalShortcuts::create(QQmlEngine *, QJSEngine *)
{
    GlobalShortcuts *shortcuts = instance();
    // **One for the window and the registry**, which is why QML must not
    // own it.
    QJSEngine::setObjectOwnership(shortcuts, QJSEngine::CppOwnership);
    // A test that makes a window of its own brings an engine of its own.
    if (shortcuts->d->facade != AppShell::engine()) {
        shortcuts->attach(AppShell::engine());
    }
    return shortcuts;
}

void GlobalShortcuts::attach(core::EngineFacade *facade)
{
    d->facade = facade;
    d->overview.attach(facade);
}

QList<GlobalShortcuts::Action> GlobalShortcuts::fixedActions()
{
    return {
        {QString(kShowWindow), i18nc("@action a global shortcut", "Show Reverblabr")},
        {QString(kInputSwitchA), i18nc("@action a global shortcut", "Input Switch: play A")},
        {QString(kInputSwitchB), i18nc("@action a global shortcut", "Input Switch: play B")},
        {QString(kTimeShiftPause), i18nc("@action a global shortcut", "Time Shift: pause or play")},
        {QString(kTimeShiftBack),
         i18ncp(
             "@action a global shortcut", "Time Shift: back %1 second", "Time Shift: back %1 seconds", kJumpSeconds)},
        {QString(kTimeShiftForward),
         i18ncp("@action a global shortcut",
                "Time Shift: forward %1 second",
                "Time Shift: forward %1 seconds",
                kJumpSeconds)},
        {QString(kTimeShiftLive), i18nc("@action a global shortcut", "Time Shift: back to live")},
        {QString(kRecorderSplit), i18nc("@action a global shortcut", "Recorder: start a new file")},
    };
}

QString GlobalShortcuts::sessionOf(const QString &actionId)
{
    return actionId.startsWith(kSessionPrefix) ? actionId.mid(kSessionPrefix.size()) : QString{};
}

QList<GlobalShortcuts::Action> GlobalShortcuts::actions() const
{
    return fixedActions() + d->sessionActions;
}

QVariantList GlobalShortcuts::actionList() const
{
    QVariantList list;
    for (const Action &action : actions()) {
        list.append(QVariantMap{{u"id"_s, action.id}, {u"text"_s, action.text}});
    }
    return list;
}

bool GlobalShortcuts::isReady() const
{
    return d->ready;
}

void GlobalShortcuts::trigger(const QString &actionId)
{
    if (const QString sessionId = sessionOf(actionId); !sessionId.isEmpty()) {
        const QVariantMap entry = d->overview.rowAt(d->overview.rowOf(sessionId));
        if (entry.isEmpty()) {
            return;
        }
        // **Starting counts as running**: a second press while it starts
        // stops it, which is what the person pressing it twice means.
        const QString state = entry.value(u"state"_s).toString();
        if (state == core::sessionState::running || state == core::sessionState::starting) {
            d->overview.stop({sessionId});
        } else {
            d->overview.run({sessionId});
        }
        return;
    }
    if (actionId == kShowWindow) {
        Q_EMIT showRequested();
        return;
    }
    for (const Action &action : fixedActions()) {
        if (action.id == actionId) {
            Q_EMIT blockActionRequested(actionId);
            return;
        }
    }
}

} // namespace rb::ui
