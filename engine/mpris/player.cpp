// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "mpris/player.h"

#include <QDBusArgument>
#include <QDBusVariant>
#include <QRegularExpression>

#include <algorithm>
#include <tuple>

using namespace Qt::StringLiterals;

namespace rb::mpris
{

namespace
{

/// A value from a player's metadata, out of the variant the bus wraps it in.
[[nodiscard]] QVariant unwrapped(QVariant value)
{
    if (value.canConvert<QDBusVariant>() && value.metaType() == QMetaType::fromType<QDBusVariant>()) {
        value = value.value<QDBusVariant>().variant();
    }
    return value;
}

[[nodiscard]] QString text(const QVariant &wrapped)
{
    const QVariant value = unwrapped(wrapped);
    if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
        // A list of strings where one is expected: the first.
        QStringList list;
        value.value<QDBusArgument>() >> list;
        return list.isEmpty() ? QString() : list.constFirst().trimmed();
    }
    if (value.metaType() == QMetaType::fromType<QStringList>()) {
        const QStringList list = value.toStringList();
        return list.isEmpty() ? QString() : list.constFirst().trimmed();
    }
    return value.toString().trimmed();
}

[[nodiscard]] QString joined(const QVariant &wrapped)
{
    const QVariant value = unwrapped(wrapped);
    QStringList list;
    if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
        value.value<QDBusArgument>() >> list;
    } else if (value.metaType() == QMetaType::fromType<QStringList>()
               || value.metaType() == QMetaType::fromType<QVariantList>()) {
        list = value.toStringList();
    } else {
        list = QStringList{value.toString()};
    }
    QStringList names;
    for (const QString &name : std::as_const(list)) {
        if (const QString trimmed = name.trimmed(); !trimmed.isEmpty()) {
            names.append(trimmed);
        }
    }
    return names.join(", "_L1);
}

[[nodiscard]] int rank(Playback playback)
{
    switch (playback) {
    case Playback::Playing:
        return 2;
    case Playback::Paused:
        return 1;
    case Playback::Stopped:
        break;
    }
    return 0;
}

} // namespace

QString playerKey(const QString &busName)
{
    if (!busName.startsWith(kBusPrefix) || busName.size() == kBusPrefix.size()) {
        return {};
    }
    QString key = busName.mid(kBusPrefix.size());
    // **The part a player adds for a second copy of itself**: the spec
    // asks for ".instance" and the process id, and browsers put more after
    // it. What is before it is the same player every time.
    static const QRegularExpression instance(u"\\.instance[^.]*$"_s);
    key.remove(instance);
    return key;
}

Track trackFromMetadata(const QVariantMap &metadata)
{
    Track track;
    track.artist = joined(metadata.value(u"xesam:artist"_s));
    track.title = text(metadata.value(u"xesam:title"_s));
    track.album = text(metadata.value(u"xesam:album"_s));
    return track;
}

Playback playbackFromStatus(const QString &status)
{
    if (status == "Playing"_L1) {
        return Playback::Playing;
    }
    if (status == "Paused"_L1) {
        return Playback::Paused;
    }
    return Playback::Stopped;
}

bool isApplication(const Player &player, const CapturedApplication &application)
{
    // **Any of the three**, as the engine tries them in turn: a session
    // written for a packaged player goes on finding it as a Flatpak.
    return (!application.binary.isEmpty() && player.binaries.contains(application.binary))
        || (!application.flatpakId.isEmpty() && player.desktopEntry == application.flatpakId)
        || (!application.name.isEmpty() && player.identity.compare(application.name, Qt::CaseInsensitive) == 0);
}

const Player *
choosePlayer(std::span<const Player> players, const QString &wanted, std::span<const CapturedApplication> captured)
{
    const auto eligible = [&](const Player &player) {
        if (!wanted.isEmpty()) {
            return player.key == wanted;
        }
        return captured.empty() || std::ranges::any_of(captured, [&](const CapturedApplication &app) {
                   return isApplication(player, app);
               });
    };
    const Player *best = nullptr;
    for (const Player &player : players) {
        if (!eligible(player)) {
            continue;
        }
        // The bus name last, so that two players alike in every other way
        // are chosen between the same way every time.
        const auto order = [](const Player &each) {
            return std::tuple{rank(each.playback), each.playingSince, each.busName};
        };
        if (best == nullptr || order(*best) < order(player)) {
            best = &player;
        }
    }
    return best;
}

Track nowPlaying(const Player *player)
{
    if (player == nullptr || player->playback == Playback::Stopped) {
        return {};
    }
    return player->track;
}

} // namespace rb::mpris
