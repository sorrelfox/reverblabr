// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "globalshortcutregistry.h"

#include "globalshortcuts.h"

#include <KGlobalAccel>
#include <QAction>
#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QSet>

namespace rb::ui
{

class GlobalShortcutRegistry::Private
{
public:
    GlobalShortcuts *shortcuts = nullptr;
    QHash<QString, QAction *> actions;
};

GlobalShortcutRegistry::GlobalShortcutRegistry(GlobalShortcuts *shortcuts, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->shortcuts = shortcuts;
    const auto sync = [this] {
        const QList<GlobalShortcuts::Action> actions = d->shortcuts->actions();
        // **An action that is no longer wanted is let go**, which KGlobalAccel
        // takes as inactive and keeps its key for: one whose session was
        // deleted has already been forgotten, key and all.
        QSet<QString> wantedIds;
        for (const GlobalShortcuts::Action &wanted : actions) {
            wantedIds.insert(wanted.id);
        }
        for (auto it = d->actions.begin(); it != d->actions.end();) {
            if (wantedIds.contains(it.key())) {
                ++it;
            } else {
                delete it.value();
                it = d->actions.erase(it);
            }
        }
        for (const GlobalShortcuts::Action &wanted : actions) {
            QAction *existing = d->actions.value(wanted.id);
            if (existing != nullptr && existing->text() == wanted.text) {
                continue;
            }
            // **A new name is a new registration**: KGlobalAccel reads an
            // action's name when it registers it, and only then. Deleting
            // the old one sets it inactive, which keeps its key.
            delete existing;
            auto *action = new QAction(wanted.text, this);
            action->setObjectName(wanted.id);
            connect(action, &QAction::triggered, this, [this, id = wanted.id] { d->shortcuts->trigger(id); });
            d->actions.insert(wanted.id, action);
            // **No default key**, and whatever key the person set loaded
            // back.
            KGlobalAccel::setGlobalShortcut(action, QList<QKeySequence>{});
        }
    };
    connect(shortcuts, &GlobalShortcuts::actionsChanged, this, sync);
    connect(shortcuts, &GlobalShortcuts::forgotten, this, [this](const QString &actionId) {
        QAction *action = d->actions.take(actionId);
        if (action == nullptr) {
            return;
        }
        KGlobalAccel::self()->removeAllShortcuts(action);
        delete action;
    });
    sync();
}

GlobalShortcutRegistry::~GlobalShortcutRegistry() = default;

QAction *GlobalShortcutRegistry::action(const QString &actionId) const
{
    return d->actions.value(actionId);
}

} // namespace rb::ui
