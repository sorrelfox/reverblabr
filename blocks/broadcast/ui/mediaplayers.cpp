// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "mediaplayers.h"

#include "mpris/playerwatcher.h"

#include <QCollator>
#include <QDBusConnection>
#include <QVariantMap>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace rb::ui
{

MediaPlayers::MediaPlayers() = default;

MediaPlayers::~MediaPlayers() = default;

MediaPlayers *MediaPlayers::instance()
{
    // **Never deleted**, as Appearance is not: QML holds it for as long as
    // any engine lives, and a test makes more than one.
    static MediaPlayers *const players = new MediaPlayers;
    return players;
}

MediaPlayers *MediaPlayers::create(QQmlEngine *engine, QJSEngine *scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)
    MediaPlayers *const players = instance();
    QJSEngine::setObjectOwnership(players, QJSEngine::CppOwnership);
    players->watch();
    return players;
}

QVariantList MediaPlayers::players() const
{
    return m_players;
}

QString MediaPlayers::nameOf(const QString &id) const
{
    for (const QVariant &player : m_players) {
        const QVariantMap entry = player.toMap();
        if (entry.value(u"id"_s).toString() == id) {
            return entry.value(u"name"_s).toString();
        }
    }
    return {};
}

void MediaPlayers::setPlayersForTesting(const QVariantList &players)
{
    m_testing = true;
    m_watcher.reset();
    m_players = players;
    Q_EMIT changed();
}

void MediaPlayers::watch()
{
    if (m_testing || m_watcher) {
        return;
    }
    m_watcher = std::make_unique<mpris::PlayerWatcher>(QDBusConnection::sessionBus());
    connect(m_watcher.get(), &mpris::PlayerWatcher::changed, this, &MediaPlayers::refresh);
}

void MediaPlayers::refresh()
{
    // **Each player once**: two copies of one player are one choice, as a
    // block names a player by its key.
    QVariantList players;
    for (const mpris::Player &player : m_watcher->players()) {
        const auto same = std::ranges::find_if(players, [&player](const QVariant &entry) {
            return entry.toMap().value(u"id"_s).toString() == player.key;
        });
        const bool playing = player.playback == mpris::Playback::Playing;
        if (same != players.end()) {
            if (playing) {
                QVariantMap entry = same->toMap();
                entry.insert(u"playing"_s, true);
                *same = entry;
            }
            continue;
        }
        players.append(QVariantMap{
            {u"id"_s, player.key},
            {u"name"_s, player.identity.isEmpty() ? player.key : player.identity},
            {u"playing"_s, playing},
        });
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::ranges::sort(players, [&collator](const QVariant &left, const QVariant &right) {
        return collator.compare(left.toMap().value(u"name"_s).toString(), right.toMap().value(u"name"_s).toString())
            < 0;
    });
    if (players != m_players) {
        m_players = std::move(players);
        Q_EMIT changed();
    }
}

} // namespace rb::ui
