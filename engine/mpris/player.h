// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <cstdint>
#include <span>
#include <vector>

/// **The media players on the session bus**, as the MPRIS specification
/// has them say what they are playing: who they are, whether they are
/// playing, and the track's artist, title and album. Read, never driven:
/// nothing here asks a player to do anything.
namespace rb::mpris
{

/// The prefix every player's bus name starts with.
inline constexpr QLatin1StringView kBusPrefix{"org.mpris.MediaPlayer2."};

/// What a player says it is playing.
struct Track {
    QString artist;
    QString title;
    QString album;

    [[nodiscard]] bool operator==(const Track &other) const = default;
    [[nodiscard]] bool isEmpty() const
    {
        return artist.isEmpty() && title.isEmpty() && album.isEmpty();
    }
};

enum class Playback : std::uint8_t {
    Stopped,
    Paused,
    Playing,
};

/// One player, as the bus and the player itself have said it.
struct Player {
    /// Its name on the bus: `org.mpris.MediaPlayer2.vlc.instance4242`.
    QString busName;
    /// **Which player it is, across restarts**: the bus name without the
    /// prefix and without the part a player adds to tell its own copies
    /// apart (playerKey()). What a block that names a player stores.
    QString key;
    /// The name it gives itself ("VLC media player"), for a person to read.
    QString identity;
    /// Its desktop entry without ".desktop": a Flatpak's application id,
    /// for one in a sandbox.
    QString desktopEntry;
    /// The process on the other end of its connection, and the names it
    /// was started by (its command's and its executable's), which is what
    /// the audio system calls it too. Zero and empty when the bus would
    /// not say.
    std::uint32_t pid = 0;
    QStringList binaries;
    Playback playback = Playback::Stopped;
    Track track;
    /// When it last started playing, as a count that only goes up: the
    /// most recent of several playing players is the one with the highest.
    std::uint64_t playingSince = 0;

    [[nodiscard]] bool operator==(const Player &other) const = default;
};

/// \a busName's player key: "vlc" for `org.mpris.MediaPlayer2.vlc` and for
/// `org.mpris.MediaPlayer2.vlc.instance4242`, "firefox" for
/// `org.mpris.MediaPlayer2.firefox.instance_1_23`. Empty for a name that
/// is not a player's.
[[nodiscard]] QString playerKey(const QString &busName);

/// The track in a player's `Metadata`: `xesam:artist` (a list, joined with
/// commas), `xesam:title` and `xesam:album`, trimmed. A player's metadata
/// may come as it was demarshalled from the bus, maps and lists inside
/// variants, or already as Qt types.
[[nodiscard]] Track trackFromMetadata(const QVariantMap &metadata);

/// A player's `PlaybackStatus`: anything but "Playing" and "Paused" is
/// stopped.
[[nodiscard]] Playback playbackFromStatus(const QString &status);

/// **An application a session captures**, by the same three names an
/// Application block stores. Any of them may be empty.
struct CapturedApplication {
    QString binary;
    QString flatpakId;
    QString name;
};

/// Whether \a player is \a application: its binary is one of the player's,
/// or its Flatpak id is the player's desktop entry, or its name is the
/// player's identity, ignoring case.
[[nodiscard]] bool isApplication(const Player &player, const CapturedApplication &application);

/// **Which player a stream's title follows.** \a wanted is a player key, or
/// empty for the automatic choice: a player the session captures, or any
/// player when the session captures no application (a device, the whole
/// system's sound). Of the players that are left, one that is playing
/// before one that is paused, before one that is stopped; of two that are
/// playing, the one that started last. Nothing when none is left.
[[nodiscard]] const Player *
choosePlayer(std::span<const Player> players, const QString &wanted, std::span<const CapturedApplication> captured);

/// What \a player says is playing, for a title: its track, and nothing
/// while it is stopped, or when there is no player at all.
[[nodiscard]] Track nowPlaying(const Player *player);

} // namespace rb::mpris
