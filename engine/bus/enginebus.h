// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QLatin1StringView>
#include <QMetaMethod>
#include <QString>
#include <QVariantList>

#include <vector>

class QObject;

/// The engine on the session bus (ADR-0017): the names both ends agree on,
/// and the arithmetic that turns a facade member into a D-Bus one and back.
namespace rb::bus
{

/// The well-known name the daemon takes, and D-Bus activation starts it by.
inline constexpr QLatin1StringView kServiceName{"io.github.reverblabr.Engine"};
/// Where the engine object is, and the interface it serves: the XML's,
/// whose name carries its version (ADR-0020). The bus name and the path
/// stay unversioned: there is one engine.
inline constexpr QLatin1StringView kObjectPath{"/io/github/reverblabr/Engine"};
inline constexpr QLatin1StringView kInterfaceName{"io.github.reverblabr.Engine1"};
/// Where a window asks for a session's telemetry (ADR-0018), on the same
/// object: `dbus/io.github.reverblabr.Telemetry.xml`. Not the engine's API.
inline constexpr QLatin1StringView kTelemetryInterfaceName{"io.github.reverblabr.Telemetry"};
/// What Share() fails with when no session of that id is open.
inline constexpr QLatin1StringView kNoTelemetryError{"io.github.reverblabr.Error.NoTelemetry"};

/// `CreateSession` on the bus is `createSession` in the facade, and back.
/// The rule the facade's conformance test already holds it to.
[[nodiscard]] QString facadeNameOf(const QString &busName);
[[nodiscard]] QString busNameOf(const QByteArray &facadeName);

/// The D-Bus signature a call of \a method carries: its parameters' types
/// in order, "ss" for two strings. Empty when one of them is not a type
/// D-Bus carries (the conformance test keeps that from happening).
[[nodiscard]] QString signatureOf(const QMetaMethod &method);

/// \a arguments, as they arrived from the bus or are about to leave, turned
/// into exactly the types \a method takes; false when there is a different
/// number of them or one will not convert.
[[nodiscard]] bool convertArguments(const QMetaMethod &method, QVariantList &arguments);

/// Calls or emits \a method on \a object with \a arguments, which must
/// already be of its parameter types (convertArguments()). Direct: the
/// method runs, or the signal is emitted, before this returns.
void invoke(QObject *object, const QMetaMethod &method, QVariantList &arguments);

/// The interface's own XML, as introspection hands it out: the
/// `<interface>` element of `dbus/io.github.reverblabr.Engine1.xml`.
[[nodiscard]] QString interfaceXml();

/// The same for the telemetry interface.
[[nodiscard]] QString telemetryInterfaceXml();

} // namespace rb::bus
