// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "failure.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace rb::ctl
{

class EngineClient;

/// What a command did, said twice: `json` for a script (stable, never
/// translated; docs/ctl.md), `lines` for a person (translated).
struct Outcome {
    QJsonObject json;
    QStringList lines;
};

[[nodiscard]] Result<Outcome> listCommand(EngineClient &engine);
/// Every session, or the one named.
[[nodiscard]] Result<Outcome> statusCommand(EngineClient &engine, const QString &session);
[[nodiscard]] Result<Outcome> startCommand(EngineClient &engine, const QString &session);
[[nodiscard]] Result<Outcome> stopCommand(EngineClient &engine, const QString &session);
/// Every Recorder of a running session, or the one named.
[[nodiscard]] Result<Outcome> splitCommand(EngineClient &engine, const QString &session, const QString &block);
/// An Input Switch to A or B; the block may be left out when the session
/// has only one.
[[nodiscard]] Result<Outcome>
switchCommand(EngineClient &engine, const QString &session, const QString &block, const QString &input);
[[nodiscard]] Result<Outcome> setCommand(EngineClient &engine,
                                         const QString &session,
                                         const QString &block,
                                         const QString &param,
                                         const QString &value);

/// Every session's timers, or one session's.
[[nodiscard]] Result<Outcome> timersCommand(EngineClient &engine, const QString &session);
/// What a timer does when its time comes: what its systemd timer runs.
[[nodiscard]] Result<Outcome> timerFireCommand(EngineClient &engine, const QString &timerId);

/// A failure for a person, in their language.
[[nodiscard]] QString describe(const Failure &failure);

/// A session's state for a person: "running", "stopped", …
[[nodiscard]] QString stateWord(const QString &state);

} // namespace rb::ctl
