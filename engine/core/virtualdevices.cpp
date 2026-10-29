// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/virtualdevices.h"

#include "pw/virtualdevicemanager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>
#include <array>

using namespace Qt::StringLiterals;

namespace rb::core
{

namespace
{

[[nodiscard]] QString asText(std::string_view name)
{
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

/// Every error there is, for the reasons list. **Kept next to the enum's
/// own switch in spirit**: a test fails when the enum grows and this does
/// not, because the reasons list would then lack its words.
constexpr std::array kEveryError{
    pw::VirtualDeviceError::EmptyName,
    pw::VirtualDeviceError::NameTooLong,
    pw::VirtualDeviceError::NameHasControlCharacter,
    pw::VirtualDeviceError::NameNotTrimmed,
    pw::VirtualDeviceError::InvalidId,
    pw::VirtualDeviceError::DuplicateId,
    pw::VirtualDeviceError::WrongScope,
    pw::VirtualDeviceError::TooManyDevices,
    pw::VirtualDeviceError::NotOurs,
    pw::VirtualDeviceError::Malformed,
    pw::VirtualDeviceError::FileNotReadable,
    pw::VirtualDeviceError::FileNotWritable,
    pw::VirtualDeviceError::NotFound,
    pw::VirtualDeviceError::ReadOnlyScope,
};

/// One entry of a VirtualDevicesChanged message, or nothing for one that
/// makes no sense.
[[nodiscard]] std::optional<pw::VirtualDevice> deviceFrom(const QJsonObject &object)
{
    pw::VirtualDevice device;
    device.id = object.value("id"_L1).toString();
    device.name = object.value("name"_L1).toString();
    device.enabled = object.value("enabled"_L1).toBool(true);

    const QString direction = object.value("direction"_L1).toString();
    if (direction == "output"_L1) {
        device.direction = pw::VirtualDeviceDirection::Output;
    } else if (direction == "input"_L1) {
        device.direction = pw::VirtualDeviceDirection::Input;
    } else {
        return std::nullopt;
    }
    const QString channels = object.value("channels"_L1).toString();
    if (channels == "stereo"_L1) {
        device.channels = pw::VirtualDeviceChannels::Stereo;
    } else if (channels == "mono"_L1) {
        device.channels = pw::VirtualDeviceChannels::Mono;
    } else {
        return std::nullopt;
    }
    const QString scope = object.value("scope"_L1).toString();
    if (scope == pw::virtualDeviceScopeName(pw::VirtualDeviceScope::User)) {
        device.scope = pw::VirtualDeviceScope::User;
    } else if (scope == pw::virtualDeviceScopeName(pw::VirtualDeviceScope::System)) {
        device.scope = pw::VirtualDeviceScope::System;
    } else {
        return std::nullopt;
    }
    if (!pw::isValidVirtualDeviceId(device.id)) {
        return std::nullopt;
    }
    return device;
}

} // namespace

VirtualDeviceListing virtualDeviceListingFromMessage(const QString &message)
{
    const QJsonObject object = QJsonDocument::fromJson(message.toUtf8()).object();
    VirtualDeviceListing listing;
    for (const QJsonValue &entry : object.value("devices"_L1).toArray()) {
        if (const std::optional<pw::VirtualDevice> device = deviceFrom(entry.toObject())) {
            (device->scope == pw::VirtualDeviceScope::User ? listing.user : listing.system).append(*device);
        }
    }
    listing.unreadableReasons = object.value("unreadable"_L1).toObject().toVariantMap();
    listing.unreadable = listing.unreadableReasons.keys();
    return listing;
}

QVariantMap SystemDeviceWrite::helperArguments() const
{
    return {
        {u"devices"_s, pw::virtualDevicesToVariant(devices)},
        {u"previous"_s, pw::virtualDevicesToVariant(previous)},
    };
}

std::expected<SystemDeviceWrite, QString> planSystemDeviceChange(const QString &message,
                                                                 const SystemDeviceChange &change)
{
    const QString system = pw::virtualDeviceScopeName(pw::VirtualDeviceScope::System);
    const VirtualDeviceListing listing = virtualDeviceListingFromMessage(message);
    if (listing.unreadable.contains(system)) {
        return std::unexpected(listing.unreadableReasons.value(system).toString());
    }

    SystemDeviceWrite write;
    write.previous = listing.system;
    write.devices = listing.system;

    const auto trimmedName = [&change]() { return pw::validVirtualDeviceName(change.name); };

    if (change.kind == SystemDeviceChange::Kind::Create) {
        const std::expected<QString, pw::VirtualDeviceError> name = trimmedName();
        if (!name) {
            return std::unexpected(virtualDeviceReason(name.error()));
        }
        pw::VirtualDevice device;
        device.id = pw::newVirtualDeviceId(listing.user + listing.system);
        device.name = *name;
        device.direction = change.direction;
        device.channels = change.channels;
        device.enabled = true;
        device.scope = pw::VirtualDeviceScope::System;
        write.devices.append(device);
        write.deviceId = device.id;
        write.deviceName = device.name;
    } else {
        const auto found = std::ranges::find_if(
            write.devices, [&change](const pw::VirtualDevice &device) { return device.id == change.deviceId; });
        if (found == write.devices.end()) {
            return std::unexpected(virtualDeviceReason(pw::VirtualDeviceError::NotFound));
        }
        write.deviceId = found->id;
        write.deviceName = found->name;
        switch (change.kind) {
        case SystemDeviceChange::Kind::Rename: {
            const std::expected<QString, pw::VirtualDeviceError> name = trimmedName();
            if (!name) {
                return std::unexpected(virtualDeviceReason(name.error()));
            }
            found->name = *name;
            write.deviceName = *name;
            break;
        }
        case SystemDeviceChange::Kind::SetEnabled:
            found->enabled = change.enabled;
            break;
        case SystemDeviceChange::Kind::Remove:
            write.devices.erase(found);
            break;
        case SystemDeviceChange::Kind::Create:
            break;
        }
    }

    if (const auto valid = pw::validateVirtualDevices(write.devices, pw::VirtualDeviceScope::System); !valid) {
        return std::unexpected(virtualDeviceReason(valid.error()));
    }
    return write;
}

std::expected<VirtualDeviceRequest, QString> virtualDeviceRequestFromJson(const QString &json)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return std::unexpected(QString(kVirtualDeviceBadRequest));
    }
    const QJsonObject object = document.object();

    VirtualDeviceRequest request;
    request.name = object.value("name"_L1).toString();

    const QString direction = object.value("direction"_L1).toString();
    if (direction == "output"_L1) {
        request.direction = pw::VirtualDeviceDirection::Output;
    } else if (direction == "input"_L1) {
        request.direction = pw::VirtualDeviceDirection::Input;
    } else {
        return std::unexpected(QString(kVirtualDeviceBadRequest));
    }

    const QJsonValue channels = object.value("channels"_L1);
    if (channels.isUndefined() || channels.toString() == "stereo"_L1) {
        request.channels = pw::VirtualDeviceChannels::Stereo;
    } else if (channels.toString() == "mono"_L1) {
        request.channels = pw::VirtualDeviceChannels::Mono;
    } else {
        return std::unexpected(QString(kVirtualDeviceBadRequest));
    }
    return request;
}

QString virtualDeviceRequestName(const QString &json)
{
    return QJsonDocument::fromJson(json.toUtf8()).object().value("name"_L1).toString();
}

QString virtualDevicesMessage(const QList<pw::VirtualDeviceState> &devices,
                              std::optional<pw::VirtualDeviceError> userFile,
                              std::optional<pw::VirtualDeviceError> systemFile)
{
    QJsonObject unreadable;
    if (userFile) {
        unreadable.insert(pw::virtualDeviceScopeName(pw::VirtualDeviceScope::User), virtualDeviceReason(*userFile));
    }
    if (systemFile) {
        unreadable.insert(pw::virtualDeviceScopeName(pw::VirtualDeviceScope::System), virtualDeviceReason(*systemFile));
    }
    const QJsonObject message{
        {"devices"_L1, pw::virtualDevicesToJson(devices)},
        {"unreadable"_L1, unreadable},
    };
    return QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact));
}

QString virtualDeviceReason(pw::VirtualDeviceError error)
{
    return asText(pw::virtualDeviceErrorName(error));
}

std::optional<pw::VirtualDeviceError> virtualDeviceErrorFromReason(const QString &reason)
{
    for (const pw::VirtualDeviceError error : kEveryError) {
        if (virtualDeviceReason(error) == reason) {
            return error;
        }
    }
    return std::nullopt;
}

QStringList virtualDeviceReasons()
{
    QStringList reasons;
    for (const pw::VirtualDeviceError error : kEveryError) {
        reasons.append(virtualDeviceReason(error));
    }
    reasons.append(QString(kVirtualDeviceBadRequest));
    reasons.append(QString(kVirtualDeviceChangedElsewhere));
    return reasons;
}

} // namespace rb::core
