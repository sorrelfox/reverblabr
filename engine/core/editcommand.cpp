// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/editcommand.h"

#include "core/sessionstore.h"

#include <QJsonArray>
#include <QJsonValue>

#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>

using namespace Qt::Literals::StringLiterals;

namespace rb::core
{
namespace
{

using Inverse = std::expected<EditInverse, EditError>;

/// False, but not until the compiler knows which command it is being asked
/// about. **What turns a forgotten command into a build failure**: the
/// chains below used to end in a plain `else`, so a new command added
/// without a branch of its own would quietly have been named and written
/// out as the last one anybody did think of.
template <typename Command>
constexpr bool kUnhandledCommand = false;

/// Where an item goes in a list of \a size: \a index when it names a place
/// in it, and the end otherwise.
qsizetype insertionPoint(qsizetype index, qsizetype size)
{
    return index >= 0 && index <= size ? index : size;
}

Inverse apply(Session &session, const AddBlock &command)
{
    if (command.block.id.isEmpty()) {
        return std::unexpected(EditError::BlockIdEmpty);
    }
    if (command.block.type.isEmpty()) {
        return std::unexpected(EditError::BlockTypeEmpty);
    }
    if (session.hasBlock(command.block.id)) {
        return std::unexpected(EditError::BlockIdInUse);
    }
    session.blocks.insert(insertionPoint(command.index, session.blocks.size()), command.block);
    return EditInverse{RemoveBlock{command.block.id}};
}

Inverse apply(Session &session, const RemoveBlock &command)
{
    const BlockInstance *block = session.findBlock(command.blockId);
    if (block == nullptr) {
        return std::unexpected(EditError::BlockNotFound);
    }

    // Undoing a removal has to put the wires back too, after the block and
    // each one where it was, so the inverse is several commands. The wires
    // are collected in list order, so putting them back in that order lands
    // each at the index it came from.
    EditInverse inverse;
    inverse.emplace_back(AddBlock{*block, session.blocks.indexOf(*block)});
    for (qsizetype index = 0; index < session.wires.size(); ++index) {
        const Wire &wire = session.wires.at(index);
        if (wire.fromBlock == command.blockId || wire.toBlock == command.blockId) {
            inverse.emplace_back(Connect{wire, index});
        }
    }

    session.wires.removeIf(
        [&](const Wire &wire) { return wire.fromBlock == command.blockId || wire.toBlock == command.blockId; });
    session.blocks.removeIf([&](const BlockInstance &candidate) { return candidate.id == command.blockId; });
    return inverse;
}

Inverse apply(Session &session, const MoveBlock &command)
{
    BlockInstance *block = session.findBlock(command.blockId);
    if (block == nullptr) {
        return std::unexpected(EditError::BlockNotFound);
    }
    const QPointF previous = block->position;
    block->position = command.position;
    return EditInverse{MoveBlock{command.blockId, previous}};
}

Inverse apply(Session &session, const Connect &command)
{
    if (const std::optional<EditError> refused = connectRefusal(session, command.wire)) {
        return std::unexpected(*refused);
    }
    session.wires.insert(insertionPoint(command.index, session.wires.size()), command.wire);
    return EditInverse{Disconnect{command.wire}};
}

Inverse apply(Session &session, const Disconnect &command)
{
    const qsizetype index = session.wires.indexOf(command.wire);
    if (index < 0) {
        return std::unexpected(EditError::WireNotFound);
    }
    session.wires.removeAt(index);
    return EditInverse{Connect{command.wire, index}};
}

Inverse apply(Session &session, const SetParam &command)
{
    BlockInstance *block = session.findBlock(command.blockId);
    if (block == nullptr) {
        return std::unexpected(EditError::BlockNotFound);
    }
    if (command.key.isEmpty()) {
        return std::unexpected(EditError::ParamKeyEmpty);
    }

    // A parameter that was not set before is put back by removing it again,
    // which an undefined value means here.
    const QJsonValue previous = block->params.value(command.key);
    if (command.value.isUndefined()) {
        block->params.remove(command.key);
    } else {
        block->params.insert(command.key, command.value);
    }
    return EditInverse{SetParam{command.blockId, command.key, previous}};
}

Inverse apply(Session &session, const Rename &command)
{
    if (command.blockId.isEmpty()) {
        const QString previous = session.name;
        session.name = command.name;
        return EditInverse{Rename{QString{}, previous}};
    }
    BlockInstance *block = session.findBlock(command.blockId);
    if (block == nullptr) {
        return std::unexpected(EditError::BlockNotFound);
    }
    const QString previous = block->nickname;
    block->nickname = command.name;
    return EditInverse{Rename{command.blockId, previous}};
}

Inverse apply(Session &session, const SetNotes &command)
{
    // No check of any kind, which is the whole of this command's contract:
    // whatever somebody wrote is what the session keeps.
    const QString previous = session.notes;
    session.notes = command.notes;
    return EditInverse{SetNotes{previous}};
}

Inverse apply(Session &session, const SetEnabled &command)
{
    BlockInstance *block = session.findBlock(command.blockId);
    if (block == nullptr) {
        return std::unexpected(EditError::BlockNotFound);
    }
    const bool previous = block->enabled;
    block->enabled = command.enabled;
    return EditInverse{SetEnabled{command.blockId, previous}};
}

Inverse apply(Session &session, const ApplyPreset &command)
{
    BlockInstance *block = session.findBlock(command.blockId);
    if (block == nullptr) {
        return std::unexpected(EditError::BlockNotFound);
    }
    // **The inverse is built before anything is set**, out of exactly the
    // parameters this command touches — so undoing a preset puts back the
    // values that were there and nothing else, and leaves alone the ones
    // applying it never looked at.
    ApplyPreset back;
    back.blockId = command.blockId;
    back.presetId = block->preset;

    for (const QString &key : command.clear) {
        if (const QJsonValue previous = block->params.value(key); !previous.isUndefined()) {
            back.params.insert(key, previous);
        }
        block->params.remove(key);
    }
    for (auto value = command.params.constBegin(); value != command.params.constEnd(); ++value) {
        const QJsonValue previous = block->params.value(value.key());
        if (previous.isUndefined()) {
            // It was not set before, so putting it back means taking it off
            // again rather than setting it to anything.
            back.clear.append(value.key());
        } else {
            back.params.insert(value.key(), previous);
        }
        block->params.insert(value.key(), value.value());
    }
    block->preset = command.presetId;
    return EditInverse{back};
}

Inverse apply(Session &session, const SetSessionSetting &command)
{
    SessionSettings &settings = session.settings;
    switch (command.key) {
    case SessionSettingKey::AutoConnect: {
        if (!command.value.isBool()) {
            return std::unexpected(EditError::SettingTypeMismatch);
        }
        const bool previous = settings.autoConnect;
        settings.autoConnect = command.value.toBool();
        return EditInverse{SetSessionSetting{command.key, QJsonValue{previous}}};
    }
    case SessionSettingKey::SampleRate: {
        if (!command.value.isString()) {
            return std::unexpected(EditError::SettingTypeMismatch);
        }
        const QString wanted = command.value.toString();
        // "auto", or a rate in hertz written out. Anything else would reach
        // the engine as a rate it cannot ask PipeWire for.
        bool isNumber = false;
        static_cast<void>(wanted.toUInt(&isNumber));
        if (wanted != QLatin1String("auto") && !isNumber) {
            return std::unexpected(EditError::SettingValueNotAllowed);
        }
        const QString previous = settings.sampleRate;
        settings.sampleRate = wanted;
        return EditInverse{SetSessionSetting{command.key, QJsonValue{previous}}};
    }
    case SessionSettingKey::Latency: {
        if (!command.value.isString()) {
            return std::unexpected(EditError::SettingTypeMismatch);
        }
        const QString wanted = command.value.toString();
        if (wanted != QLatin1String("default") && wanted != QLatin1String("low") && wanted != QLatin1String("high")) {
            return std::unexpected(EditError::SettingValueNotAllowed);
        }
        const QString previous = settings.latency;
        settings.latency = wanted;
        return EditInverse{SetSessionSetting{command.key, QJsonValue{previous}}};
    }
    case SessionSettingKey::AutoRun: {
        if (!command.value.isString()) {
            return std::unexpected(EditError::SettingTypeMismatch);
        }
        const QString wanted = command.value.toString();
        if (wanted != QLatin1String(autoRun::off) && wanted != QLatin1String(autoRun::login)) {
            return std::unexpected(EditError::SettingValueNotAllowed);
        }
        const QString previous = settings.autoRun;
        settings.autoRun = wanted;
        return EditInverse{SetSessionSetting{command.key, QJsonValue{previous}}};
    }
    }
    return std::unexpected(EditError::SettingValueNotAllowed);
}

} // namespace

std::string_view sessionSettingKeyName(SessionSettingKey key) noexcept
{
    switch (key) {
    case SessionSettingKey::AutoConnect:
        return "autoConnect";
    case SessionSettingKey::SampleRate:
        return "sampleRate";
    case SessionSettingKey::Latency:
        return "latency";
    case SessionSettingKey::AutoRun:
        return "autoRun";
    }
    return "unknown";
}

std::string_view editCommandName(const EditCommand &command) noexcept
{
    return std::visit(
        [](const auto &specific) -> std::string_view {
            using Command = std::decay_t<decltype(specific)>;
            if constexpr (std::is_same_v<Command, AddBlock>) {
                return "addBlock";
            } else if constexpr (std::is_same_v<Command, RemoveBlock>) {
                return "removeBlock";
            } else if constexpr (std::is_same_v<Command, MoveBlock>) {
                return "moveBlock";
            } else if constexpr (std::is_same_v<Command, Connect>) {
                return "connect";
            } else if constexpr (std::is_same_v<Command, Disconnect>) {
                return "disconnect";
            } else if constexpr (std::is_same_v<Command, SetParam>) {
                return "setParam";
            } else if constexpr (std::is_same_v<Command, Rename>) {
                return "rename";
            } else if constexpr (std::is_same_v<Command, SetNotes>) {
                return "setNotes";
            } else if constexpr (std::is_same_v<Command, SetEnabled>) {
                return "setEnabled";
            } else if constexpr (std::is_same_v<Command, SetSessionSetting>) {
                return "setSessionSetting";
            } else if constexpr (std::is_same_v<Command, ApplyPreset>) {
                return "applyPreset";
            } else {
                static_assert(kUnhandledCommand<Command>, "this command has no name here");
            }
        },
        command);
}

std::string_view editErrorName(EditError error) noexcept
{
    switch (error) {
    case EditError::BlockIdEmpty:
        return "blockIdEmpty";
    case EditError::BlockIdInUse:
        return "blockIdInUse";
    case EditError::BlockTypeEmpty:
        return "blockTypeEmpty";
    case EditError::BlockNotFound:
        return "blockNotFound";
    case EditError::WireBlockNotFound:
        return "wireBlockNotFound";
    case EditError::WireToItself:
        return "wireToItself";
    case EditError::WireAlreadyExists:
        return "wireAlreadyExists";
    case EditError::WireWouldCycle:
        return "wireWouldCycle";
    case EditError::WireNotFound:
        return "wireNotFound";
    case EditError::ParamKeyEmpty:
        return "paramKeyEmpty";
    case EditError::SettingTypeMismatch:
        return "settingTypeMismatch";
    case EditError::SettingValueNotAllowed:
        return "settingValueNotAllowed";
    }
    return "unknown";
}

std::optional<EditError> connectRefusal(const Session &session, const Wire &wire)
{
    if (!session.hasBlock(wire.fromBlock) || !session.hasBlock(wire.toBlock)) {
        return EditError::WireBlockNotFound;
    }
    if (wire.fromBlock == wire.toBlock) {
        return EditError::WireToItself;
    }
    if (session.hasWire(wire)) {
        return EditError::WireAlreadyExists;
    }
    // **Refused here rather than at the moment somebody presses Start.** A
    // loop is what the graph compiler cannot schedule, and by the time it
    // says so the wire has been drawn, the session looks finished and
    // nothing points at which of the wires is the wrong one. The session
    // knows enough to say no while the pointer is still on the port.
    if (session.reaches(wire.toBlock, wire.fromBlock)) {
        return EditError::WireWouldCycle;
    }
    return std::nullopt;
}

std::expected<EditInverse, EditError> applyEdit(Session &session, const EditCommand &command)
{
    std::expected<EditInverse, EditError> result =
        std::visit([&session](const auto &specific) { return apply(session, specific); }, command);
    if (result) {
        ++session.revision;
    }
    return result;
}

std::expected<std::vector<EditCommand>, EditJsonError> editCommandsFromJson(const QJsonDocument &document)
{
    std::vector<EditCommand> commands;
    if (document.isObject()) {
        std::expected<EditCommand, EditJsonError> one = editCommandFromJson(document.object());
        if (!one) {
            return std::unexpected(one.error());
        }
        commands.push_back(std::move(*one));
        return commands;
    }
    if (!document.isArray()) {
        return std::unexpected(EditJsonError::MalformedArguments);
    }
    const QJsonArray array = document.array();
    if (array.isEmpty()) {
        return std::unexpected(EditJsonError::MalformedArguments);
    }
    commands.reserve(static_cast<std::size_t>(array.size()));
    for (const QJsonValue &entry : array) {
        if (!entry.isObject()) {
            return std::unexpected(EditJsonError::MalformedArguments);
        }
        std::expected<EditCommand, EditJsonError> one = editCommandFromJson(entry.toObject());
        if (!one) {
            return std::unexpected(one.error());
        }
        commands.push_back(std::move(*one));
    }
    return commands;
}

std::expected<EditInverse, EditBatchError> applyEdits(Session &session, std::span<const EditCommand> commands)
{
    EditInverse inverse;
    for (std::size_t index = 0; index < commands.size(); ++index) {
        std::expected<EditInverse, EditError> one = applyEdit(session, commands[index]);
        if (!one) {
            return std::unexpected(EditBatchError{index, one.error()});
        }
        // Undone in the opposite order to the way they were applied.
        inverse.insert(inverse.begin(), one->begin(), one->end());
    }
    return inverse;
}

std::string_view editJsonErrorName(EditJsonError error) noexcept
{
    switch (error) {
    case EditJsonError::NoOperation:
        return "noOperation";
    case EditJsonError::UnknownOperation:
        return "unknownOperation";
    case EditJsonError::MalformedArguments:
        return "malformedArguments";
    }
    return "unknown";
}

QJsonObject editCommandToJson(const EditCommand &command)
{
    const std::string_view name = editCommandName(command);
    QJsonObject json{{"op"_L1, QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()))}};
    std::visit(
        [&json](const auto &specific) {
            using Command = std::decay_t<decltype(specific)>;
            if constexpr (std::is_same_v<Command, AddBlock>) {
                json.insert("block"_L1, blockToJson(specific.block));
                json.insert("index"_L1, static_cast<qint64>(specific.index));
            } else if constexpr (std::is_same_v<Command, RemoveBlock>) {
                json.insert("blockId"_L1, specific.blockId);
            } else if constexpr (std::is_same_v<Command, MoveBlock>) {
                json.insert("blockId"_L1, specific.blockId);
                json.insert("x"_L1, specific.position.x());
                json.insert("y"_L1, specific.position.y());
            } else if constexpr (std::is_same_v<Command, Connect>) {
                json.insert("wire"_L1, wireToJson(specific.wire));
                json.insert("index"_L1, static_cast<qint64>(specific.index));
            } else if constexpr (std::is_same_v<Command, Disconnect>) {
                json.insert("wire"_L1, wireToJson(specific.wire));
            } else if constexpr (std::is_same_v<Command, SetParam>) {
                json.insert("blockId"_L1, specific.blockId);
                json.insert("key"_L1, specific.key);
                json.insert("value"_L1, specific.value);
            } else if constexpr (std::is_same_v<Command, Rename>) {
                json.insert("blockId"_L1, specific.blockId);
                json.insert("name"_L1, specific.name);
            } else if constexpr (std::is_same_v<Command, SetNotes>) {
                json.insert("notes"_L1, specific.notes);
            } else if constexpr (std::is_same_v<Command, SetEnabled>) {
                json.insert("blockId"_L1, specific.blockId);
                json.insert("enabled"_L1, specific.enabled);
            } else if constexpr (std::is_same_v<Command, SetSessionSetting>) {
                const std::string_view key = sessionSettingKeyName(specific.key);
                json.insert("key"_L1, QString::fromUtf8(key.data(), static_cast<qsizetype>(key.size())));
                json.insert("value"_L1, specific.value);
            } else if constexpr (std::is_same_v<Command, ApplyPreset>) {
                json.insert("blockId"_L1, specific.blockId);
                json.insert("preset"_L1, specific.presetId);
                json.insert("params"_L1, specific.params);
                // **Written even when empty**, because a reader that found
                // no "clear" and one that found an empty one would otherwise
                // be told different things by the same edit.
                QJsonArray clear;
                for (const QString &key : specific.clear) {
                    clear.append(key);
                }
                json.insert("clear"_L1, clear);
            } else {
                static_assert(kUnhandledCommand<Command>, "this command has no JSON here");
            }
        },
        command);
    return json;
}

std::expected<EditCommand, EditJsonError> editCommandFromJson(const QJsonObject &json)
{
    const QJsonValue operation = json.value("op"_L1);
    if (!operation.isString()) {
        return std::unexpected(EditJsonError::NoOperation);
    }
    const QString op = operation.toString();
    const QString blockId = json.value("blockId"_L1).toString();

    if (op == "addBlock"_L1) {
        const std::optional<BlockInstance> block = blockFromJson(json.value("block"_L1));
        if (!block) {
            return std::unexpected(EditJsonError::MalformedArguments);
        }
        AddBlock command;
        command.block = *block;
        command.index = static_cast<qsizetype>(json.value("index"_L1).toInteger(-1));
        return command;
    }
    if (op == "removeBlock"_L1) {
        if (blockId.isEmpty()) {
            return std::unexpected(EditJsonError::MalformedArguments);
        }
        return RemoveBlock{blockId};
    }
    if (op == "moveBlock"_L1) {
        if (blockId.isEmpty() || !json.value("x"_L1).isDouble() || !json.value("y"_L1).isDouble()) {
            return std::unexpected(EditJsonError::MalformedArguments);
        }
        MoveBlock command;
        command.blockId = blockId;
        command.position = QPointF{json.value("x"_L1).toDouble(), json.value("y"_L1).toDouble()};
        return command;
    }
    if (op == "connect"_L1 || op == "disconnect"_L1) {
        const std::optional<Wire> wire = wireFromJson(json.value("wire"_L1));
        if (!wire) {
            return std::unexpected(EditJsonError::MalformedArguments);
        }
        if (op == "disconnect"_L1) {
            return Disconnect{*wire};
        }
        Connect command;
        command.wire = *wire;
        command.index = static_cast<qsizetype>(json.value("index"_L1).toInteger(-1));
        return command;
    }
    if (op == "applyPreset"_L1) {
        if (blockId.isEmpty() || !json.value("params"_L1).isObject()) {
            return std::unexpected(EditJsonError::MalformedArguments);
        }
        ApplyPreset command;
        command.blockId = blockId;
        // **An empty preset id is a real answer**, meaning the block is
        // showing none, so its absence is not an error.
        command.presetId = json.value("preset"_L1).toString();
        command.params = json.value("params"_L1).toObject();
        const QJsonValue clear = json.value("clear"_L1);
        if (!clear.isUndefined() && !clear.isArray()) {
            return std::unexpected(EditJsonError::MalformedArguments);
        }
        for (const QJsonValue &key : clear.toArray()) {
            if (!key.isString() || key.toString().isEmpty()) {
                return std::unexpected(EditJsonError::MalformedArguments);
            }
            command.clear.append(key.toString());
        }
        // A preset that sets nothing and clears nothing is not an edit.
        if (command.params.isEmpty() && command.clear.isEmpty()) {
            return std::unexpected(EditJsonError::MalformedArguments);
        }
        return command;
    }
    if (op == "setParam"_L1) {
        const QString key = json.value("key"_L1).toString();
        if (blockId.isEmpty() || key.isEmpty() || json.value("value"_L1).isUndefined()) {
            return std::unexpected(EditJsonError::MalformedArguments);
        }
        return SetParam{blockId, key, json.value("value"_L1)};
    }
    if (op == "rename"_L1) {
        if (!json.value("name"_L1).isString()) {
            return std::unexpected(EditJsonError::MalformedArguments);
        }
        // An empty blockId renames the session, which is deliberate.
        return Rename{blockId, json.value("name"_L1).toString()};
    }
    if (op == "setNotes"_L1) {
        // The note may say anything, empty included, but it has to be a
        // string: an edit that arrived without one would otherwise wipe
        // what somebody wrote.
        if (!json.value("notes"_L1).isString()) {
            return std::unexpected(EditJsonError::MalformedArguments);
        }
        return SetNotes{json.value("notes"_L1).toString()};
    }
    if (op == "setEnabled"_L1) {
        if (blockId.isEmpty() || !json.value("enabled"_L1).isBool()) {
            return std::unexpected(EditJsonError::MalformedArguments);
        }
        return SetEnabled{blockId, json.value("enabled"_L1).toBool()};
    }
    if (op == "setSessionSetting"_L1) {
        const QString key = json.value("key"_L1).toString();
        SetSessionSetting command;
        if (key == "autoConnect"_L1) {
            command.key = SessionSettingKey::AutoConnect;
        } else if (key == "sampleRate"_L1) {
            command.key = SessionSettingKey::SampleRate;
        } else if (key == "latency"_L1) {
            command.key = SessionSettingKey::Latency;
        } else if (key == "autoRun"_L1) {
            command.key = SessionSettingKey::AutoRun;
        } else {
            return std::unexpected(EditJsonError::MalformedArguments);
        }
        if (json.value("value"_L1).isUndefined()) {
            return std::unexpected(EditJsonError::MalformedArguments);
        }
        command.value = json.value("value"_L1);
        return command;
    }
    return std::unexpected(EditJsonError::UnknownOperation);
}

} // namespace rb::core
