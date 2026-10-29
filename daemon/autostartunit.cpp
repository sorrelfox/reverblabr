// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "autostartunit.h"

#include <QDBusError>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QStringList>

using namespace Qt::StringLiterals;

namespace rb::daemon
{

namespace
{

constexpr auto kService = "org.freedesktop.systemd1";
constexpr auto kPath = "/org/freedesktop/systemd1";
constexpr auto kInterface = "org.freedesktop.systemd1.Manager";

QDBusMessage managerCall(const char *method)
{
    return QDBusMessage::createMethodCall(QLatin1StringView(kService),
                                          QLatin1StringView(kPath),
                                          QLatin1StringView(kInterface),
                                          QLatin1StringView(method));
}

QString unitName()
{
    return QLatin1StringView(AutostartUnit::kUnitName);
}

/// What an error from the manager means for the unit.
AutostartUnit::State stateOf(const QDBusError &error)
{
    const QString name = error.name();
    // Nobody on the bus answers for the manager: no systemd user instance.
    if (error.type() == QDBusError::ServiceUnknown || name == "org.freedesktop.DBus.Error.NameHasNoOwner"_L1) {
        return AutostartUnit::State::NoManager;
    }
    // **No unit file by that name**: the manager maps ENOENT to FileNotFound
    // for a unit file it cannot find, and some calls say NoSuchUnit.
    if (name == "org.freedesktop.DBus.Error.FileNotFound"_L1 || name == "org.freedesktop.systemd1.NoSuchUnit"_L1) {
        return AutostartUnit::State::NotInstalled;
    }
    if (name == "org.freedesktop.systemd1.UnitMasked"_L1) {
        return AutostartUnit::State::Masked;
    }
    return AutostartUnit::State::Failed;
}

/// What `GetUnitFileState` says, for a unit whose file has an [Install]
/// section.
AutostartUnit::State stateOf(const QString &fileState)
{
    if (fileState == "enabled"_L1 || fileState == "enabled-runtime"_L1) {
        return AutostartUnit::State::Enabled;
    }
    if (fileState == "disabled"_L1) {
        return AutostartUnit::State::Disabled;
    }
    if (fileState == "masked"_L1 || fileState == "masked-runtime"_L1) {
        return AutostartUnit::State::Masked;
    }
    // static, linked, alias, generated, transient, bad: nothing this can
    // make "started at login" by enabling it.
    return AutostartUnit::State::Failed;
}

} // namespace

AutostartUnit::AutostartUnit(QDBusConnection bus, QObject *parent)
    : QObject(parent)
    , m_bus(std::move(bus))
{
}

AutostartUnit::State AutostartUnit::state() const noexcept
{
    return m_state;
}

QString AutostartUnit::detail() const
{
    return m_detail;
}

void AutostartUnit::refresh()
{
    QDBusMessage call = managerCall("GetUnitFileState");
    call.setArguments({unitName()});
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        const QDBusPendingReply<QString> reply = *finished;
        if (reply.isError()) {
            settle(stateOf(reply.error()), reply.error().name() + u": "_s + reply.error().message());
            return;
        }
        settle(stateOf(reply.value()), reply.value());
    });
}

void AutostartUnit::setEnabled(bool enabled)
{
    QDBusMessage call = managerCall(enabled ? "EnableUnitFiles" : "DisableUnitFiles");
    // (as files, b runtime[, b force]): for good, not only until the next
    // boot, and never over a unit file of somebody else's.
    if (enabled) {
        call.setArguments({QStringList{unitName()}, false, false});
    } else {
        call.setArguments({QStringList{unitName()}, false});
    }
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        const QDBusMessage reply = finished->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            const QDBusError error(reply);
            settle(stateOf(error), error.name() + u": "_s + error.message());
            return;
        }
        // **Then reload, as `systemctl enable` does**, so the manager's idea
        // of what login pulls in is the one on disk; and ask again, so the
        // state is the manager's answer rather than what was hoped for.
        auto *reloading = new QDBusPendingCallWatcher(m_bus.asyncCall(managerCall("Reload")), this);
        connect(reloading, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *done) {
            done->deleteLater();
            refresh();
        });
    });
}

void AutostartUnit::settle(State state, const QString &detail)
{
    m_detail = detail;
    if (state == m_state) {
        return;
    }
    m_state = state;
    Q_EMIT stateChanged(state);
}

} // namespace rb::daemon
