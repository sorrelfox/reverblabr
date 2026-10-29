// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/session.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUuid>

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string_view>

namespace rb::core
{

/// The schema version this build writes. A file with a lower one is migrated
/// on the way in; a file with a higher one is refused, because this version
/// cannot know what it would be throwing away.
inline constexpr int kSessionSchemaVersion = 1;

/// The extension session files carry.
inline constexpr QLatin1StringView kSessionFileSuffix{".rbsession"};

/// Why a session could not be read or written.
enum class SessionError : std::uint8_t {
    FileNotFound,
    FileNotReadable,
    FileNotWritable,
    NotJson, ///< The bytes are not JSON at all.
    NotAnObject, ///< The JSON is valid but is not a session object.
    SchemaTooNew, ///< Written by a later version of the application.
    MissingId, ///< No session id, or one that is not a uuid.
    MalformedBlock, ///< A block is not an object, or has no id or type.
    DuplicateBlockId,
    MalformedWire, ///< A wire does not name two blocks and two ports.
    WireBlockNotFound, ///< A wire names a block the file does not contain.
};

[[nodiscard]] std::string_view sessionErrorName(SessionError error) noexcept;

/// What went wrong while loading, and what was done about it.
struct SessionLoadFailure {
    SessionError error = SessionError::NotJson;
    /// The file that could not be read.
    QString path;
    /// Where the unreadable file was moved to, when it was moved. A caller
    /// reports this to the user as session.corruptFile with both paths.
    QString backupPath;
};

/// Turns a session into the JSON a file holds, and back.
///
/// The two are exact inverses for every field, including the parts this
/// version does not understand: a block type it does not know keeps its
/// parameters untouched, and `timers` and `automations` are carried through
/// so that a later version's file survives being opened and saved here.
[[nodiscard]] QJsonObject sessionToJson(const Session &session);

/// One block or one wire, on its own.
///
/// The session file is the obvious user of these, but not the only one: an
/// edit that adds a block has to carry the block, and a patch has to carry
/// the edit. One spelling for all of them, so a block written into a file
/// and a block sent through the facade cannot drift apart.
[[nodiscard]] QJsonObject blockToJson(const BlockInstance &block);
[[nodiscard]] std::optional<BlockInstance> blockFromJson(const QJsonValue &value);
[[nodiscard]] QJsonObject wireToJson(const Wire &wire);
[[nodiscard]] std::optional<Wire> wireFromJson(const QJsonValue &value);

/// Reads a session out of \a json.
///
/// \a isKnownType, when given, says whether a block type exists in this
/// build. A block whose type it refuses is kept exactly as it is and flagged
/// (BlockInstance::unknownType), never dropped.
[[nodiscard]] std::expected<Session, SessionError>
sessionFromJson(const QJsonObject &json, const std::function<bool(QStringView)> &isKnownType = {});

/// The session files in one directory, and the reading and writing of them.
///
/// Sessions live in `$XDG_DATA_HOME/reverblabr/sessions/<uuid>.rbsession`.
/// Saving is atomic: the bytes go to a temporary file in the same directory,
/// are flushed to the disk, and only then take the real file's place, so a
/// crash or a power cut leaves either the old session or the new one and
/// never half of either.
class SessionStore
{
public:
    /// Uses `$XDG_DATA_HOME/reverblabr/sessions`.
    SessionStore();

    /// Uses \a directory instead, which is what tests do.
    explicit SessionStore(QString directory);

    /// `$XDG_DATA_HOME/reverblabr/sessions`, whether or not it exists.
    [[nodiscard]] static QString defaultDirectory();

    [[nodiscard]] const QString &directory() const noexcept
    {
        return m_directory;
    }

    /// Where the file for \a id is, whether or not it exists.
    [[nodiscard]] QString pathFor(const QUuid &id) const;

    /// Writes \a session, making the directory if it is not there yet.
    [[nodiscard]] std::expected<void, SessionError> save(const Session &session) const;

    /// Writes \a session to \a path rather than to where it lives, which is
    /// what exporting one is. **The same atomic write**: an export
    /// interrupted half way leaves whatever was there before, not half a
    /// file, and a person who exported over the wrong name still has it.
    [[nodiscard]] std::expected<void, SessionError> saveFile(const Session &session, const QString &path) const;

    /// Reads the session with that id.
    [[nodiscard]] std::expected<Session, SessionLoadFailure>
    load(const QUuid &id, const std::function<bool(QStringView)> &isKnownType = {}) const;

    /// Reads a session from any path.
    ///
    /// A file that cannot be read is **moved aside** to
    /// `<name>.corrupt-<timestamp>` and reported, so that the application
    /// still starts and the user still has the bytes. A file that is merely
    /// missing is left alone.
    [[nodiscard]] std::expected<Session, SessionLoadFailure>
    loadFile(const QString &path, const std::function<bool(QStringView)> &isKnownType = {}) const;

    /// The ids of the sessions in the directory, in id order.
    [[nodiscard]] QList<QUuid> list() const;

    [[nodiscard]] std::expected<void, SessionError> remove(const QUuid &id) const;

private:
    QString m_directory;
};

} // namespace rb::core
