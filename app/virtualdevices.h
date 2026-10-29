// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/enginefacade.h"

#include <QAbstractListModel>
#include <QLatin1StringView>
#include <QQmlEngine>
#include <QString>
#include <QVariantMap>
#include <QWindow>

#include <memory>

namespace rb::core
{
struct SystemDeviceChange;
}

namespace rb::ui
{

class SystemDeviceWriter;

/// The virtual devices, as the Virtual Devices window lists them
/// (ADR-0013): outputs apps play into and microphones apps record from.
///
/// **The engine owns them**; this is what it last said, one row a device,
/// and the calls that ask it for a change. A change is never shown before
/// the engine has made it: a row keeps its old name until the list that
/// carries the new one arrives, and a refused change leaves it as it was.
///
/// **Rows are updated in place.** A list the engine sends is matched to the
/// rows by id, so a device whose status changes is one changed row, not a
/// model reset — a reset would take the focus and the scroll position of the
/// window with it every time PipeWire made a node.
class VirtualDevices : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Whether the engine has said anything yet. Before it has, an empty
    /// list means "not known", not "none".
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    /// Whether a device asked for with create() has not been answered yet.
    Q_PROPERTY(bool creating READ creating NOTIFY pendingChanged)
    /// The scopes whose file could not be read, "user" and "system", each
    /// with the engine's reason. Their devices are not in the list.
    Q_PROPERTY(QVariantMap unreadable READ unreadable NOTIFY unreadableChanged)
    /// The longest name a device may have, in characters.
    Q_PROPERTY(int maximumNameLength READ maximumNameLength CONSTANT)
    /// Whether this build can make and change devices for all users:
    /// it was built with KAuth. Whether the helper is actually installed is
    /// only known by asking it.
    Q_PROPERTY(bool allUsersAvailable READ allUsersAvailable CONSTANT)
    /// Whether a change to an all-users device is waiting for the
    /// administrator's password, or for the helper. **One at a time**:
    /// another is refused until this one is answered.
    Q_PROPERTY(bool authorizing READ authorizing NOTIFY authorizingChanged)
    /// The window the password prompt belongs to: the Virtual Devices
    /// window sets itself.
    Q_PROPERTY(QWindow *promptWindow READ promptWindow WRITE setPromptWindow NOTIFY promptWindowChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        /// The `node.name` sessions and other applications know it by.
        NodeNameRole,
        /// "output" or "input".
        DirectionRole,
        /// "mono" or "stereo".
        ChannelsRole,
        EnabledRole,
        /// "user" or "system". A system device is every user's, and only an
        /// administrator may change it.
        ScopeRole,
        /// "disabled", "present", "creating", "missing" or
        /// "waitingForPipeWire".
        StatusRole,
        /// A change to it has been asked for and not answered yet.
        PendingRole,
    };
    Q_ENUM(Role)

    explicit VirtualDevices(QObject *parent = nullptr);
    ~VirtualDevices() override;

    static VirtualDevices *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] bool ready() const;
    [[nodiscard]] int count() const;
    [[nodiscard]] bool creating() const;
    [[nodiscard]] QVariantMap unreadable() const;
    [[nodiscard]] int maximumNameLength() const;

    /// The reason a change to an all-users device is refused with while
    /// another is still waiting for the password.
    static constexpr QLatin1StringView kAuthorizationPending{"AuthorizationPending"};

    /// One of the user's own, enabled. \a direction is "output" or "input",
    /// \a channels "mono" or "stereo".
    Q_INVOKABLE void create(const QString &name, const QString &direction, const QString &channels);
    /// One for every user of the computer, enabled, through the helper and
    /// the administrator's password. Answered like create(): with
    /// created() once the engine lists it, or with refused() — which is
    /// also what a cancelled or refused password prompt gives.
    Q_INVOKABLE void createForAllUsers(const QString &name, const QString &direction, const QString &channels);
    /// These three go through the helper and the password when the device
    /// is an all-users one, and straight to the engine when it is the
    /// user's own; the window need not tell them apart.
    Q_INVOKABLE void rename(const QString &deviceId, const QString &name);
    Q_INVOKABLE void setEnabled(const QString &deviceId, bool enabled);
    Q_INVOKABLE void remove(const QString &deviceId);
    /// Asks the engine for the list again.
    Q_INVOKABLE void refresh();
    /// Has the engine read both files again: for after the all-users helper
    /// has written one.
    Q_INVOKABLE void reload();

    /// The row \a deviceId is in, or -1.
    [[nodiscard]] Q_INVOKABLE int indexOf(const QString &deviceId) const;
    /// One row as a map of its roles, by their names; empty for no such row.
    [[nodiscard]] Q_INVOKABLE QVariantMap get(int row) const;
    /// Words for one of the engine's reasons, for a file that could not be
    /// read.
    [[nodiscard]] Q_INVOKABLE QString reasonText(const QString &reason) const;

    [[nodiscard]] bool allUsersAvailable() const;
    [[nodiscard]] bool authorizing() const;
    [[nodiscard]] QWindow *promptWindow() const;
    void setPromptWindow(QWindow *window);

    /// The facade to talk to. A test gives its own; the singleton takes
    /// AppShell's.
    void attach(core::EngineFacade *facade);

    /// What asks the all-users helper, for a test to give its own runner.
    [[nodiscard]] SystemDeviceWriter *systemWriter() const;

Q_SIGNALS:
    void readyChanged();
    void countChanged();
    void pendingChanged();
    void unreadableChanged();
    void authorizingChanged();
    void promptWindowChanged();
    /// The device create() or createForAllUsers() asked for exists, under
    /// \a deviceId.
    void created(const QString &deviceId);
    /// A change was refused and nothing changed, or PipeWire would not make
    /// a device's node. \a code is the engine's, \a reason its word for why
    /// (empty for a node refusal), and \a message the sentence for a person.
    /// **The window shows this itself**: it is about the window's own list.
    void refused(const QString &code, const QString &reason, const QString &message);

private:
    /// An all-users change: worked out, then sent to the helper.
    void changeForAllUsers(const core::SystemDeviceChange &change);
    /// The helper's answer to it: empty when the file was written.
    void answerForAllUsers(const QString &reason);
    /// Refuses a change with \a reason, in the engine's own terms, so the
    /// window shows it like any other refusal: a create's refusal, or one
    /// about an existing device.
    void refuse(bool creating, const QString &deviceName, const QString &reason);

    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::ui
