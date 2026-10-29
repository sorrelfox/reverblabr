// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "pw/pwobjects.h"

#include <pipewire/keys.h>

using namespace Qt::StringLiterals;

namespace rb::pw
{

QString portDirectionName(PortDirection direction)
{
    return direction == PortDirection::Input ? u"input"_s : u"output"_s;
}

QString nodeKindName(NodeKind kind)
{
    switch (kind) {
    case NodeKind::Other:
        return u"other"_s;
    case NodeKind::Sink:
        return u"sink"_s;
    case NodeKind::Source:
        return u"source"_s;
    case NodeKind::PlaybackStream:
        return u"playbackStream"_s;
    case NodeKind::RecordStream:
        return u"recordStream"_s;
    }
    return u"other"_s;
}

NodeKind nodeKindFromMediaClass(QStringView mediaClass)
{
    if (mediaClass == u"Audio/Sink") {
        return NodeKind::Sink;
    }
    if (mediaClass == u"Audio/Source" || mediaClass == u"Audio/Source/Virtual") {
        return NodeKind::Source;
    }
    if (mediaClass == u"Stream/Output/Audio") {
        return NodeKind::PlaybackStream;
    }
    if (mediaClass == u"Stream/Input/Audio") {
        return NodeKind::RecordStream;
    }
    return NodeKind::Other;
}

namespace
{

/// The key the desktop portal puts a sandboxed application's id under.
constexpr const char *kPortalAppIdKey = "pipewire.access.portal.app_id";

[[nodiscard]] QString text(const QHash<QString, QString> &props, const char *key)
{
    return props.value(QString::fromUtf8(key));
}

[[nodiscard]] qint64 number(const QHash<QString, QString> &props, const char *key, qint64 fallback)
{
    bool ok = false;
    const qint64 value = text(props, key).toLongLong(&ok);
    return ok ? value : fallback;
}

} // namespace

void readNodeProperties(PwNode &node)
{
    const QHash<QString, QString> &props = node.props;
    node.name = text(props, PW_KEY_NODE_NAME);
    node.description = text(props, PW_KEY_NODE_DESCRIPTION);
    node.nick = text(props, PW_KEY_NODE_NICK);
    node.mediaClass = text(props, PW_KEY_MEDIA_CLASS);
    node.kind = nodeKindFromMediaClass(node.mediaClass);
    node.mediaRole = text(props, PW_KEY_MEDIA_ROLE);
    node.applicationName = text(props, PW_KEY_APP_NAME);
    node.applicationBinary = text(props, PW_KEY_APP_PROCESS_BINARY);
    node.applicationPid = number(props, PW_KEY_APP_PROCESS_ID, -1);
    node.applicationIconName = text(props, PW_KEY_APP_ICON_NAME);
    node.flatpakId = text(props, kPortalAppIdKey);
    if (node.flatpakId.isEmpty()) {
        node.flatpakId = text(props, PW_KEY_APP_ID);
    }
    node.linkGroup = text(props, PW_KEY_NODE_LINK_GROUP);
    node.eventId = props.value(u"event.id"_s);
    node.isVirtual = text(props, PW_KEY_NODE_VIRTUAL) == u"true"_s;
    node.clientId = static_cast<std::uint32_t>(number(props, PW_KEY_CLIENT_ID, 0));
    node.deviceId = static_cast<std::uint32_t>(number(props, PW_KEY_DEVICE_ID, 0));
    node.driverId = static_cast<std::uint32_t>(number(props, "node.driver-id", 0));
    node.canDrive = text(props, PW_KEY_NODE_DRIVER) == u"true"_s;
}

void readPortProperties(PwPort &port)
{
    const QHash<QString, QString> &props = port.props;
    port.nodeId = static_cast<std::uint32_t>(number(props, PW_KEY_NODE_ID, 0));
    port.index = static_cast<std::uint32_t>(number(props, PW_KEY_PORT_ID, 0));
    port.name = text(props, PW_KEY_PORT_NAME);
    port.channel = text(props, PW_KEY_AUDIO_CHANNEL);
    port.isMonitor = text(props, PW_KEY_PORT_MONITOR) == u"true"_s;
    const QString direction = text(props, PW_KEY_PORT_DIRECTION);
    if (direction == u"in"_s || direction == u"input"_s) {
        port.direction = PortDirection::Input;
    } else if (direction == u"out"_s || direction == u"output"_s) {
        port.direction = PortDirection::Output;
    }
}

void readDeviceProperties(PwDevice &device)
{
    const QHash<QString, QString> &props = device.props;
    device.name = text(props, PW_KEY_DEVICE_NAME);
    device.description = text(props, PW_KEY_DEVICE_DESCRIPTION);
    device.nick = text(props, PW_KEY_DEVICE_NICK);
    device.mediaClass = text(props, PW_KEY_MEDIA_CLASS);
}

bool PwNode::isNotification() const
{
    return mediaRole.compare(u"Notification"_s, Qt::CaseInsensitive) == 0 || !eventId.isEmpty();
}

bool PwNode::isReplayed() const
{
    return !linkGroup.isEmpty();
}

} // namespace rb::pw
