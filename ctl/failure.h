// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QJsonObject>
#include <QString>

#include <expected>

namespace rb::ctl
{

/// What `reverblabr-ctl` exits with. **These numbers are part of what a
/// script relies on**: docs/ctl.md lists them, and a test holds the two
/// together. Values are only ever added.
enum class ExitCode : int {
    Ok = 0,
    /// The engine was asked and said no, with an error code.
    Refused = 1,
    /// The command line was wrong: an unknown command, a missing argument,
    /// a value the setting cannot take.
    Usage = 2,
    /// No session, block or setting by that name, or more than one.
    NotFound = 3,
    /// No engine to ask: no session bus, or the engine is not installed or
    /// would not start.
    Unreachable = 4,
    /// The engine was asked and did not answer in time.
    Timeout = 5,
};

/// Why a command did not do what it was asked. The code is the engine's
/// own (`session.cannotRun`, …) when the engine refused, and one of the
/// tool's `ctl.*` codes otherwise; `args` carries what the message needs,
/// by name, as the engine's Error signal does.
struct Failure {
    ExitCode exit = ExitCode::Refused;
    QString code;
    QJsonObject args;
};

template <typename T>
using Result = std::expected<T, Failure>;

[[nodiscard]] inline std::unexpected<Failure> fail(ExitCode exit, QString code, QJsonObject args = {})
{
    return std::unexpected(Failure{exit, std::move(code), std::move(args)});
}

} // namespace rb::ctl
