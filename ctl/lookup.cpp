// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lookup.h"

#include <QJsonDocument>

#include <cmath>
#include <optional>

using namespace Qt::StringLiterals;

namespace rb::ctl
{

std::vector<SessionEntry> sessionsFromJson(const QJsonArray &sessions)
{
    std::vector<SessionEntry> entries;
    for (const QJsonValue &value : sessions) {
        const QJsonObject session = value.toObject();
        const QString id = session.value("id"_L1).toString();
        if (id.isEmpty()) {
            continue;
        }
        entries.push_back({id, session.value("name"_L1).toString(), session.value("state"_L1).toString()});
    }
    return entries;
}

namespace
{

[[nodiscard]] QJsonArray idsOf(const auto &matches)
{
    QJsonArray ids;
    for (const auto *match : matches) {
        ids.append(match->id);
    }
    return ids;
}

} // namespace

Result<SessionEntry> findSession(const std::vector<SessionEntry> &sessions, const QString &wanted)
{
    for (const SessionEntry &session : sessions) {
        if (session.id == wanted) {
            return session;
        }
    }
    // **Exact names first, then ignoring case**, and in each round more than
    // one is a question for the person, never a guess: starting the wrong
    // one of two "Radio" sessions is worse than asking which.
    for (const Qt::CaseSensitivity sensitivity : {Qt::CaseSensitive, Qt::CaseInsensitive}) {
        std::vector<const SessionEntry *> matches;
        for (const SessionEntry &session : sessions) {
            if (session.name.compare(wanted, sensitivity) == 0) {
                matches.push_back(&session);
            }
        }
        if (matches.size() == 1) {
            return *matches.front();
        }
        if (matches.size() > 1) {
            return fail(ExitCode::NotFound,
                        u"ctl.sessionAmbiguous"_s,
                        {{"session"_L1, wanted}, {"matches"_L1, idsOf(matches)}});
        }
    }
    return fail(ExitCode::NotFound, u"ctl.sessionNotFound"_s, {{"session"_L1, wanted}});
}

std::vector<BlockEntry> blocksFromSnapshot(const QJsonObject &session)
{
    std::vector<BlockEntry> blocks;
    for (const QJsonValue &value : session.value("blocks"_L1).toArray()) {
        const QJsonObject block = value.toObject();
        blocks.push_back({block.value("id"_L1).toString(),
                          block.value("type"_L1).toString(),
                          block.value("nickname"_L1).toString()});
    }
    return blocks;
}

Result<BlockEntry> findBlock(const std::vector<BlockEntry> &blocks, const QString &wanted, const QString &type)
{
    std::vector<const BlockEntry *> candidates;
    for (const BlockEntry &block : blocks) {
        if (type.isEmpty() || block.type == type) {
            candidates.push_back(&block);
        }
    }
    const QJsonObject asked{{"block"_L1, wanted}, {"type"_L1, type}};
    if (wanted.isEmpty()) {
        if (candidates.size() == 1) {
            return *candidates.front();
        }
        return fail(ExitCode::NotFound,
                    candidates.empty() ? u"ctl.blockNotFound"_s : u"ctl.blockAmbiguous"_s,
                    {{"block"_L1, wanted}, {"type"_L1, type}, {"matches"_L1, idsOf(candidates)}});
    }
    for (const BlockEntry *block : candidates) {
        if (block->id == wanted) {
            return *block;
        }
    }
    for (const Qt::CaseSensitivity sensitivity : {Qt::CaseSensitive, Qt::CaseInsensitive}) {
        std::vector<const BlockEntry *> matches;
        for (const BlockEntry *block : candidates) {
            if (!block->nickname.isEmpty() && block->nickname.compare(wanted, sensitivity) == 0) {
                matches.push_back(block);
            }
        }
        if (matches.size() == 1) {
            return *matches.front();
        }
        if (matches.size() > 1) {
            QJsonObject args = asked;
            args.insert("matches"_L1, idsOf(matches));
            return fail(ExitCode::NotFound, u"ctl.blockAmbiguous"_s, args);
        }
    }
    return fail(ExitCode::NotFound, u"ctl.blockNotFound"_s, asked);
}

QJsonObject paramDescriptor(const QJsonObject &catalogue, const QString &type, const QString &param)
{
    for (const QJsonValue &entry : catalogue.value("blocks"_L1).toArray()) {
        const QJsonObject block = entry.toObject();
        if (block.value("type"_L1).toString() != type) {
            continue;
        }
        for (const QJsonValue &descriptor : block.value("params"_L1).toArray()) {
            if (descriptor.toObject().value("id"_L1).toString() == param) {
                return descriptor.toObject();
            }
        }
    }
    return {};
}

namespace
{

[[nodiscard]] std::unexpected<Failure> badValue(const QJsonObject &descriptor, const QString &typed)
{
    QJsonObject args{
        {"param"_L1, descriptor.value("id"_L1)}, {"value"_L1, typed}, {"type"_L1, descriptor.value("type"_L1)}};
    for (const auto key : {"minimum"_L1, "maximum"_L1, "choices"_L1}) {
        if (descriptor.contains(key)) {
            args.insert(key, descriptor.value(key));
        }
    }
    return fail(ExitCode::Usage, u"ctl.badValue"_s, args);
}

/// A number as a script writes one: the C locale's, whatever the person's
/// locale says, since "0,5" in one language is two numbers in another.
[[nodiscard]] std::optional<double> numberFrom(const QString &typed)
{
    bool ok = false;
    const double number = typed.trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(number)) {
        return std::nullopt;
    }
    return number;
}

} // namespace

Result<QJsonValue> valueForParam(const QJsonObject &descriptor, const QString &typed)
{
    if (descriptor.value("readOnly"_L1).toBool(false)) {
        return fail(ExitCode::Usage, u"ctl.readOnly"_s, {{"param"_L1, descriptor.value("id"_L1)}});
    }
    const QString type = descriptor.value("type"_L1).toString();
    if (type == "float"_L1 || type == "int"_L1) {
        const std::optional<double> number = numberFrom(typed);
        if (!number || (type == "int"_L1 && std::floor(*number) != *number)) {
            return badValue(descriptor, typed);
        }
        const QJsonValue minimum = descriptor.value("minimum"_L1);
        const QJsonValue maximum = descriptor.value("maximum"_L1);
        if ((minimum.isDouble() && *number < minimum.toDouble())
            || (maximum.isDouble() && *number > maximum.toDouble())) {
            return badValue(descriptor, typed);
        }
        return type == "int"_L1 ? QJsonValue(static_cast<qint64>(*number)) : QJsonValue(*number);
    }
    if (type == "bool"_L1) {
        const QString word = typed.trimmed().toLower();
        if (word == "true"_L1 || word == "on"_L1 || word == "yes"_L1 || word == "1"_L1) {
            return QJsonValue(true);
        }
        if (word == "false"_L1 || word == "off"_L1 || word == "no"_L1 || word == "0"_L1) {
            return QJsonValue(false);
        }
        return badValue(descriptor, typed);
    }
    if (type == "enum"_L1) {
        const QJsonArray choices = descriptor.value("choices"_L1).toArray();
        for (const Qt::CaseSensitivity sensitivity : {Qt::CaseSensitive, Qt::CaseInsensitive}) {
            for (const QJsonValue &choice : choices) {
                if (choice.toString().compare(typed, sensitivity) == 0) {
                    return QJsonValue(choice.toString());
                }
            }
        }
        return badValue(descriptor, typed);
    }
    return QJsonValue(typed);
}

QJsonValue untypedValue(const QString &typed)
{
    // Wrapped in an array, since a bare scalar is not a JSON document.
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray("[") + typed.toUtf8() + QByteArray("]"), &error);
    if (error.error == QJsonParseError::NoError && document.array().size() == 1) {
        const QJsonValue value = document.array().at(0);
        if (value.isDouble() || value.isBool() || value.isString()) {
            return value;
        }
    }
    return QJsonValue(typed);
}

} // namespace rb::ctl
