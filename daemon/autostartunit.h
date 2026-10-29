// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QString>

#include <cstdint>

namespace rb::daemon
{

/// Whether systemd starts the engine's user unit at login (ADR-0021):
/// asks the user's service manager on \a bus, and enables or disables the
/// unit through it, as `systemctl --user enable` would.
///
/// **Through the manager, never by writing its files**: the manager knows
/// where the unit is installed, what its `[Install]` section wants and what
/// a person has masked, and it reloads itself. On a desktop without a user
/// manager on the bus, or with the unit not installed (a build tree), it
/// says so and does nothing.
class AutostartUnit : public QObject
{
    Q_OBJECT

public:
    enum class State : std::uint8_t {
        /// Not asked yet, or the answer is on its way.
        Unknown,
        /// Started at login.
        Enabled,
        /// Installed and not started at login.
        Disabled,
        /// Masked by somebody: left alone, never unmasked from here.
        Masked,
        /// The manager knows no such unit: the engine is not installed.
        NotInstalled,
        /// No user service manager on this bus.
        NoManager,
        /// The manager answered with an error it should not have.
        Failed,
    };
    Q_ENUM(State)

    static constexpr auto kUnitName = "reverblabrd.service";

    explicit AutostartUnit(QDBusConnection bus, QObject *parent = nullptr);

    [[nodiscard]] State state() const noexcept;
    /// What the manager said last, for the log.
    [[nodiscard]] QString detail() const;

    /// Asks the manager what the unit's file state is.
    void refresh();
    /// Enables or disables the unit, then asks again.
    void setEnabled(bool enabled);

Q_SIGNALS:
    void stateChanged(rb::daemon::AutostartUnit::State state);

private:
    void settle(State state, const QString &detail);

    QDBusConnection m_bus;
    State m_state = State::Unknown;
    QString m_detail;
};

} // namespace rb::daemon
