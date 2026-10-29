// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "mpris/playerwatcher.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QFileInfo>
#include <QHash>

#include <algorithm>
#include <filesystem>
#include <system_error>

using namespace Qt::StringLiterals;

namespace rb::mpris
{

namespace
{

const QString kBusService = u"org.freedesktop.DBus"_s;
const QString kBusPath = u"/org/freedesktop/DBus"_s;
const QString kBusInterface = u"org.freedesktop.DBus"_s;
const QString kPlayerPath = u"/org/mpris/MediaPlayer2"_s;
const QString kRootInterface = u"org.mpris.MediaPlayer2"_s;
const QString kPlayerInterface = u"org.mpris.MediaPlayer2.Player"_s;
const QString kProperties = u"org.freedesktop.DBus.Properties"_s;

/// A map the bus handed over still marshalled, as a{sv} inside a variant is.
[[nodiscard]] QVariantMap asMap(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
        return qdbus_cast<QVariantMap>(value.value<QDBusArgument>());
    }
    return value.toMap();
}

/// **The names a process was started by**: its command's, which is what
/// the audio system calls an application, and its executable's, which a
/// wrapper script or a symbolic link can make another.
[[nodiscard]] QStringList binariesOf(std::uint32_t pid)
{
    QStringList names;
    if (pid == 0) {
        return names;
    }
    const QString proc = u"/proc/%1/"_s.arg(pid);
    QFile cmdline(proc + u"cmdline"_s);
    if (cmdline.open(QIODevice::ReadOnly)) {
        const QByteArray first = cmdline.read(4096).split('\0').constFirst();
        if (const QString name = QFileInfo(QString::fromLocal8Bit(first)).fileName(); !name.isEmpty()) {
            names.append(name);
        }
    }
    std::error_code error;
    const std::filesystem::path exe = std::filesystem::read_symlink(QString(proc + u"exe"_s).toStdString(), error);
    if (!error) {
        QString name = QString::fromStdString(exe.filename().string());
        // An executable replaced while it runs, as an update does.
        name.remove(" (deleted)"_L1);
        if (!name.isEmpty() && !names.contains(name)) {
            names.append(name);
        }
    }
    return names;
}

} // namespace

class PlayerWatcher::Private
{
public:
    explicit Private(PlayerWatcher *owner, const QDBusConnection &connection)
        : q(owner)
        , bus(connection)
    {
    }

    PlayerWatcher *q;
    QDBusConnection bus;
    std::vector<Player> players;
    /// The connection that owns each player's name. **An answer from any
    /// other is let go**: a player that quit and came back has a new one,
    /// and what the old one said is not about it.
    QHash<QString, QString> owners;
    bool ready = false;
    std::uint64_t starts = 0;

    [[nodiscard]] Player *find(const QString &busName)
    {
        const auto found = std::ranges::find(players, busName, &Player::busName);
        return found != players.end() ? &*found : nullptr;
    }

    /// Calls \a member on the bus or a player, and hands the answer to
    /// \a done if \a busName is still owned by \a owner by then.
    template <typename Done>
    void ask(const QDBusMessage &call, const QString &busName, const QString &owner, Done done)
    {
        auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), q);
        QObject::connect(
            watcher, &QDBusPendingCallWatcher::finished, q, [this, watcher, busName, owner, done = std::move(done)] {
                watcher->deleteLater();
                if (watcher->isError() || owners.value(busName) != owner) {
                    return;
                }
                if (Player *player = find(busName)) {
                    done(*player, watcher->reply());
                }
            });
    }

    void list()
    {
        const QDBusMessage call = QDBusMessage::createMethodCall(kBusService, kBusPath, kBusInterface, u"ListNames"_s);
        auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), q);
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, q, [this, watcher] {
            watcher->deleteLater();
            const QDBusPendingReply<QStringList> reply = *watcher;
            if (!reply.isError()) {
                for (const QString &name : reply.value()) {
                    if (!playerKey(name).isEmpty() && !owners.contains(name)) {
                        owned(name);
                    }
                }
            }
            ready = true;
            Q_EMIT q->ready();
            Q_EMIT q->changed();
        });
    }

    /// A name found in the listing: whose it is, first.
    void owned(const QString &busName)
    {
        QDBusMessage call = QDBusMessage::createMethodCall(kBusService, kBusPath, kBusInterface, u"GetNameOwner"_s);
        call << busName;
        auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), q);
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, q, [this, watcher, busName] {
            watcher->deleteLater();
            const QDBusPendingReply<QString> reply = *watcher;
            // A NameOwnerChanged may have come first, and said it already.
            if (!reply.isError() && !owners.contains(busName)) {
                add(busName, reply.value());
            }
        });
    }

    void add(const QString &busName, const QString &owner)
    {
        owners.insert(busName, owner);
        Player player;
        player.busName = busName;
        player.key = playerKey(busName);
        const auto at = std::ranges::lower_bound(players, busName, {}, &Player::busName);
        players.insert(at, std::move(player));

        QDBusMessage pid =
            QDBusMessage::createMethodCall(kBusService, kBusPath, kBusInterface, u"GetConnectionUnixProcessID"_s);
        pid << owner;
        ask(pid, busName, owner, [this](Player &found, const QDBusMessage &reply) {
            if (reply.arguments().isEmpty()) {
                return;
            }
            found.pid = reply.arguments().constFirst().toUInt();
            found.binaries = binariesOf(found.pid);
            Q_EMIT q->changed();
        });
        readAll(busName, owner, kRootInterface);
        readAll(busName, owner, kPlayerInterface);
        Q_EMIT q->changed();
    }

    void readAll(const QString &busName, const QString &owner, const QString &interface)
    {
        QDBusMessage call = QDBusMessage::createMethodCall(owner, kPlayerPath, kProperties, u"GetAll"_s);
        call << interface;
        ask(call, busName, owner, [this, interface](Player &found, const QDBusMessage &reply) {
            if (!reply.arguments().isEmpty() && apply(found, interface, asMap(reply.arguments().constFirst()))) {
                Q_EMIT q->changed();
            }
        });
    }

    void remove(const QString &busName)
    {
        owners.remove(busName);
        if (const auto found = std::ranges::find(players, busName, &Player::busName); found != players.end()) {
            players.erase(found);
            Q_EMIT q->changed();
        }
    }

    /// Takes in what \a player said about \a interface's properties, and
    /// says whether anything changed.
    bool apply(Player &player, const QString &interface, const QVariantMap &properties)
    {
        const Player before = player;
        if (interface == kRootInterface) {
            if (const auto identity = properties.constFind(u"Identity"_s); identity != properties.constEnd()) {
                player.identity = identity->toString().trimmed();
            }
            if (const auto entry = properties.constFind(u"DesktopEntry"_s); entry != properties.constEnd()) {
                player.desktopEntry = entry->toString().trimmed();
            }
        } else if (interface == kPlayerInterface) {
            if (const auto status = properties.constFind(u"PlaybackStatus"_s); status != properties.constEnd()) {
                const Playback playback = playbackFromStatus(status->toString());
                if (playback == Playback::Playing && player.playback != Playback::Playing) {
                    player.playingSince = ++starts;
                }
                player.playback = playback;
            }
            if (const auto metadata = properties.constFind(u"Metadata"_s); metadata != properties.constEnd()) {
                player.track = trackFromMetadata(asMap(*metadata));
            }
        }
        return !(player == before);
    }
};

PlayerWatcher::PlayerWatcher(const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>(this, bus))
{
    if (!d->bus.isConnected()) {
        return;
    }
    // **Told before asking**, so that a player that comes while the
    // listing is on its way is not missed.
    d->bus.connect(kBusService,
                   kBusPath,
                   kBusInterface,
                   u"NameOwnerChanged"_s,
                   this,
                   SLOT(onNameOwnerChanged(QString, QString, QString)));
    // Any sender: a player is known by its connection, which the slot looks up.
    d->bus.connect(
        QString(), kPlayerPath, kProperties, u"PropertiesChanged"_s, this, SLOT(onPropertiesChanged(QDBusMessage)));
    d->list();
}

PlayerWatcher::~PlayerWatcher()
{
    if (d->bus.isConnected()) {
        d->bus.disconnect(kBusService,
                          kBusPath,
                          kBusInterface,
                          u"NameOwnerChanged"_s,
                          this,
                          SLOT(onNameOwnerChanged(QString, QString, QString)));
        d->bus.disconnect(
            QString(), kPlayerPath, kProperties, u"PropertiesChanged"_s, this, SLOT(onPropertiesChanged(QDBusMessage)));
    }
}

const std::vector<Player> &PlayerWatcher::players() const
{
    return d->players;
}

bool PlayerWatcher::isReady() const
{
    return d->ready;
}

void PlayerWatcher::onNameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner)
{
    if (playerKey(name).isEmpty()) {
        return;
    }
    if (!oldOwner.isEmpty() || d->owners.contains(name)) {
        d->remove(name);
    }
    if (!newOwner.isEmpty()) {
        d->add(name, newOwner);
    }
}

void PlayerWatcher::onPropertiesChanged(const QDBusMessage &message)
{
    const QList<QVariant> arguments = message.arguments();
    if (arguments.size() < 2) {
        return;
    }
    const QString interface = arguments.at(0).toString();
    if (interface != kRootInterface && interface != kPlayerInterface) {
        return;
    }
    const QVariantMap properties = asMap(arguments.at(1));
    const QStringList invalidated = arguments.size() > 2 ? arguments.at(2).toStringList() : QStringList{};
    bool any = false;
    for (auto owner = d->owners.cbegin(); owner != d->owners.cend(); ++owner) {
        if (owner.value() != message.service()) {
            continue;
        }
        Player *player = d->find(owner.key());
        if (player == nullptr) {
            continue;
        }
        any = d->apply(*player, interface, properties) || any;
        // **Changed without the new value**: asked for, as the
        // specification lets a player do.
        if (!invalidated.isEmpty()) {
            d->readAll(owner.key(), owner.value(), interface);
        }
    }
    if (any) {
        Q_EMIT changed();
    }
}

} // namespace rb::mpris
