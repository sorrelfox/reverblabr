// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/session.h"

#include <QJsonObject>
#include <QString>

namespace rb::core
{

/// One session as `SessionsListed` gives it: `{id, name, state, sources,
/// recordings, startedAt}`.
///
/// **Enough to draw a row of the Sessions window without opening
/// anything.** Opening a session to see what it captures would put it in a
/// tab and start watching its devices; the engine has the file in hand
/// already, because listing reads every one.
/// - `sources`: the blocks sound comes in through, `[{type, name}]` in the
///   order they sit in the session. The name is what somebody called the
///   block, or empty.
/// - `recordings`: how many recordings the session has made, as the Files
///   tab would list them.
/// - `startedAt`: when a running session was started, in milliseconds since
///   the epoch. Absent for one that is not running.
///
/// **Both engines build it here**, so the mock and the real one cannot
/// disagree about what a row holds.
[[nodiscard]] QJsonObject
sessionListEntry(const Session &session, const QString &state, int recordings, qint64 startedAtMs);

} // namespace rb::core
