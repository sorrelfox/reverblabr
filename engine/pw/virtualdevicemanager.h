// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "pw/virtualdeviceconf.h"

#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QString>

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string_view>

namespace rb::pw
{

class Connection;
class RegistryMirror;

/// Where a device stands in the graph right now.
enum class VirtualDeviceStatus : std::uint8_t {
    Disabled, ///< Switched off: kept in its file, not in the graph.
    Present, ///< In the graph.
    Creating, ///< Asked for; PipeWire has not shown it yet.
    /// Should be in the graph and is not: PipeWire refused it, or something
    /// else removed it. It comes back the next time the devices are
    /// reconciled — at the next connection, a reload, or any change to the list.
    Missing,
    WaitingForPipeWire, ///< No connection, or the graph has not been read yet.
};

[[nodiscard]] std::string_view virtualDeviceStatusName(VirtualDeviceStatus status) noexcept;

/// One device, and where it stands.
struct VirtualDeviceState {
    VirtualDevice device;
    VirtualDeviceStatus status = VirtualDeviceStatus::WaitingForPipeWire;

    bool operator==(const VirtualDeviceState &) const = default;
};

/// The list as the facade sends it: one object per device with `id`, `name`,
/// `nodeName`, `direction` ("output" or "input"), `channels` ("mono" or
/// "stereo"), `enabled`, `scope` ("user" or "system") and `status`.
[[nodiscard]] QJsonArray virtualDevicesToJson(const QList<VirtualDeviceState> &devices);

/// Keeps the virtual devices' files and the live graph in step (ADR-0013).
///
/// **The files are the truth.** Each scope's drop-in holds its devices,
/// disabled ones too, and PipeWire makes the enabled ones whenever it
/// starts. This makes a change take effect at once as well: every change is
/// written first, and then the live node follows — created through the
/// `adapter` factory with `object.linger = true`, so it outlives the app,
/// or destroyed through the registry.
///
/// **Renaming creates before it destroys.** A node's description cannot be
/// changed from outside, so a rename makes a new node under the same
/// `node.name` and destroys the old one only once the new one has its
/// ports. Destroying first would send the apps playing into it to the
/// default device for a moment, which is heard (the spike, §4).
///
/// **Reconciling** — when the graph has been read, at every connection, and
/// after every change — creates each enabled device the graph lacks, and
/// destroys each marked node that no enabled entry in a readable file
/// accounts for. A scope whose file cannot be read is left alone, since it
/// may hold devices the user still wants. It never makes a second node with a
/// name already in use. A device that something else removes is **not**
/// made again behind the user's back: it shows as Missing until the next
/// reconcile.
///
/// **All-users devices are read-only here.** Their file belongs to the
/// privileged helper; the app has it write the file and then calls
/// reload(), and this makes the live side follow.
class VirtualDeviceManager : public QObject
{
    Q_OBJECT

public:
    /// Both must outlive the manager. The files are the user's own and
    /// `/etc`'s until setStores() says otherwise.
    VirtualDeviceManager(Connection &connection, RegistryMirror &mirror, QObject *parent = nullptr);
    ~VirtualDeviceManager() override;

    /// Other files, for tests. Reads them and reconciles.
    void setStores(VirtualDeviceStore user, VirtualDeviceStore system);
    [[nodiscard]] const VirtualDeviceStore &store(VirtualDeviceScope scope) const;

    /// Every device of both scopes, the user's first, each in file order.
    [[nodiscard]] QList<VirtualDeviceState> devices() const;
    [[nodiscard]] std::optional<VirtualDeviceState> device(const QString &id) const;

    /// Why a scope's file could not be read, if it could not. Its devices are
    /// then not listed, and nothing of that scope is changed in the graph.
    [[nodiscard]] std::optional<VirtualDeviceError> scopeError(VirtualDeviceScope scope) const;

    /// An id no device of either scope uses, for a new all-users device.
    [[nodiscard]] QString newId() const;

    /// A new device of the user's own, enabled. The name is trimmed first.
    [[nodiscard]] std::expected<VirtualDevice, VirtualDeviceError>
    create(const QString &name, VirtualDeviceDirection direction, VirtualDeviceChannels channels);
    [[nodiscard]] std::expected<void, VirtualDeviceError> rename(const QString &id, const QString &name);
    [[nodiscard]] std::expected<void, VirtualDeviceError> setEnabled(const QString &id, bool enabled);
    [[nodiscard]] std::expected<void, VirtualDeviceError> remove(const QString &id);

    /// Reads both files again and reconciles: for when one was changed
    /// behind the manager's back, which is what the helper does.
    void reload();

    /// Reconciles now. Everything above does it by itself; this is for a
    /// test that wants to see a pass happen.
    void reconcileNow();

Q_SIGNALS:
    /// The list, or where one of its devices stands, changed.
    void devicesChanged();
    /// PipeWire would not make a device's node. \a detail is for the log.
    void nodeRefused(const QString &id, const QString &detail);

public:
    /// The implementation. Named here only so that the file which defines it
    /// can reach it from PipeWire's C callbacks.
    class Private;

private:
    std::unique_ptr<Private> d;
};

} // namespace rb::pw
