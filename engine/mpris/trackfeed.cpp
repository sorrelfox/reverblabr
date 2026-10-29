// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "mpris/trackfeed.h"

#include "mpris/playerwatcher.h"

#include <algorithm>

namespace rb::mpris
{

TrackFeed::TrackFeed(PlayerWatcher &watcher, Options options, QObject *parent)
    : QObject(parent)
    , m_watcher(watcher)
    , m_options(options)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &TrackFeed::deliver);
    connect(&m_watcher, &PlayerWatcher::changed, this, &TrackFeed::reconsider);
    reconsider();
}

TrackFeed::TrackFeed(PlayerWatcher &watcher, QObject *parent)
    : TrackFeed(watcher, Options{}, parent)
{
}

void TrackFeed::setChoice(const QString &wanted, std::vector<CapturedApplication> captured)
{
    m_wanted = wanted;
    m_captured = std::move(captured);
    reconsider();
}

QString TrackFeed::playerBusName() const
{
    return m_player;
}

std::optional<Track> TrackFeed::lastTrack() const
{
    return m_sent;
}

void TrackFeed::reconsider()
{
    // **Nothing until the listing is in**: before it, "no player" only
    // means the bus has not answered yet.
    if (!m_watcher.isReady()) {
        return;
    }
    const Player *player = choosePlayer(m_watcher.players(), m_wanted, m_captured);
    m_player = player != nullptr ? player->busName : QString();
    m_current = nowPlaying(player);
    if (m_sent == m_current) {
        m_timer.stop();
        return;
    }
    if (m_timer.isActive()) {
        // Already on its way, and it takes whatever is current then.
        return;
    }
    std::chrono::milliseconds wait = m_options.settle;
    if (m_sent && m_sinceSent.isValid()) {
        const std::chrono::milliseconds since{m_sinceSent.elapsed()};
        wait = std::max(wait, m_options.minimumGap - since);
    }
    m_timer.start(wait);
}

void TrackFeed::deliver()
{
    if (m_sent == m_current) {
        return;
    }
    m_sent = m_current;
    m_sinceSent.start();
    Q_EMIT trackChanged(m_current);
}

} // namespace rb::mpris
