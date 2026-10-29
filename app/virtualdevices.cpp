// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "virtualdevices.h"

#include "appshell.h"
#include "core/virtualdevices.h"
#include "errormessages.h"
#include "systemdevicewriter.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QPointer>

#include <algorithm>
#include <deque>
#include <optional>
#include <utility>

using namespace Qt::StringLiterals;

namespace rb::ui
{

namespace
{

/// One device as the engine last described it.
struct Row {
    QString id;
    QString name;
    QString nodeName;
    QString direction;
    QString channels;
    bool enabled = true;
    QString scope;
    QString status;

    bool operator==(const Row &) const = default;
};

[[nodiscard]] Row rowFrom(const QJsonObject &object)
{
    Row row;
    row.id = object.value("id"_L1).toString();
    row.name = object.value("name"_L1).toString();
    row.nodeName = object.value("nodeName"_L1).toString();
    row.direction = object.value("direction"_L1).toString();
    row.channels = object.value("channels"_L1).toString();
    row.enabled = object.value("enabled"_L1).toBool(true);
    row.scope = object.value("scope"_L1).toString();
    row.status = object.value("status"_L1).toString();
    return row;
}

/// A call made and not answered yet. **The engine answers every call once**,
/// with the list or with a refusal, and in the order it was asked, so
/// the oldest outstanding call is the one an answer belongs to.
struct Asked {
    /// Empty for a create, which has no id yet.
    QString deviceId;
    /// A reload asked for on nobody's behalf, after the helper refused a
    /// list that no longer matched the file: nothing is pending on it.
    bool reloadOnly = false;
};

/// A change to an all-users device that is out with the helper.
struct Authorizing {
    core::SystemDeviceChange::Kind kind = core::SystemDeviceChange::Kind::Create;
    /// The device: a create's new id.
    QString deviceId;
    /// Its name, for a message about it.
    QString deviceName;
};

} // namespace

class VirtualDevices::Private
{
public:
    QPointer<core::EngineFacade> facade;
    QList<Row> rows;
    bool ready = false;
    QVariantMap unreadable;
    std::deque<Asked> asked;
    /// The engine's last VirtualDevicesChanged, which an all-users change
    /// is worked out from.
    QString lastMessage;
    SystemDeviceWriter writer;
    QPointer<QWindow> promptWindow;
    std::optional<Authorizing> authorizing;
    /// A device created for all users, until the engine lists it.
    QString awaitedCreation;

    [[nodiscard]] bool isPending(const QString &deviceId) const
    {
        if (authorizing && !deviceId.isEmpty() && authorizing->deviceId == deviceId) {
            return true;
        }
        return std::ranges::any_of(
            asked, [&deviceId](const Asked &call) { return !call.reloadOnly && call.deviceId == deviceId; });
    }
};

VirtualDevices::VirtualDevices(QObject *parent)
    : QAbstractListModel(parent)
    , d(std::make_unique<Private>())
{
    connect(
        &d->writer, &SystemDeviceWriter::finished, this, [this](const QString &reason) { answerForAllUsers(reason); });
    // Attached here rather than only in create(), because attaching AppShell
    // only there once left Run doing nothing.
    attach(AppShell::engine());
}

VirtualDevices::~VirtualDevices() = default;

VirtualDevices *VirtualDevices::create(QQmlEngine *, QJSEngine *)
{
    return new VirtualDevices;
}

void VirtualDevices::attach(core::EngineFacade *facade)
{
    if (d->facade) {
        d->facade->disconnect(this);
    }
    d->facade = facade;
    d->asked.clear();
    if (!d->facade) {
        return;
    }

    connect(d->facade, &core::EngineFacade::virtualDevicesChanged, this, [this](const QString &json) {
        d->lastMessage = json;
        const QJsonObject message = QJsonDocument::fromJson(json.toUtf8()).object();
        QList<Row> listed;
        for (const QJsonValue &entry : message.value("devices"_L1).toArray()) {
            Row row = rowFrom(entry.toObject());
            if (!row.id.isEmpty()) {
                listed.append(row);
            }
        }

        // The answer to the oldest call, if one is waiting. A list the
        // engine sent of its own accord — a node PipeWire has made — may
        // take the place of an answer still on its way; the cost is a
        // pending mark cleared a moment early, never a wrong row.
        QString answered;
        const bool wasCreating = creating();
        if (!d->asked.empty()) {
            answered = d->asked.front().deviceId;
            d->asked.pop_front();
        }

        const int before = static_cast<int>(d->rows.size());
        // Gone first, from the end so that the rows still to look at keep
        // their numbers.
        for (int row = before - 1; row >= 0; --row) {
            const QString id = d->rows.at(row).id;
            if (std::ranges::none_of(listed, [&id](const Row &device) { return device.id == id; })) {
                beginRemoveRows({}, row, row);
                d->rows.removeAt(row);
                endRemoveRows();
            }
        }
        // Then each listed device into its place: already there, moved from
        // further down, or new.
        for (int row = 0; row < listed.size(); ++row) {
            const Row &device = listed.at(row);
            int found = -1;
            for (int at = row; at < d->rows.size(); ++at) {
                if (d->rows.at(at).id == device.id) {
                    found = at;
                    break;
                }
            }
            if (found < 0) {
                beginInsertRows({}, row, row);
                d->rows.insert(row, device);
                endInsertRows();
                continue;
            }
            if (found != row) {
                beginMoveRows({}, found, found, {}, row);
                d->rows.move(found, row);
                endMoveRows();
            }
            if (d->rows.at(row) != device) {
                d->rows[row] = device;
                Q_EMIT dataChanged(index(row), index(row));
            }
        }
        // Whatever the answer was about is not pending any more.
        if (const int row = indexOf(answered); row >= 0) {
            Q_EMIT dataChanged(index(row), index(row), {PendingRole});
        }
        if (wasCreating != creating() || !answered.isEmpty()) {
            Q_EMIT pendingChanged();
        }
        if (d->rows.size() != before) {
            Q_EMIT countChanged();
        }

        const QVariantMap unreadable = message.value("unreadable"_L1).toObject().toVariantMap();
        if (unreadable != d->unreadable) {
            d->unreadable = unreadable;
            Q_EMIT unreadableChanged();
        }
        if (!d->ready) {
            d->ready = true;
            Q_EMIT readyChanged();
        }
        // A device the helper made for all users is there once the engine
        // has read the file again and lists it.
        if (!d->awaitedCreation.isEmpty() && indexOf(d->awaitedCreation) >= 0) {
            Q_EMIT created(std::exchange(d->awaitedCreation, QString()));
        }
    });

    connect(d->facade, &core::EngineFacade::virtualDeviceCreated, this, [this](const QString &deviceId) {
        Q_EMIT created(deviceId);
    });

    connect(d->facade, &core::EngineFacade::error, this, [this](const QString &code, const QString &argsJson) {
        const bool answersACall = code == "device.virtualNotCreated"_L1 || code == "device.virtualRefused"_L1;
        if (!answersACall && code != "device.virtualNodeRefused"_L1) {
            return;
        }
        const QJsonObject args = QJsonDocument::fromJson(argsJson.toUtf8()).object();
        if (answersACall && !d->asked.empty()) {
            const QString answered = d->asked.front().deviceId;
            d->asked.pop_front();
            if (const int row = indexOf(answered); row >= 0) {
                Q_EMIT dataChanged(index(row), index(row), {PendingRole});
            }
            Q_EMIT pendingChanged();
        }
        Q_EMIT refused(code, args.value("reason"_L1).toString(), app::errorMessage(code, args.toVariantMap()));
    });

    // Asked after this returns, never during it, for the reason the presets
    // footer once logged a binding loop: a binding that reads the list must
    // not see it change while it is being worked out.
    QMetaObject::invokeMethod(d->facade, [facade = d->facade] { facade->listVirtualDevices(); }, Qt::QueuedConnection);
}

int VirtualDevices::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(d->rows.size());
}

QVariant VirtualDevices::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const Row &row = d->rows.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return row.name;
    case IdRole:
        return row.id;
    case NodeNameRole:
        return row.nodeName;
    case DirectionRole:
        return row.direction;
    case ChannelsRole:
        return row.channels;
    case EnabledRole:
        return row.enabled;
    case ScopeRole:
        return row.scope;
    case StatusRole:
        return row.status;
    case PendingRole:
        return d->isPending(row.id);
    default:
        return {};
    }
}

QHash<int, QByteArray> VirtualDevices::roleNames() const
{
    return {
        {IdRole, "deviceId"},
        {NameRole, "name"},
        {NodeNameRole, "nodeName"},
        {DirectionRole, "direction"},
        {ChannelsRole, "channels"},
        {EnabledRole, "enabled"},
        {ScopeRole, "scope"},
        {StatusRole, "status"},
        {PendingRole, "pending"},
    };
}

bool VirtualDevices::ready() const
{
    return d->ready;
}

int VirtualDevices::count() const
{
    return static_cast<int>(d->rows.size());
}

bool VirtualDevices::creating() const
{
    return d->isPending(QString())
        || (d->authorizing && d->authorizing->kind == core::SystemDeviceChange::Kind::Create);
}

QVariantMap VirtualDevices::unreadable() const
{
    return d->unreadable;
}

int VirtualDevices::maximumNameLength() const
{
    return core::kVirtualDeviceNameLimit;
}

void VirtualDevices::create(const QString &name, const QString &direction, const QString &channels)
{
    if (!d->facade) {
        return;
    }
    d->asked.push_back(Asked{});
    Q_EMIT pendingChanged();
    const QJsonObject request{{"name"_L1, name}, {"direction"_L1, direction}, {"channels"_L1, channels}};
    d->facade->createVirtualDevice(QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)));
}

void VirtualDevices::createForAllUsers(const QString &name, const QString &direction, const QString &channels)
{
    const std::expected<core::VirtualDeviceRequest, QString> request =
        core::virtualDeviceRequestFromJson(QString::fromUtf8(
            QJsonDocument(QJsonObject{{"name"_L1, name}, {"direction"_L1, direction}, {"channels"_L1, channels}})
                .toJson(QJsonDocument::Compact)));
    if (!request) {
        refuse(true, name, request.error());
        return;
    }
    core::SystemDeviceChange change;
    change.kind = core::SystemDeviceChange::Kind::Create;
    change.name = name;
    change.direction = request->direction;
    change.channels = request->channels;
    changeForAllUsers(change);
}

void VirtualDevices::rename(const QString &deviceId, const QString &name)
{
    if (const int row = indexOf(deviceId); row >= 0 && d->rows.at(row).scope == "system"_L1) {
        core::SystemDeviceChange change;
        change.kind = core::SystemDeviceChange::Kind::Rename;
        change.deviceId = deviceId;
        change.name = name;
        changeForAllUsers(change);
        return;
    }
    if (!d->facade) {
        return;
    }
    d->asked.push_back(Asked{deviceId});
    if (const int row = indexOf(deviceId); row >= 0) {
        Q_EMIT dataChanged(index(row), index(row), {PendingRole});
    }
    Q_EMIT pendingChanged();
    d->facade->renameVirtualDevice(deviceId, name);
}

void VirtualDevices::setEnabled(const QString &deviceId, bool enabled)
{
    if (const int row = indexOf(deviceId); row >= 0 && d->rows.at(row).scope == "system"_L1) {
        core::SystemDeviceChange change;
        change.kind = core::SystemDeviceChange::Kind::SetEnabled;
        change.deviceId = deviceId;
        change.enabled = enabled;
        changeForAllUsers(change);
        return;
    }
    if (!d->facade) {
        return;
    }
    d->asked.push_back(Asked{deviceId});
    if (const int row = indexOf(deviceId); row >= 0) {
        Q_EMIT dataChanged(index(row), index(row), {PendingRole});
    }
    Q_EMIT pendingChanged();
    d->facade->setVirtualDeviceEnabled(deviceId, enabled);
}

void VirtualDevices::remove(const QString &deviceId)
{
    if (const int row = indexOf(deviceId); row >= 0 && d->rows.at(row).scope == "system"_L1) {
        core::SystemDeviceChange change;
        change.kind = core::SystemDeviceChange::Kind::Remove;
        change.deviceId = deviceId;
        changeForAllUsers(change);
        return;
    }
    if (!d->facade) {
        return;
    }
    d->asked.push_back(Asked{deviceId});
    if (const int row = indexOf(deviceId); row >= 0) {
        Q_EMIT dataChanged(index(row), index(row), {PendingRole});
    }
    Q_EMIT pendingChanged();
    d->facade->deleteVirtualDevice(deviceId);
}

void VirtualDevices::refresh()
{
    if (d->facade) {
        d->facade->listVirtualDevices();
    }
}

void VirtualDevices::reload()
{
    if (d->facade) {
        d->facade->reloadVirtualDevices();
    }
}

int VirtualDevices::indexOf(const QString &deviceId) const
{
    if (deviceId.isEmpty()) {
        return -1;
    }
    for (int row = 0; row < d->rows.size(); ++row) {
        if (d->rows.at(row).id == deviceId) {
            return row;
        }
    }
    return -1;
}

QVariantMap VirtualDevices::get(int row) const
{
    if (row < 0 || row >= d->rows.size()) {
        return {};
    }
    QVariantMap map;
    const QHash<int, QByteArray> names = roleNames();
    for (auto name = names.cbegin(); name != names.cend(); ++name) {
        map.insert(QString::fromLatin1(name.value()), data(index(row), name.key()));
    }
    return map;
}

QString VirtualDevices::reasonText(const QString &reason) const
{
    return app::reasonMessage(reason);
}

bool VirtualDevices::allUsersAvailable() const
{
    return SystemDeviceWriter::available();
}

bool VirtualDevices::authorizing() const
{
    return d->authorizing.has_value();
}

QWindow *VirtualDevices::promptWindow() const
{
    return d->promptWindow;
}

void VirtualDevices::setPromptWindow(QWindow *window)
{
    if (d->promptWindow == window) {
        return;
    }
    d->promptWindow = window;
    d->writer.setPromptParent(window);
    Q_EMIT promptWindowChanged();
}

SystemDeviceWriter *VirtualDevices::systemWriter() const
{
    return &d->writer;
}

void VirtualDevices::refuse(bool creating, const QString &deviceName, const QString &reason)
{
    const QString code = creating ? u"device.virtualNotCreated"_s : u"device.virtualRefused"_s;
    const QVariantMap args{{creating ? u"name"_s : u"device"_s, deviceName}, {u"reason"_s, reason}};
    Q_EMIT refused(code, reason, app::errorMessage(code, args));
}

void VirtualDevices::changeForAllUsers(const core::SystemDeviceChange &change)
{
    const QString deviceName = [&] {
        if (change.kind == core::SystemDeviceChange::Kind::Create) {
            return change.name;
        }
        const int row = indexOf(change.deviceId);
        return row >= 0 ? d->rows.at(row).name : change.deviceId;
    }();
    if (!d->facade) {
        return;
    }
    if (d->authorizing) {
        refuse(change.kind == core::SystemDeviceChange::Kind::Create, deviceName, QString(kAuthorizationPending));
        return;
    }
    const std::expected<core::SystemDeviceWrite, QString> write = core::planSystemDeviceChange(d->lastMessage, change);
    if (!write) {
        refuse(change.kind == core::SystemDeviceChange::Kind::Create, deviceName, write.error());
        return;
    }

    // A refusal names the device the way the list shows it now: a
    // rename's old name, a new device's trimmed one.
    const bool creating = change.kind == core::SystemDeviceChange::Kind::Create;
    if (d->writer.busy()) {
        refuse(creating, deviceName, QString(kAuthorizationPending));
        return;
    }
    d->authorizing = Authorizing{change.kind, write->deviceId, creating ? write->deviceName : deviceName};
    if (const int row = indexOf(write->deviceId); row >= 0) {
        Q_EMIT dataChanged(index(row), index(row), {PendingRole});
    }
    Q_EMIT authorizingChanged();
    Q_EMIT pendingChanged();
    // Last: a stand-in helper may answer before this returns.
    d->writer.write(write->helperArguments());
}

void VirtualDevices::answerForAllUsers(const QString &reason)
{
    if (!d->authorizing) {
        return;
    }
    const Authorizing answered = *std::exchange(d->authorizing, std::nullopt);
    const bool creating = answered.kind == core::SystemDeviceChange::Kind::Create;

    if (reason.isEmpty() && d->facade) {
        // Written. The engine reads the file again and makes the live node;
        // the change stays pending until its list arrives, like any other.
        d->asked.push_back(Asked{creating ? QString() : answered.deviceId});
        if (creating) {
            d->awaitedCreation = answered.deviceId;
        }
        d->facade->reloadVirtualDevices();
    } else if (!reason.isEmpty()) {
        // Somebody else changed the all-users devices meanwhile: show what
        // the file holds now, so that the next try starts from it.
        if (reason == core::kVirtualDeviceChangedElsewhere && d->facade) {
            d->asked.push_back(Asked{QString(), true});
            d->facade->reloadVirtualDevices();
        }
        refuse(creating, answered.deviceName, reason);
    }

    if (const int row = indexOf(answered.deviceId); row >= 0) {
        Q_EMIT dataChanged(index(row), index(row), {PendingRole});
    }
    Q_EMIT authorizingChanged();
    Q_EMIT pendingChanged();
}

} // namespace rb::ui
