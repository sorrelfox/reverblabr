// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "failure.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <vector>

namespace rb::ctl
{

/// A session as `SessionsListed` gives it.
struct SessionEntry {
    QString id;
    QString name;
    /// An open session's state, or `closed` for one only on disk.
    QString state;
};

/// The entries of a `SessionsListed` payload; anything that is not an
/// object with an id is left out.
[[nodiscard]] std::vector<SessionEntry> sessionsFromJson(const QJsonArray &sessions);

/// The session a person meant by `wanted`: its id, else its exact name, else
/// the one name that matches ignoring case. More than one match is refused
/// rather than guessed, with the ids that matched.
[[nodiscard]] Result<SessionEntry> findSession(const std::vector<SessionEntry> &sessions, const QString &wanted);

/// A block as a session's snapshot gives it.
struct BlockEntry {
    QString id;
    QString type;
    /// The name a person gave it, if any.
    QString nickname;
};

[[nodiscard]] std::vector<BlockEntry> blocksFromSnapshot(const QJsonObject &session);

/// The block a person meant: its id, else its exact nickname, else the one
/// nickname that matches ignoring case. With `wanted` empty and `type`
/// given, the only block of that type. `type`, when given, also limits the
/// search to blocks of that type.
[[nodiscard]] Result<BlockEntry>
findBlock(const std::vector<BlockEntry> &blocks, const QString &wanted, const QString &type = {});

/// The value a setting is sent as, from what was typed, checked against the
/// setting's descriptor in the block catalogue: a number in range for a
/// float or int, true or false for a bool (also on/off, yes/no, 1/0), one of
/// the choices for an enum, and the text as it is for a string.
[[nodiscard]] Result<QJsonValue> valueForParam(const QJsonObject &descriptor, const QString &typed);

/// A setting's descriptor from the catalogue, by the block's type and the
/// setting's id; an empty object when the catalogue has no such setting.
[[nodiscard]] QJsonObject paramDescriptor(const QJsonObject &catalogue, const QString &type, const QString &param);

/// The value of a setting the catalogue does not describe (a hosted
/// plugin's own): JSON if it reads as a number, true, false or a JSON
/// string, otherwise the text itself.
[[nodiscard]] QJsonValue untypedValue(const QString &typed);

} // namespace rb::ctl
