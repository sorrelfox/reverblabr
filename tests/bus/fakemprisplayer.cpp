// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// **A media player, played by a test**, in a process of its own on the
// test's private bus: it has a player's name and says what it is playing
// as MPRIS players do, through its properties and PropertiesChanged, and
// plays nothing. A process of its own so that the watcher finds it by its
// connection's process, as it finds a real player.
//
// The test changes what it plays with org.reverblabr.Test.SetTrack(artist,
// title, album) and SetPlayback("Playing" | "Paused" | "Stopped"), and can
// have a change said only as invalidated (SetInvalidateOnly(b)), as some
// players say it. It refuses to start on anything that looks like a
// desktop's session bus.
//
//   reverblabr-fake-mpris-player --name <bus name suffix> [--identity <name>] [--desktop-entry <id>]

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QStringList>

#include <cstdio>

using namespace Qt::StringLiterals;

namespace
{

const QString kPath = u"/org/mpris/MediaPlayer2"_s;
const QString kRoot = u"org.mpris.MediaPlayer2"_s;
const QString kPlayer = u"org.mpris.MediaPlayer2.Player"_s;
const QString kProperties = u"org.freedesktop.DBus.Properties"_s;
const QString kTestInterface = u"org.reverblabr.Test"_s;

class Player : public QDBusVirtualObject
{
public:
    Player(QString identity, QString desktopEntry)
        : m_identity(std::move(identity))
        , m_desktopEntry(std::move(desktopEntry))
    {
    }

    [[nodiscard]] QString introspect(const QString &path) const override
    {
        Q_UNUSED(path)
        return uR"(<interface name="org.mpris.MediaPlayer2">
  <property name="Identity" type="s" access="read"/>
  <property name="DesktopEntry" type="s" access="read"/>
  <property name="CanQuit" type="b" access="read"/>
  <property name="CanRaise" type="b" access="read"/>
  <property name="HasTrackList" type="b" access="read"/>
  <property name="SupportedUriSchemes" type="as" access="read"/>
  <property name="SupportedMimeTypes" type="as" access="read"/>
</interface>
<interface name="org.mpris.MediaPlayer2.Player">
  <property name="PlaybackStatus" type="s" access="read"/>
  <property name="Metadata" type="a{sv}" access="read"/>
</interface>
<interface name="org.reverblabr.Test">
  <method name="SetTrack"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="s" direction="in"/></method>
  <method name="SetPlayback"><arg type="s" direction="in"/></method>
  <method name="SetInvalidateOnly"><arg type="b" direction="in"/></method>
</interface>)"_s;
    }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        const QString member = message.member();
        const QList<QVariant> arguments = message.arguments();
        if (message.interface() == kProperties) {
            if (member == "Get"_L1 && arguments.size() == 2) {
                const QVariant value = properties(arguments.at(0).toString()).value(arguments.at(1).toString());
                if (!value.isValid()) {
                    return false;
                }
                connection.send(message.createReply(QVariant::fromValue(QDBusVariant(value))));
                return true;
            }
            if (member == "GetAll"_L1 && arguments.size() == 1) {
                connection.send(message.createReply(properties(arguments.at(0).toString())));
                return true;
            }
            return false;
        }
        if (message.interface() != kTestInterface) {
            return false;
        }
        if (member == "SetTrack"_L1 && arguments.size() == 3) {
            m_artist = arguments.at(0).toString();
            m_title = arguments.at(1).toString();
            m_album = arguments.at(2).toString();
            ++m_track;
            connection.send(message.createReply());
            changed(connection, u"Metadata"_s);
            return true;
        }
        if (member == "SetPlayback"_L1 && arguments.size() == 1) {
            m_playback = arguments.at(0).toString();
            connection.send(message.createReply());
            changed(connection, u"PlaybackStatus"_s);
            return true;
        }
        if (member == "SetInvalidateOnly"_L1 && arguments.size() == 1) {
            m_invalidateOnly = arguments.at(0).toBool();
            connection.send(message.createReply());
            return true;
        }
        return false;
    }

private:
    [[nodiscard]] QVariantMap properties(const QString &interface) const
    {
        if (interface == kRoot) {
            return {
                {u"Identity"_s, m_identity},
                {u"DesktopEntry"_s, m_desktopEntry},
                {u"CanQuit"_s, false},
                {u"CanRaise"_s, false},
                {u"HasTrackList"_s, false},
                {u"SupportedUriSchemes"_s, QStringList{}},
                {u"SupportedMimeTypes"_s, QStringList{}},
            };
        }
        if (interface == kPlayer) {
            QVariantMap metadata{
                {u"mpris:trackid"_s, QVariant::fromValue(QDBusObjectPath(u"/org/reverblabr/track/%1"_s.arg(m_track)))},
            };
            // **The artist is a list**, as the specification has it.
            if (!m_artist.isEmpty()) {
                metadata.insert(u"xesam:artist"_s, QStringList{m_artist});
            }
            if (!m_title.isEmpty()) {
                metadata.insert(u"xesam:title"_s, m_title);
            }
            if (!m_album.isEmpty()) {
                metadata.insert(u"xesam:album"_s, m_album);
            }
            return {{u"PlaybackStatus"_s, m_playback}, {u"Metadata"_s, metadata}};
        }
        return {};
    }

    void changed(const QDBusConnection &connection, const QString &property)
    {
        QDBusMessage signal = QDBusMessage::createSignal(kPath, kProperties, u"PropertiesChanged"_s);
        if (m_invalidateOnly) {
            signal << kPlayer << QVariantMap{} << QStringList{property};
        } else {
            signal << kPlayer << QVariantMap{{property, properties(kPlayer).value(property)}} << QStringList{};
        }
        connection.send(signal);
    }

    QString m_identity;
    QString m_desktopEntry;
    QString m_playback = u"Stopped"_s;
    QString m_artist;
    QString m_title;
    QString m_album;
    int m_track = 0;
    bool m_invalidateOnly = false;
};

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QByteArray address = qgetenv("DBUS_SESSION_BUS_ADDRESS");
    if (address.isEmpty() || address.contains(QByteArray("/run/user/"))) {
        std::fputs("refusing to play a media player on what looks like a desktop's session bus\n", stderr);
        return 2;
    }
    QCommandLineParser parser;
    const QCommandLineOption name(u"name"_s, u"The bus name after org.mpris.MediaPlayer2."_s, u"suffix"_s);
    const QCommandLineOption identity(u"identity"_s, u"The name it gives itself."_s, u"name"_s, u"Test Player"_s);
    const QCommandLineOption desktopEntry(u"desktop-entry"_s, u"Its desktop entry."_s, u"id"_s);
    parser.addOptions({name, identity, desktopEntry});
    parser.process(application);
    if (parser.value(name).isEmpty()) {
        std::fputs("--name is needed\n", stderr);
        return 1;
    }

    Player player(parser.value(identity), parser.value(desktopEntry));
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerVirtualObject(kPath, &player) || !bus.registerService(kRoot + u'.' + parser.value(name))) {
        std::fputs("could not take the player's name\n", stderr);
        return 1;
    }
    std::fputs("ready\n", stdout);
    std::fflush(stdout);
    return QCoreApplication::exec();
}
