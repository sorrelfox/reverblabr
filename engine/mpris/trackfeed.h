// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "mpris/player.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <chrono>
#include <optional>
#include <vector>

namespace rb::mpris
{

class PlayerWatcher;

/// **What one stream's title follows**: the player choosePlayer() picks
/// for it among a watcher's, and the track that player says is playing,
/// handed on when it changes.
///
/// **Not every change is handed on at once.** A player says a new track in
/// pieces (the title, then the album, then the length) and some say it
/// again with every skip, and each title a stream is given is a request to
/// its server. So a change waits a moment for the rest of it (settle), and
/// two titles are never handed on closer together than minimumGap; what
/// is handed on is what is playing then, never a step that was overtaken.
class TrackFeed : public QObject
{
    Q_OBJECT

public:
    struct Options {
        std::chrono::milliseconds settle{250};
        std::chrono::milliseconds minimumGap{1000};
    };

    /// \a watcher must outlive the feed.
    TrackFeed(PlayerWatcher &watcher, Options options, QObject *parent = nullptr);
    explicit TrackFeed(PlayerWatcher &watcher, QObject *parent = nullptr);

    /// Which player to follow: \a wanted is a player key, or empty to let
    /// \a captured decide (choosePlayer()).
    void setChoice(const QString &wanted, std::vector<CapturedApplication> captured);

    /// The player followed now, by its bus name; empty for none.
    [[nodiscard]] QString playerBusName() const;
    /// What was last handed on.
    [[nodiscard]] std::optional<Track> lastTrack() const;

Q_SIGNALS:
    /// What is playing now. The first comes as soon as the watcher knows
    /// which players there are, empty if there is none to follow.
    void trackChanged(const rb::mpris::Track &track);

private:
    void reconsider();
    void deliver();

    PlayerWatcher &m_watcher;
    Options m_options;
    QString m_wanted;
    std::vector<CapturedApplication> m_captured;
    QString m_player;
    Track m_current;
    std::optional<Track> m_sent;
    QElapsedTimer m_sinceSent;
    QTimer m_timer;
};

} // namespace rb::mpris
