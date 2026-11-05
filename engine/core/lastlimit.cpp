// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/lastlimit.h"

#include "blocks/joinlr/joinlr.h"
#include "blocks/outputdevice/outputdevice.h"
#include "dsp/ear.h"

#include <QHash>
#include <QJsonValue>
#include <QList>
#include <QSet>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>

using namespace Qt::StringLiterals;

namespace rb::core
{

namespace
{

constexpr std::uint32_t kLeft = 0;
constexpr std::uint32_t kRight = 1;

/// The input a Join L/R's output comes from: its left side is the Left
/// lane's left port and its right side the Right lane's right one.
[[nodiscard]] std::uint32_t inputOf(const BlockInstance &block, std::uint32_t side)
{
    if (block.type == QLatin1StringView{blocks::JoinLR::kTypeId}) {
        return side == kLeft ? 0 : 3;
    }
    return side;
}

[[nodiscard]] QList<const Wire *> wiresInto(const Session &session, const QString &blockId, std::uint32_t port)
{
    QList<const Wire *> wires;
    for (const Wire &wire : session.wires) {
        if (wire.toBlock == blockId && wire.toPort == port) {
            wires.append(&wire);
        }
    }
    return wires;
}

/// The wires a block takes \a side from. **Switched off, every lane**: a
/// bypassed block folds its later lanes into the first, whatever it does when
/// it is on, so a Join carries both of its lanes on each side and a Mixer all
/// of its own.
[[nodiscard]] QList<const Wire *> wiresFeeding(const Session &session, const BlockInstance &block, std::uint32_t side)
{
    if (block.enabled) {
        return wiresInto(session, block.id, inputOf(block, side));
    }
    QList<const Wire *> wires;
    for (const Wire &wire : session.wires) {
        if (wire.toBlock == block.id && wire.toPort % 2 == side) {
            wires.append(&wire);
        }
    }
    return wires;
}

/// Whether no wire enters \a block at any port, on any lane.
[[nodiscard]] bool isSource(const Session &session, const BlockInstance &block)
{
    return std::ranges::none_of(session.wires, [&block](const Wire &wire) { return wire.toBlock == block.id; });
}

/// A block that works on one ear passes the other side as it came.
[[nodiscard]] dsp::GainReach reachOn(const BlockInstance &block, std::uint32_t side, const LimitFacts &facts)
{
    const QString ear = facts.text(block, QLatin1StringView{dsp::kEarParamId});
    if (!ear.isEmpty() && ear != (side == kLeft ? "left"_L1 : "right"_L1)) {
        return dsp::GainReach::Unity;
    }
    return facts.reach(block);
}

/// Walks up one side from \a block's input. \a seen keeps a loop in a session
/// that cannot be compiled from walking for ever.
[[nodiscard]] LimitReason exposed(const Session &session,
                                  const BlockInstance &block,
                                  std::uint32_t side,
                                  const LimitFacts &facts,
                                  QSet<QString> &seen)
{
    const QList<const Wire *> wires = wiresFeeding(session, block, side);
    if (wires.size() > 1) {
        return {LimitCause::TwoWires, block.id};
    }
    if (wires.isEmpty()) {
        return {};
    }
    const BlockInstance *up = session.findBlock(wires.front()->fromBlock);
    if (up == nullptr) {
        return {};
    }
    const std::uint32_t upSide = wires.front()->fromPort % 2;
    if (isSource(session, *up)) {
        // **A source ends the walk, whatever its reach**: nothing can stand
        // before it. Otherwise lift on one ear would be limited for the sake
        // of the other, which has only the source on it.
        return {};
    }
    if (seen.contains(up->id)) {
        return {};
    }
    seen.insert(up->id);
    if (up->enabled) {
        switch (reachOn(*up, upSide, facts)) {
        case dsp::GainReach::EndsInLimit:
            return {};
        case dsp::GainReach::Adds:
            return {LimitCause::Block, up->id};
        case dsp::GainReach::Unity:
            break;
        }
    }
    return exposed(session, *up, upSide, facts, seen);
}

/// Every block with a wire to \a output, however far back.
[[nodiscard]] QList<const BlockInstance *> upstreamOf(const Session &session, const BlockInstance &output)
{
    QList<const BlockInstance *> found;
    QSet<QString> seen{output.id};
    QList<QString> pending{output.id};
    while (!pending.isEmpty()) {
        const QString blockId = pending.takeLast();
        for (const Wire &wire : session.wires) {
            if (wire.toBlock != blockId || seen.contains(wire.fromBlock)) {
                continue;
            }
            seen.insert(wire.fromBlock);
            pending.append(wire.fromBlock);
            if (const BlockInstance *block = session.findBlock(wire.fromBlock); block != nullptr) {
                found.append(block);
            }
        }
    }
    return found;
}

} // namespace

LimitFacts limitFactsOf(const dsp::BlockRegistry &registry)
{
    // Asked often, of few types: a block costs little and the parameters of
    // a type never change, so each type is described once.
    struct Type {
        dsp::BlockInfo info;
        std::unique_ptr<dsp::Block> block;
    };
    auto types = std::make_shared<QHash<QString, std::shared_ptr<Type>>>();
    const auto typeOf = [&registry, types](const BlockInstance &block) -> std::shared_ptr<Type> {
        if (const auto known = types->constFind(block.type); known != types->cend()) {
            return *known;
        }
        std::expected<std::unique_ptr<dsp::Block>, dsp::BlockError> made = registry.create(block.type.toStdString());
        std::shared_ptr<Type> type;
        if (made) {
            type = std::make_shared<Type>(Type{(*made)->info(), std::move(*made)});
        }
        types->insert(block.type, type);
        return type;
    };
    const auto defaultOf = [typeOf](const BlockInstance &block,
                                    const QString &paramId) -> std::optional<dsp::ParamValue> {
        const std::shared_ptr<Type> type = typeOf(block);
        if (type == nullptr) {
            return std::nullopt;
        }
        const std::string wanted = paramId.toStdString();
        for (const dsp::ParamDescriptor &descriptor : type->block->params()) {
            if (descriptor.id == wanted) {
                return descriptor.defaultValue;
            }
        }
        return std::nullopt;
    };

    LimitFacts facts;
    facts.reach = [typeOf](const BlockInstance &block) {
        const std::shared_ptr<Type> type = typeOf(block);
        return type == nullptr ? dsp::GainReach::Adds : type->info.gainReach;
    };
    facts.inGroup = [typeOf](const BlockInstance &block) {
        const std::shared_ptr<Type> type = typeOf(block);
        return type != nullptr && type->info.category == dsp::BlockCategory::Hearing;
    };
    facts.text = [defaultOf](const BlockInstance &block, const QString &paramId) {
        const std::optional<dsp::ParamValue> byDefault = defaultOf(block, paramId);
        if (!byDefault) {
            return QString{};
        }
        const QJsonValue held = block.params.value(paramId);
        return held.isString() ? held.toString() : QString::fromStdString(byDefault->text());
    };
    facts.number = [defaultOf](const BlockInstance &block, const QString &paramId, float fallback) {
        const std::optional<dsp::ParamValue> byDefault = defaultOf(block, paramId);
        if (!byDefault) {
            return fallback;
        }
        const QJsonValue held = block.params.value(paramId);
        return held.isDouble() ? static_cast<float>(held.toDouble()) : byDefault->asFloat().value_or(fallback);
    };
    return facts;
}

LimitReason lastLimitReason(const Session &session, const BlockInstance &output, const LimitFacts &facts)
{
    const QList<const BlockInstance *> upstream = upstreamOf(session, output);
    if (std::ranges::none_of(upstream, [&facts](const BlockInstance *block) { return facts.inGroup(*block); })) {
        return {};
    }
    const QString left = facts.text(output, u"leftChannel"_s);
    if (left == facts.text(output, u"rightChannel"_s)
        && left != QLatin1StringView{blocks::OutputDevice::kNaturalChannel}) {
        return {LimitCause::OneChannel, {}};
    }
    for (const std::uint32_t side : {kLeft, kRight}) {
        QSet<QString> seen;
        if (const LimitReason reason = exposed(session, output, side, facts, seen); reason.cause != LimitCause::None) {
            return reason;
        }
    }
    return {};
}

bool lastLimitNeeded(const Session &session, const BlockInstance &output, const LimitFacts &facts)
{
    return lastLimitReason(session, output, facts).cause != LimitCause::None;
}

float appliedLimitDb(const Session &session, const BlockInstance &output, const LimitFacts &facts)
{
    float lowest = facts.number(output, u"limitDb"_s, blocks::OutputDevice::kLimitHighestDb);
    for (const BlockInstance *block : upstreamOf(session, output)) {
        lowest = std::min(lowest, facts.number(*block, u"limiterCeilingDb"_s, lowest));
    }
    return lowest;
}

} // namespace rb::core
