// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>

#include <memory>

namespace rb::mpris
{
class PlayerWatcher;
}

namespace rb::ui
{

/// **The media players running now**, for the Broadcast panel to offer
/// one to follow: each once, by the key a block stores and the name it
/// gives itself.
///
/// Read from the session bus by the window itself, which is on the same
/// bus as the engine and sees the same players; what the engine follows
/// for a stream it reads on its own. **Watched from the first time the
/// panel asks**, which it does only while a block's title is to come from
/// a player.
class MediaPlayers : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// `[{id, name, playing}]`, by name: the player key, the name the
    /// player gives itself (its key when it gives none), and whether it is
    /// playing now.
    Q_PROPERTY(QVariantList players READ players NOTIFY changed)

public:
    ~MediaPlayers() override;

    static MediaPlayers *instance();
    static MediaPlayers *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    [[nodiscard]] QVariantList players() const;

    /// The name of the player called \a id, or empty when it is not
    /// running.
    [[nodiscard]] Q_INVOKABLE QString nameOf(const QString &id) const;

    /// **The players a test says there are**, in the form players() has,
    /// and never the bus's from then on.
    void setPlayersForTesting(const QVariantList &players);

Q_SIGNALS:
    void changed();

private:
    MediaPlayers();
    void watch();
    void refresh();

    std::unique_ptr<mpris::PlayerWatcher> m_watcher;
    QVariantList m_players;
    bool m_testing = false;
};

} // namespace rb::ui
