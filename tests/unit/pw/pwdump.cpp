// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "pwdump.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

using namespace Qt::StringLiterals;

namespace rb::test
{

namespace
{

/// A property as PipeWire hands it over: text, whatever pw-dump made of it.
[[nodiscard]] QString asText(const QJsonValue &value)
{
    if (value.isBool()) {
        return value.toBool() ? u"true"_s : u"false"_s;
    }
    if (value.isDouble()) {
        return QString::number(value.toInteger());
    }
    if (value.isString()) {
        return value.toString();
    }
    const QJsonDocument nested = value.isArray() ? QJsonDocument(value.toArray()) : QJsonDocument(value.toObject());
    return QString::fromUtf8(nested.toJson(QJsonDocument::Compact));
}

[[nodiscard]] QHash<QString, QString> propsOf(const QJsonObject &info)
{
    QHash<QString, QString> props;
    const QJsonObject json = info.value("props"_L1).toObject();
    for (auto entry = json.constBegin(); entry != json.constEnd(); ++entry) {
        props.insert(entry.key(), asText(entry.value()));
    }
    return props;
}

[[nodiscard]] pw::PwObjectId idOf(const QJsonObject &object, const QHash<QString, QString> &props)
{
    return {static_cast<std::uint32_t>(object.value("id"_L1).toInteger()),
            props.value(u"object.serial"_s).toULongLong()};
}

[[nodiscard]] pw::NodeState nodeStateOf(const QString &state)
{
    static const QHash<QString, pw::NodeState> states{
        {u"error"_s, pw::NodeState::Error},
        {u"creating"_s, pw::NodeState::Creating},
        {u"suspended"_s, pw::NodeState::Suspended},
        {u"idle"_s, pw::NodeState::Idle},
        {u"running"_s, pw::NodeState::Running},
    };
    return states.value(state, pw::NodeState::Unknown);
}

[[nodiscard]] pw::LinkState linkStateOf(const QString &state)
{
    static const QHash<QString, pw::LinkState> states{
        {u"error"_s, pw::LinkState::Error},
        {u"unlinked"_s, pw::LinkState::Unlinked},
        {u"init"_s, pw::LinkState::Init},
        {u"negotiating"_s, pw::LinkState::Negotiating},
        {u"allocating"_s, pw::LinkState::Allocating},
        {u"paused"_s, pw::LinkState::Paused},
        {u"active"_s, pw::LinkState::Active},
    };
    return states.value(state, pw::LinkState::Unknown);
}

} // namespace

PwDump readPwDump(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    PwDump dump;
    const QJsonArray objects = QJsonDocument::fromJson(file.readAll()).array();
    for (const QJsonValue &entry : objects) {
        const QJsonObject object = entry.toObject();
        const QString type = object.value("type"_L1).toString();
        const QJsonObject info = object.value("info"_L1).toObject();
        if (type == "PipeWire:Interface:Node"_L1) {
            pw::PwNode node;
            node.props = propsOf(info);
            node.object = idOf(object, node.props);
            pw::readNodeProperties(node);
            node.state = nodeStateOf(info.value("state"_L1).toString());
            dump.nodes.append(node);
        } else if (type == "PipeWire:Interface:Port"_L1) {
            pw::PwPort port;
            port.props = propsOf(info);
            port.object = idOf(object, port.props);
            pw::readPortProperties(port);
            dump.ports.append(port);
        } else if (type == "PipeWire:Interface:Link"_L1) {
            pw::PwLink link;
            link.object = idOf(object, propsOf(info));
            link.outputNode = static_cast<std::uint32_t>(info.value("output-node-id"_L1).toInteger());
            link.outputPort = static_cast<std::uint32_t>(info.value("output-port-id"_L1).toInteger());
            link.inputNode = static_cast<std::uint32_t>(info.value("input-node-id"_L1).toInteger());
            link.inputPort = static_cast<std::uint32_t>(info.value("input-port-id"_L1).toInteger());
            link.state = linkStateOf(info.value("state"_L1).toString());
            dump.links.append(link);
        } else if (type == "PipeWire:Interface:Device"_L1) {
            pw::PwDevice device;
            device.props = propsOf(info);
            device.object = idOf(object, device.props);
            pw::readDeviceProperties(device);
            dump.devices.append(device);
        }
    }
    return dump;
}

} // namespace rb::test
