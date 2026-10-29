// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "mpris/player.h"

#include <QDBusConnection>
#include <QObject>

#include <memory>
#include <vector>

class QDBusMessage;

namespace rb::mpris
{

/// **Every media player on a bus, kept up to date as it changes.**
///
/// It asks the bus once which players are there, and from then on is told:
/// a player that comes or goes by the bus's NameOwnerChanged, a new track
/// or a pause by the player's own PropertiesChanged. **Nothing is polled**,
/// so a player that never says it changed (a few do not) is as it was
/// last read.
///
/// Everything is asked without waiting for the answer, so a player that
/// is slow or stuck holds up nothing but its own entry, which appears when
/// it answers. Lives on the thread it was made on, which needs an event
/// loop.
class PlayerWatcher : public QObject
{
    Q_OBJECT

public:
    /// Starts watching \a bus at once. A bus that is not connected has no
    /// players, and says so by never saying anything.
    explicit PlayerWatcher(const QDBusConnection &bus, QObject *parent = nullptr);
    ~PlayerWatcher() override;

    /// The players there are now, in the order of their bus names. A player
    /// is here from the moment the bus says it has the name, and its
    /// details fill in as it answers.
    [[nodiscard]] const std::vector<Player> &players() const;

    /// Whether the bus has said which players were there to begin with.
    [[nodiscard]] bool isReady() const;

Q_SIGNALS:
    /// A player came, went, or said something new.
    void changed();
    /// The first listing is in.
    void ready();

private Q_SLOTS:
    void onNameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);
    void onPropertiesChanged(const QDBusMessage &message);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::mpris
