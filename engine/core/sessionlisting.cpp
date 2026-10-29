// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/sessionlisting.h"

#include "core/timers.h"

#include <QJsonArray>
#include <QUuid>

using namespace Qt::StringLiterals;

namespace rb::core
{

QJsonObject sessionListEntry(const Session &session, const QString &state, int recordings, qint64 startedAtMs)
{
    // **A source is a block whose type says so**: the category is the first
    // part of every type id, stable for ever (blockinfo.h), so this needs no
    // block made to ask.
    QJsonArray sources;
    for (const BlockInstance &block : session.blocks) {
        if (block.type.startsWith("source."_L1)) {
            sources.append(QJsonObject{{"type"_L1, block.type}, {"name"_L1, block.nickname}});
        }
    }
    // **Only the timers this version can run**, as it writes them: a list
    // is what the background engine makes its systemd timers from.
    QJsonArray timers;
    for (const Timer &timer : timersFromJson(session.timers)) {
        timers.append(timerToJson(timer));
    }
    QJsonObject entry{
        {"id"_L1, session.id.toString(QUuid::WithoutBraces)},
        {"name"_L1, session.name},
        {"state"_L1, state},
        {"sources"_L1, sources},
        {"recordings"_L1, recordings},
        {"autoRun"_L1, session.settings.autoRun},
        {"timers"_L1, timers},
    };
    if (startedAtMs > 0) {
        entry.insert("startedAt"_L1, static_cast<double>(startedAtMs));
    }
    return entry;
}

} // namespace rb::core
