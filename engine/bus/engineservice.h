// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QDBusConnection>
#include <QDBusVirtualObject>
#include <QSet>

#include <functional>
#include <memory>

namespace rb::core
{
class EngineFacade;
}

namespace rb::bus
{

/// Serves an engine on a bus (ADR-0017): the daemon's half of the bridge.
///
/// **Generic, not generated.** A method call named in the interface XML is
/// answered by calling the facade method of the same name, first letter
/// lowered, through Qt's meta-object system; every facade signal leaves as
/// the D-Bus signal of the same name, first letter raised. So a member added
/// to the facade and the XML is served with nothing written here, and one
/// that is only in one of them is refused rather than guessed at.
///
/// **Telemetry is served beside it, not through it** (ADR-0018): the
/// `io.github.reverblabr.Telemetry` interface on the same object hands a
/// window the memory a session's levels are published in, once, and from
/// then on nothing about levels is sent at all.
///
/// **It keeps count of who it is serving**: every unique bus name that has
/// called it and is still on the bus, and every session that is running.
/// The daemon exits once there is neither (isIdle()), so it does not stay
/// behind a window somebody closed on a stopped session.
class EngineService : public QDBusVirtualObject
{
    Q_OBJECT

public:
    /// Serves \a engine, which must outlive this, on \a connection. Call
    /// publish() to put it on the bus.
    EngineService(core::EngineFacade *engine, const QDBusConnection &connection, QObject *parent = nullptr);
    ~EngineService() override;

    /// Registers the object at kObjectPath and takes kServiceName. False
    /// when either is already somebody else's: a second daemon.
    [[nodiscard]] bool publish();

    /// **Where Share() gets a session's telemetry** (ADR-0018): \a share
    /// answers a read-only descriptor for the memory \a sessionId's
    /// snapshots are published in, which the service then owns, or -1. The
    /// engine's shareTelemetry(). Without one, every Share() fails.
    void setTelemetry(std::function<int(const QString &sessionId)> share);

    /// Nobody calling and nothing running.
    [[nodiscard]] bool isIdle() const;
    [[nodiscard]] int clientCount() const;
    [[nodiscard]] int runningCount() const;

    [[nodiscard]] QString introspect(const QString &path) const override;
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override;

Q_SIGNALS:
    /// isIdle() has changed.
    void idleChanged(bool idle);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::bus
