// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QVariant>

#include <cstdint>
#include <expected>
#include <string_view>
#include <utility>

namespace rb::pw
{

/// Which way sound goes through a virtual device, seen from the other apps.
enum class VirtualDeviceDirection : std::uint8_t {
    Output, ///< A virtual output (`Audio/Sink`): apps play into it, a session captures it.
    Input, ///< A virtual microphone (`Audio/Source/Virtual`): a session plays into it, apps record from it.
};

enum class VirtualDeviceChannels : std::uint8_t {
    Mono,
    Stereo,
};

/// Whose device it is, and so which file holds it (ADR-0013).
enum class VirtualDeviceScope : std::uint8_t {
    User, ///< This user's, in `$XDG_CONFIG_HOME/pipewire/pipewire.conf.d/`.
    System, ///< Every user's, in `/etc/pipewire/pipewire.conf.d/`; written through the helper.
};

/// One virtual device, as the drop-in holds it.
struct VirtualDevice {
    /// Eight lower-case hex digits, unique across both scopes' files. The
    /// device's `node.name` is made from it and never changes.
    QString id;
    /// The user's name for it: the node's `node.description`.
    QString name;
    VirtualDeviceDirection direction = VirtualDeviceDirection::Output;
    VirtualDeviceChannels channels = VirtualDeviceChannels::Stereo;
    /// A disabled device stays in its file, behind a condition nothing
    /// satisfies, so that it keeps its id and its name.
    bool enabled = true;
    VirtualDeviceScope scope = VirtualDeviceScope::User;

    bool operator==(const VirtualDevice &) const = default;
};

/// Why a device list or a drop-in could not be read or written.
enum class VirtualDeviceError : std::uint8_t {
    EmptyName, ///< Nothing but spaces.
    NameTooLong, ///< More than kMaxVirtualDeviceNameLength characters.
    NameHasControlCharacter, ///< A newline, a tab or another control character, or broken UTF-16.
    NameNotTrimmed, ///< White space around a name in a list; validVirtualDeviceName() removes it.
    InvalidId, ///< Not eight lower-case hex digits.
    DuplicateId, ///< Two devices share an id.
    WrongScope, ///< A device of the other scope in this scope's list.
    TooManyDevices, ///< More than kMaxVirtualDevices in one file.
    NotOurs, ///< A file at the path that Reverblabr did not write. It is never overwritten.
    Malformed, ///< Our header, but the rest cannot be read. It is left as it is.
    FileNotReadable,
    FileNotWritable,
    NotFound, ///< No device has that id.
    ReadOnlyScope, ///< An all-users device, which only the privileged helper may change.
};

[[nodiscard]] std::string_view virtualDeviceErrorName(VirtualDeviceError error) noexcept;

/// The longest name a device may have, in characters (Unicode code points).
///
/// PipeWire has no limit of its own; this one keeps a name readable in a
/// volume applet's list, where a line is about forty characters wide, and in
/// Reverblabr's own device pickers.
inline constexpr qsizetype kMaxVirtualDeviceNameLength = 64;

/// The most devices one file may hold. Far more than anybody routes by hand;
/// it bounds what the privileged helper will write.
inline constexpr qsizetype kMaxVirtualDevices = 64;

/// The first line of every file Reverblabr writes. A file at the path without
/// it is somebody else's.
inline constexpr std::string_view kVirtualDeviceFileHeader = "# Managed by Reverblabr";

/// `node.name` is this prefix and the id. Deliberately not `reverblabr.`,
/// which names our own processing nodes (ADR-0006, ADR-0013).
inline constexpr std::string_view kVirtualDeviceNodePrefix = "reverblabr-virtual.";

/// The property that marks a node as one of these devices. Its value is
/// "user" or "system", the scope whose file it comes from.
inline constexpr std::string_view kVirtualDeviceMarker = "reverblabr.virtual-device";

/// The context property a disabled device's condition asks for. Nothing sets
/// it, so a disabled device is never created.
inline constexpr std::string_view kShowDisabledVirtualDevices = "reverblabr.show-disabled-virtual-devices";

/// The name the user typed, made ready to keep: surrounding white space
/// removed, then refused when it is empty, too long, or holds a control
/// character. Quotes, backslashes, braces and any other printable character,
/// in any script, are kept as they are; the file escapes them.
///
/// **This is the check the privileged helper makes too**, so it needs
/// nothing but the string.
[[nodiscard]] std::expected<QString, VirtualDeviceError> validVirtualDeviceName(const QString &name);

/// True for eight lower-case hex digits.
[[nodiscard]] bool isValidVirtualDeviceId(const QString &id) noexcept;

/// Checks a whole list for one file: every name valid and already trimmed,
/// every id valid and unique, every device of \a scope, and no more than
/// kMaxVirtualDevices of them.
[[nodiscard]] std::expected<void, VirtualDeviceError> validateVirtualDevices(const QList<VirtualDevice> &devices,
                                                                             VirtualDeviceScope scope);

/// A new id that none of \a taken uses. Taken from a random UUID.
[[nodiscard]] QString newVirtualDeviceId(const QList<VirtualDevice> &taken);

/// `reverblabr-virtual.<id>`.
[[nodiscard]] QString virtualDeviceNodeName(const QString &id);

/// `Audio/Sink` or `Audio/Source/Virtual`.
[[nodiscard]] QString virtualDeviceMediaClass(VirtualDeviceDirection direction);

/// "user" or "system", the marker property's value.
[[nodiscard]] QString virtualDeviceScopeName(VirtualDeviceScope scope);

/// The properties a device's live node is created with: the same ones as
/// its drop-in entry's `args`, so that a node made now and a node PipeWire
/// makes from the file at its next start are the same device. The caller
/// adds `object.linger`.
[[nodiscard]] QList<std::pair<QByteArray, QByteArray>> virtualDeviceNodeProperties(const VirtualDevice &device,
                                                                                   VirtualDeviceScope scope);

/// The drop-in for \a devices, whole: the header, then one `context.objects`
/// entry per device (ADR-0013). The list is not validated here; save() does.
[[nodiscard]] QByteArray virtualDevicesToConf(const QList<VirtualDevice> &devices, VirtualDeviceScope scope);

/// Reads a drop-in back. Text without the header is NotOurs; text with it
/// that does not hold what virtualDevicesToConf() writes is Malformed.
[[nodiscard]] std::expected<QList<VirtualDevice>, VirtualDeviceError> virtualDevicesFromConf(const QByteArray &text,
                                                                                             VirtualDeviceScope scope);

/// A list as the all-users helper receives it: one map per device,
/// with exactly the keys `id`, `name`, `direction` ("output" or "input"),
/// `channels` ("mono" or "stereo") and `enabled`. There is no scope: the
/// helper only ever writes the system file.
[[nodiscard]] QVariantList virtualDevicesToVariant(const QList<VirtualDevice> &devices);

/// Reads such a list back as devices of \a scope, **strictly**, because the
/// helper reads it as root from whoever asked: anything but a list of at most
/// kMaxVirtualDevices maps with exactly those keys, of those types and words,
/// is Malformed, and a name far longer than any valid one is NameTooLong
/// before anything else looks at it. The names are otherwise not judged
/// here; validateVirtualDevices() does that.
[[nodiscard]] std::expected<QList<VirtualDevice>, VirtualDeviceError>
virtualDevicesFromVariant(const QVariant &list, VirtualDeviceScope scope);

/// One scope's drop-in on the disk, and the reading and writing of it.
///
/// Saving is atomic, like SessionStore's: the bytes go to a temporary file
/// beside the real one, reach the disk, and only then take its place, so the
/// file is never half written. A half-written drop-in would make PipeWire
/// ignore every device in it (docs/dev/spike-virtual-devices.md §6).
class VirtualDeviceStore
{
public:
    /// The scope's own file (userFile() or systemFile()).
    explicit VirtualDeviceStore(VirtualDeviceScope scope);

    /// Another file, which is what tests and the helper's own tests do.
    VirtualDeviceStore(VirtualDeviceScope scope, QString path);

    /// `$XDG_CONFIG_HOME/pipewire/pipewire.conf.d/90-reverblabr-virtual-devices.conf`.
    [[nodiscard]] static QString userFile();

    /// `<root>/etc/pipewire/pipewire.conf.d/90-reverblabr-virtual-devices-system.conf`.
    /// \a root is `/` except in tests.
    [[nodiscard]] static QString systemFile(const QString &root = QStringLiteral("/"));

    [[nodiscard]] VirtualDeviceScope scope() const noexcept
    {
        return m_scope;
    }

    [[nodiscard]] const QString &path() const noexcept
    {
        return m_path;
    }

    /// The devices in the file. No file is no devices.
    [[nodiscard]] std::expected<QList<VirtualDevice>, VirtualDeviceError> load() const;

    /// Replaces the file with \a devices, making its directory when missing,
    /// or removes it when the list is empty.
    ///
    /// Refuses, and leaves the file byte for byte as it was, when the list is
    /// invalid or when the file there is not ours or cannot be read: a file
    /// we cannot read may hold devices the user still wants.
    [[nodiscard]] std::expected<void, VirtualDeviceError> save(const QList<VirtualDevice> &devices) const;

private:
    VirtualDeviceScope m_scope;
    QString m_path;
};

} // namespace rb::pw
