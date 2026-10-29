// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "pw/virtualdeviceconf.h"

#include <QLatin1StringView>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <cstdint>
#include <expected>
#include <optional>

namespace rb::pw
{
struct VirtualDeviceState;
}

namespace rb::core
{

// What the facade says about virtual devices (ADR-0013), shared by the
// engine and the mock so that the two cannot drift apart on the wire.

/// The reason a CreateVirtualDevice whose JSON makes no sense is refused
/// with: not an object, or a direction or channel count nobody knows.
inline constexpr QLatin1StringView kVirtualDeviceBadRequest{"BadRequest"};

/// The longest name a device may have, in characters, for an interface
/// that wants to say so before it asks.
inline constexpr int kVirtualDeviceNameLimit = static_cast<int>(pw::kMaxVirtualDeviceNameLength);

/// A CreateVirtualDevice request, read.
struct VirtualDeviceRequest {
    /// As sent: the manager trims it and says whether it will do.
    QString name;
    pw::VirtualDeviceDirection direction = pw::VirtualDeviceDirection::Output;
    pw::VirtualDeviceChannels channels = pw::VirtualDeviceChannels::Stereo;
};

/// Reads `{name, direction, channels}`. "direction" is output or input and
/// must be there; "channels" is mono or stereo, and stereo when left out.
/// **The name is not judged here**: that is the manager's, which is also the
/// helper's, and one place to say what a good name is is enough. The error
/// is the reason to refuse with, kVirtualDeviceBadRequest.
[[nodiscard]] std::expected<VirtualDeviceRequest, QString> virtualDeviceRequestFromJson(const QString &json);

/// The name the create request carries, whatever else is wrong with it, so
/// that a refusal can say which device it was about.
[[nodiscard]] QString virtualDeviceRequestName(const QString &json);

/// The VirtualDevicesChanged message: `{devices: [...], unreadable: {...}}`,
/// "unreadable" naming each scope whose file could not be read and why.
[[nodiscard]] QString virtualDevicesMessage(const QList<pw::VirtualDeviceState> &devices,
                                            std::optional<pw::VirtualDeviceError> userFile,
                                            std::optional<pw::VirtualDeviceError> systemFile);

/// Every reason a device.virtualNotCreated or device.virtualRefused can
/// carry from the engine or the helper, so that a test can hold the
/// interface to having words for each.
[[nodiscard]] QStringList virtualDeviceReasons();

// ---- All users ------------------------------------------------------
//
// An all-users device is changed by the privileged helper, which writes the
// whole system file. The interface composes the list the helper is to write
// from what the engine last listed; these functions do that, so that it can
// be tested without a window, a bus or a password.

/// The reason the helper gives when the system file no longer holds the list
/// the change started from: somebody else changed the all-users devices
/// meanwhile. The helper's own constant says the same (helper/).
inline constexpr QLatin1StringView kVirtualDeviceChangedElsewhere{"ChangedElsewhere"};

/// The devices a VirtualDevicesChanged message lists, as their files hold
/// them, and the scopes whose file could not be read ("user", "system").
struct VirtualDeviceListing {
    QList<pw::VirtualDevice> user;
    QList<pw::VirtualDevice> system;
    QStringList unreadable;
    /// The engine's reason for each unreadable scope, by scope name.
    QVariantMap unreadableReasons;
};

/// Reads a VirtualDevicesChanged message. An entry that makes no sense is
/// left out; a message that makes none is an empty listing.
[[nodiscard]] VirtualDeviceListing virtualDeviceListingFromMessage(const QString &message);

/// One change to the all-users devices, as the window asks for it.
struct SystemDeviceChange {
    enum class Kind : std::uint8_t {
        Create,
        Rename,
        SetEnabled,
        Remove,
    };
    Kind kind = Kind::Create;
    /// The device changed; not used by Create, which makes a new id.
    QString deviceId;
    /// Create's and Rename's name, as typed: it is trimmed and judged here.
    QString name;
    pw::VirtualDeviceDirection direction = pw::VirtualDeviceDirection::Output;
    pw::VirtualDeviceChannels channels = pw::VirtualDeviceChannels::Stereo;
    /// SetEnabled's new state.
    bool enabled = true;
};

/// What the helper is to be asked to write.
struct SystemDeviceWrite {
    /// The system file's list as the engine last listed it. The helper
    /// writes nothing unless the file still holds exactly this.
    QList<pw::VirtualDevice> previous;
    /// The whole list the file is to hold afterwards.
    QList<pw::VirtualDevice> devices;
    /// The device the change is about: Create's new id.
    QString deviceId;
    /// Its name, for a message about it: the new one for Create and
    /// Rename, the old one otherwise.
    QString deviceName;

    /// The helper action's arguments: `devices` and `previous`, in the form
    /// pw::virtualDevicesToVariant() writes.
    [[nodiscard]] QVariantMap helperArguments() const;
};

/// Works out the list the helper is to write for \a change, from the
/// engine's last VirtualDevicesChanged \a message.
///
/// Refused, with the reason to give, when:
/// - the system file could not be read: its devices are not known, and
///   writing the list without them would lose them (the engine's reason);
/// - there is no all-users device with that id (NotFound);
/// - the name is not one the engine would take for the user's own device,
///   or the file would hold too many (a pw::VirtualDeviceError's name).
///
/// A new device's id is unique across both files, since PipeWire loads
/// both.
[[nodiscard]] std::expected<SystemDeviceWrite, QString> planSystemDeviceChange(const QString &message,
                                                                               const SystemDeviceChange &change);

/// The name of a pw::VirtualDeviceError, as a reason argument.
[[nodiscard]] QString virtualDeviceReason(pw::VirtualDeviceError error);

/// The error a reason names, or nothing for a word that is not one.
[[nodiscard]] std::optional<pw::VirtualDeviceError> virtualDeviceErrorFromReason(const QString &reason);

} // namespace rb::core
