// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/session.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPointF>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace rb::core
{

/// The session's settings, one per key the file holds. Used by
/// SetSessionSetting so that a typo is an error rather than a new setting.
enum class SessionSettingKey : std::uint8_t {
    AutoConnect,
    SampleRate,
    Latency,
    AutoRun,
};

/// The key's name in the file, for example "autoConnect".
[[nodiscard]] std::string_view sessionSettingKeyName(SessionSettingKey key) noexcept;

// --- the commands ---

/// Puts a block on the canvas. It carries the whole block, so that undoing a
/// removal puts back exactly what was there — its position in the list
/// included, because that is the order the file and the canvas hold.
struct AddBlock {
    BlockInstance block;
    /// Where in the list it goes, or -1 for the end. Out of range counts as
    /// the end.
    qsizetype index = -1;

    [[nodiscard]] bool operator==(const AddBlock &other) const = default;
};

struct RemoveBlock {
    QString blockId;

    [[nodiscard]] bool operator==(const RemoveBlock &other) const = default;
};

struct MoveBlock {
    QString blockId;
    QPointF position;

    [[nodiscard]] bool operator==(const MoveBlock &other) const = default;
};

struct Connect {
    Wire wire;
    /// Where in the list it goes, or -1 for the end, for the same reason as
    /// AddBlock::index.
    qsizetype index = -1;

    [[nodiscard]] bool operator==(const Connect &other) const = default;
};

struct Disconnect {
    Wire wire;

    [[nodiscard]] bool operator==(const Disconnect &other) const = default;
};

/// Sets one of a block's parameters. The value is JSON, because the model
/// does not know what the block's parameters are; the facade checks it
/// against the block's descriptors before it gets here.
struct SetParam {
    QString blockId;
    QString key;
    QJsonValue value;

    [[nodiscard]] bool operator==(const SetParam &other) const = default;
};

/// Renames the session, or a block when \a blockId is not empty.
struct Rename {
    QString blockId;
    QString name;

    [[nodiscard]] bool operator==(const Rename &other) const = default;
};

/// Replaces what the person wrote about the session.
///
/// **Nothing about a note can be wrong**, an empty one included: clearing
/// the box is as much an edit as filling it in, so no value of this command
/// is ever refused.
struct SetNotes {
    QString notes;

    [[nodiscard]] bool operator==(const SetNotes &other) const = default;
};

struct SetEnabled {
    QString blockId;
    bool enabled = true;

    [[nodiscard]] bool operator==(const SetEnabled &other) const = default;
};

struct SetSessionSetting {
    SessionSettingKey key = SessionSettingKey::AutoConnect;
    QJsonValue value;

    [[nodiscard]] bool operator==(const SetSessionSetting &other) const = default;
};

/// Sets several of a block's parameters at once and says which preset it is
/// showing.
///
/// **One command, so one undo.** A preset that moves nine knobs must not be
/// nine steps to take back — somebody who tried one and did not like it
/// presses undo once, as they would for anything else they did once.
///
/// **It carries the values, not the preset's name.** The session and every
/// client that follows a patch must be able to apply this without a preset
/// store, on a machine where that preset does not exist, and years later
/// when it has been deleted. `presetId` is only a marker for the panel: it
/// is what the menu shows instead of "Manual", and it is empty when nothing
/// is being shown.
struct ApplyPreset {
    QString blockId;
    /// The preset this leaves the block showing, or empty for none.
    QString presetId;
    /// The values to set. A parameter not named here is left exactly as it
    /// was, because applying the preset would not have touched it.
    QJsonObject params;
    /// Parameters to take off the block instead of setting.
    ///
    /// **This is how an inverse puts back "was never set".** A QJsonObject
    /// cannot hold an undefined value — inserting one removes the key — so
    /// the keys to remove travel beside the ones to set rather than as
    /// undefined values among them.
    QStringList clear;

    [[nodiscard]] bool operator==(const ApplyPreset &other) const = default;
};

/// Every change a session can undergo. Nothing else may touch the model: this
/// is what makes undo, the change numbering and the facade's ApplyEdit all
/// work from one place.
using EditCommand = std::variant<AddBlock,
                                 RemoveBlock,
                                 MoveBlock,
                                 Connect,
                                 Disconnect,
                                 SetParam,
                                 Rename,
                                 SetNotes,
                                 SetEnabled,
                                 SetSessionSetting,
                                 ApplyPreset>;

/// The stable name of a command, for diagnostics and for the facade.
[[nodiscard]] std::string_view editCommandName(const EditCommand &command) noexcept;

/// Why an edit was refused. Every one of these leaves the session exactly as
/// it was.
enum class EditError : std::uint8_t {
    BlockIdEmpty,
    BlockIdInUse,
    BlockTypeEmpty,
    BlockNotFound,
    WireBlockNotFound, ///< A wire names a block that is not in the session.
    WireToItself, ///< A block cannot be wired to itself.
    WireAlreadyExists,
    WireWouldCycle, ///< The wire would let the signal reach its own source again.
    WireNotFound,
    ParamKeyEmpty,
    SettingTypeMismatch, ///< A setting was given a value of the wrong type.
    SettingValueNotAllowed, ///< A setting was given a value it does not take.
};

[[nodiscard]] std::string_view editErrorName(EditError error) noexcept;

/// What has to be applied, in order, to put the session back as it was. A
/// removal gives back the block and every wire that went with it, so one
/// command's undo can be several.
using EditInverse = std::vector<EditCommand>;

/// Why an edit could not be read from JSON.
enum class EditJsonError : std::uint8_t {
    NoOperation, ///< No "op", or not a string.
    UnknownOperation, ///< An "op" this build does not know.
    MalformedArguments, ///< The operation is known; what came with it is not.
};

[[nodiscard]] std::string_view editJsonErrorName(EditJsonError error) noexcept;

/// One edit, as the facade carries it.
///
/// `{"op": "addBlock", "block": {…}, "index": -1}`, where the operation is
/// the name editCommandName() gives and a block or a wire is spelled exactly
/// as the session file spells it. **This is what ApplyEdit takes and what a
/// patch carries**, so the GUI can apply the same edit to its own copy
/// rather than being sent the whole session again.
[[nodiscard]] QJsonObject editCommandToJson(const EditCommand &command);
[[nodiscard]] std::expected<EditCommand, EditJsonError> editCommandFromJson(const QJsonObject &json);

/// Every command an edit carries, whether it arrived as one or as a list.
///
/// **A list is how a caller says "this is one thing".** A stereo link is two
/// wires and a person drew one; sending them separately leaves it to timing
/// to guess that they belong together, and leaves a half-drawn link possible
/// if the second is refused. A list is neither: it applies whole or not at
/// all, and it is one step to take back.
///
/// An empty list is malformed rather than a change of nothing, because a
/// caller that meant nothing had no reason to call.
[[nodiscard]] std::expected<std::vector<EditCommand>, EditJsonError>
editCommandsFromJson(const QJsonDocument &document);

/// Why \a wire could not be connected to \a session, or nothing when it
/// could be. **What applyEdit() asks**, so that the interface can grey out
/// a port it would be refused on without having to know the rules a second
/// time — and without applying anything to find out.
///
/// Ports are not among the rules here: how many a block type has is the
/// registry's to say, and the session does not hold it (blockcatalogue.h).
[[nodiscard]] std::optional<EditError> connectRefusal(const Session &session, const Wire &wire);

/// Checks \a command, applies it and returns its inverse. On failure the
/// session is untouched.
///
/// A successful edit counts Session::revision up by one, so that a listener
/// can tell which changes it has already seen.
[[nodiscard]] std::expected<EditInverse, EditError> applyEdit(Session &session, const EditCommand &command);

/// Applies several commands in order, stopping at the first that fails.
///
/// **It does not undo what it already applied**, because the caller holds the
/// inverses and is the one that knows what to do with them. Returns how many
/// were applied along with the error.
struct EditBatchError {
    std::size_t applied = 0;
    EditError error = EditError::BlockNotFound;

    [[nodiscard]] bool operator==(const EditBatchError &other) const = default;
};

[[nodiscard]] std::expected<EditInverse, EditBatchError> applyEdits(Session &session,
                                                                    std::span<const EditCommand> commands);

} // namespace rb::core
