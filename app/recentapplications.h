// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/enginefacade.h"

#include <KSharedConfig>
#include <QDateTime>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>

#include <functional>
#include <memory>

namespace rb::ui
{

/// The applications this machine has been heard playing, so that one which
/// has since been closed can still be picked by name.
///
/// **Why anything has to be remembered at all.** The pickers offer what is
/// playing, which is the easy half; the other half is a program somebody
/// wants to name *because* it was a nuisance, and by the time they open the
/// panel to say so they have usually closed it. Typing `binary:discord`
/// from memory works, and knowing that is exactly what the picker exists to
/// spare them.
///
/// **A match rule and a name, never a node.** A node id means nothing
/// between one boot and the next, so what is written down is the rule the
/// engine matches by and what the program called itself — the two things
/// that are still true tomorrow.
///
/// **In the state file rather than the settings file.** Nobody chose this
/// list; the application noticed it. KConfig keeps the two apart on purpose,
/// and a file somebody might copy between machines has no business carrying
/// what was playing on one of them.
class RecentApplications : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// What has been seen, freshest first, each as `{key, name, lastSeen}`.
    /// `key` is the match rule, which is what a picker hands back.
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)

public:
    /// How many are kept. A picker is a list somebody reads, and a hundred
    /// names nobody recognises is not a better list than a dozen they do.
    static constexpr int kMaxEntries = 32;

    /// How long one is remembered for. **Long enough to cover "that thing
    /// last week"** and short enough that a program uninstalled a season
    /// ago stops being offered.
    static constexpr int kRememberedDays = 30;

    /// How stale the written-down time may get before it is written again.
    /// **Every inventory would otherwise be a write** to a file in
    /// somebody's home directory, and the inventory changes every time any
    /// program on the machine opens or closes a sound.
    static constexpr int kRewriteAfterMinutes = 60;

    explicit RecentApplications(QObject *parent = nullptr);
    ~RecentApplications() override;

    static RecentApplications *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    /// Follows \a facade, asking it for the inventory at once, and writes
    /// down every application it names. A test gives its own; the QML
    /// singleton is given the window's.
    ///
    /// **It follows the engine rather than the Inventory singleton**, which
    /// is the same signal read for a different purpose: a list that filled
    /// only while a picker was open would miss the program that played and
    /// closed while nobody was looking — which is the one case it is for.
    void attach(core::EngineFacade *facade);

    [[nodiscard]] QVariantList entries() const;

    /// Writes one down by hand. **Invokable for the tests**, which have no
    /// way to make a program play.
    Q_INVOKABLE void remember(const QString &key, const QString &name);

    /// Whether the store has heard of a rule. Empty is never remembered,
    /// and neither is anything node-scoped.
    [[nodiscard]] Q_INVOKABLE bool knows(const QString &key) const;

    /// What the application called itself when it was last seen, or empty
    /// for one this machine has never heard play.
    ///
    /// **This is what "found by name" means** for a program that has been
    /// closed: the inventory has nothing to say about it, and the readable
    /// half of a match rule is a binary name rather than the name on its
    /// window.
    [[nodiscard]] Q_INVOKABLE QString nameFor(const QString &key) const;

    /// Where to read and write. A test gives its own, so that running the
    /// tests does not reach into whoever is running them.
    static void setConfig(const KSharedConfig::Ptr &config);

    /// What the store calls now. **A test plants an entry that is older
    /// than the machine has been on**, which is the only way to watch one
    /// go stale without waiting a month for it.
    static void setClock(std::function<QDateTime()> clock);

Q_SIGNALS:
    void changed();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::ui
