// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/sessionstore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>
#include <QStandardPaths>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace rb::core
{
namespace
{

constexpr auto kKeySchemaVersion = "schemaVersion"_L1;
constexpr auto kKeyId = "id"_L1;
constexpr auto kKeyName = "name"_L1;
constexpr auto kKeyNotes = "notes"_L1;
constexpr auto kKeySettings = "settings"_L1;
constexpr auto kKeyBlocks = "blocks"_L1;
constexpr auto kKeyWires = "wires"_L1;
constexpr auto kKeyTimers = "timers"_L1;
constexpr auto kKeyAutomations = "automations"_L1;

/// Writes the position as a two-element array, which is what the file holds.
QJsonArray positionToJson(QPointF position)
{
    return QJsonArray{position.x(), position.y()};
}

QPointF positionFromJson(const QJsonValue &value)
{
    const QJsonArray array = value.toArray();
    if (array.size() != 2) {
        return {};
    }
    return QPointF{array.at(0).toDouble(), array.at(1).toDouble()};
}

QJsonObject settingsToJson(const SessionSettings &settings)
{
    return QJsonObject{
        {"autoConnect"_L1, settings.autoConnect},
        {"sampleRate"_L1, settings.sampleRate},
        {"latency"_L1, settings.latency},
        {"autoRun"_L1, settings.autoRun},
    };
}

SessionSettings settingsFromJson(const QJsonObject &json)
{
    // A setting the file leaves out, or holds in the wrong shape, falls back
    // to its default rather than stopping the load: none of them can make a
    // session unopenable.
    SessionSettings settings;
    if (const QJsonValue value = json.value("autoConnect"_L1); value.isBool()) {
        settings.autoConnect = value.toBool();
    }
    if (const QJsonValue value = json.value("sampleRate"_L1); value.isString()) {
        settings.sampleRate = value.toString();
    }
    if (const QJsonValue value = json.value("latency"_L1); value.isString()) {
        settings.latency = value.toString();
    }
    // Only a value this version knows: an unknown one, from a later version,
    // must not start a session nobody asked this version to start.
    if (const QJsonValue value = json.value("autoRun"_L1);
        value.isString() && value.toString() == QLatin1String(autoRun::login)) {
        settings.autoRun = value.toString();
    }
    return settings;
}

/// Reads one end of a wire: ["b1", 0].
bool endpointFromJson(const QJsonValue &value, QString &block, std::uint32_t &port)
{
    const QJsonArray array = value.toArray();
    if (array.size() != 2 || !array.at(0).isString() || !array.at(1).isDouble()) {
        return false;
    }
    const double portNumber = array.at(1).toDouble();
    if (portNumber < 0.0 || portNumber != std::floor(portNumber)) {
        return false;
    }
    block = array.at(0).toString();
    port = static_cast<std::uint32_t>(portNumber);
    return !block.isEmpty();
}

/// Brings a file written by an older schema up to the current one.
///
/// There is nothing to do yet: version 1 is the first. The hook is here so
/// that the first migration has an obvious place to go, and so that the rule
/// is written down — migrate on the way in, never on the way out.
QJsonObject migrateToCurrent(QJsonObject json, int fromVersion)
{
    static_cast<void>(fromVersion);
    return json;
}

/// Reads the whole file. Kept separate so that a read error and a parse error
/// are told apart.
std::expected<QByteArray, SessionError> readFile(const QString &path)
{
    QFile file(path);
    if (!file.exists()) {
        return std::unexpected(SessionError::FileNotFound);
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(SessionError::FileNotReadable);
    }
    return file.readAll();
}

/// Moves an unreadable file out of the way, and gives back where it went.
/// An empty result means it could not be moved, which is not worth failing
/// the load over: the load has failed already.
QString moveAside(const QString &path)
{
    const QString stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd-hhmmsszzz"_L1);
    const QString backup = path + ".corrupt-"_L1 + stamp;
    return QFile::rename(path, backup) ? backup : QString{};
}

} // namespace

QJsonObject blockToJson(const BlockInstance &block)
{
    return QJsonObject{
        {"id"_L1, block.id},
        {"type"_L1, block.type},
        {"nickname"_L1, block.nickname.isEmpty() ? QJsonValue{} : QJsonValue{block.nickname}},
        {"enabled"_L1, block.enabled},
        {"pos"_L1, positionToJson(block.position)},
        {"params"_L1, block.params},
        {"preset"_L1, block.preset.isEmpty() ? QJsonValue{} : QJsonValue{block.preset}},
    };
}

std::optional<BlockInstance> blockFromJson(const QJsonValue &value)
{
    if (!value.isObject()) {
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    BlockInstance block;
    block.id = object.value("id"_L1).toString();
    block.type = object.value("type"_L1).toString();
    if (block.id.isEmpty() || block.type.isEmpty()) {
        return std::nullopt;
    }
    block.nickname = object.value("nickname"_L1).toString();
    block.enabled = object.value("enabled"_L1).toBool(true);
    block.position = positionFromJson(object.value("pos"_L1));
    block.params = object.value("params"_L1).toObject();
    block.preset = object.value("preset"_L1).toString();
    return block;
}

QJsonObject wireToJson(const Wire &wire)
{
    return QJsonObject{
        {"from"_L1, QJsonArray{wire.fromBlock, static_cast<qint64>(wire.fromPort)}},
        {"to"_L1, QJsonArray{wire.toBlock, static_cast<qint64>(wire.toPort)}},
    };
}

std::optional<Wire> wireFromJson(const QJsonValue &value)
{
    if (!value.isObject()) {
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    Wire wire;
    if (!endpointFromJson(object.value("from"_L1), wire.fromBlock, wire.fromPort)
        || !endpointFromJson(object.value("to"_L1), wire.toBlock, wire.toPort)) {
        return std::nullopt;
    }
    return wire;
}

std::string_view sessionErrorName(SessionError error) noexcept
{
    switch (error) {
    case SessionError::FileNotFound:
        return "fileNotFound";
    case SessionError::FileNotReadable:
        return "fileNotReadable";
    case SessionError::FileNotWritable:
        return "fileNotWritable";
    case SessionError::NotJson:
        return "notJson";
    case SessionError::NotAnObject:
        return "notAnObject";
    case SessionError::SchemaTooNew:
        return "schemaTooNew";
    case SessionError::MissingId:
        return "missingId";
    case SessionError::MalformedBlock:
        return "malformedBlock";
    case SessionError::DuplicateBlockId:
        return "duplicateBlockId";
    case SessionError::MalformedWire:
        return "malformedWire";
    case SessionError::WireBlockNotFound:
        return "wireBlockNotFound";
    }
    return "unknown";
}

QJsonObject sessionToJson(const Session &session)
{
    QJsonArray blocks;
    for (const BlockInstance &block : session.blocks) {
        blocks.append(blockToJson(block));
    }
    QJsonArray wires;
    for (const Wire &wire : session.wires) {
        wires.append(wireToJson(wire));
    }

    return QJsonObject{
        {kKeySchemaVersion, kSessionSchemaVersion},
        {kKeyId, session.id.toString(QUuid::WithoutBraces)},
        {kKeyName, session.name},
        {kKeyNotes, session.notes},
        {kKeySettings, settingsToJson(session.settings)},
        {kKeyBlocks, blocks},
        {kKeyWires, wires},
        {kKeyTimers, session.timers},
        {kKeyAutomations, session.automations},
    };
}

std::expected<Session, SessionError> sessionFromJson(const QJsonObject &original,
                                                     const std::function<bool(QStringView)> &isKnownType)
{
    const int version = original.value(kKeySchemaVersion).toInt(kSessionSchemaVersion);
    if (version > kSessionSchemaVersion) {
        return std::unexpected(SessionError::SchemaTooNew);
    }
    const QJsonObject json = migrateToCurrent(original, version);

    Session session;
    session.id = QUuid::fromString(json.value(kKeyId).toString());
    if (session.id.isNull()) {
        return std::unexpected(SessionError::MissingId);
    }
    session.name = json.value(kKeyName).toString();
    session.notes = json.value(kKeyNotes).toString();
    session.settings = settingsFromJson(json.value(kKeySettings).toObject());
    session.timers = json.value(kKeyTimers).toArray();
    session.automations = json.value(kKeyAutomations).toArray();

    QSet<QString> seenIds;
    for (const QJsonValue &value : json.value(kKeyBlocks).toArray()) {
        if (!value.isObject()) {
            return std::unexpected(SessionError::MalformedBlock);
        }
        const std::optional<BlockInstance> read = blockFromJson(value);
        if (!read) {
            return std::unexpected(SessionError::MalformedBlock);
        }
        BlockInstance block = *read;
        if (seenIds.contains(block.id)) {
            return std::unexpected(SessionError::DuplicateBlockId);
        }
        seenIds.insert(block.id);
        // A type this build does not have keeps everything it came with and
        // is flagged, so that opening and saving cannot lose it.
        block.unknownType = isKnownType && !isKnownType(block.type);
        session.blocks.append(block);
    }

    for (const QJsonValue &value : json.value(kKeyWires).toArray()) {
        if (!value.isObject()) {
            return std::unexpected(SessionError::MalformedWire);
        }
        const std::optional<Wire> read = wireFromJson(value);
        if (!read) {
            return std::unexpected(SessionError::MalformedWire);
        }
        const Wire wire = *read;
        if (!seenIds.contains(wire.fromBlock) || !seenIds.contains(wire.toBlock)) {
            return std::unexpected(SessionError::WireBlockNotFound);
        }
        session.wires.append(wire);
    }

    return session;
}

SessionStore::SessionStore()
    : m_directory(defaultDirectory())
{
}

SessionStore::SessionStore(QString directory)
    : m_directory(std::move(directory))
{
}

QString SessionStore::defaultDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/sessions"_L1;
}

QString SessionStore::pathFor(const QUuid &id) const
{
    return m_directory + u'/' + id.toString(QUuid::WithoutBraces) + kSessionFileSuffix;
}

std::expected<void, SessionError> SessionStore::save(const Session &session) const
{
    return saveFile(session, pathFor(session.id));
}

std::expected<void, SessionError> SessionStore::saveFile(const Session &session, const QString &path) const
{
    const QString directoryPath = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(directoryPath)) {
        return std::unexpected(SessionError::FileNotWritable);
    }

    // QJsonDocument writes numbers the same way whatever the process locale
    // is, which is why the file is safe to move between machines.
    const QByteArray bytes = QJsonDocument(sessionToJson(session)).toJson(QJsonDocument::Indented);

    // Written beside the real file, flushed all the way to the disk, and only
    // then renamed over it: an interrupted save leaves the old session whole.
    const QString temporaryPath = path + ".tmp"_L1;
    {
        QFile temporary(temporaryPath);
        if (!temporary.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return std::unexpected(SessionError::FileNotWritable);
        }
        if (temporary.write(bytes) != bytes.size() || !temporary.flush()) {
            temporary.close();
            QFile::remove(temporaryPath);
            return std::unexpected(SessionError::FileNotWritable);
        }
        if (::fsync(static_cast<int>(temporary.handle())) != 0) {
            temporary.close();
            QFile::remove(temporaryPath);
            return std::unexpected(SessionError::FileNotWritable);
        }
    }

    QFile::remove(path); // QFile::rename will not replace an existing file
    if (!QFile::rename(temporaryPath, path)) {
        QFile::remove(temporaryPath);
        return std::unexpected(SessionError::FileNotWritable);
    }

    // The rename itself has to reach the disk, or a power cut could leave the
    // directory entry behind even though the bytes are safe.
    if (const int directory = ::open(QFile::encodeName(directoryPath).constData(), O_RDONLY | O_DIRECTORY);
        directory >= 0) {
        ::fsync(directory);
        ::close(directory);
    }
    return {};
}

std::expected<Session, SessionLoadFailure> SessionStore::load(const QUuid &id,
                                                              const std::function<bool(QStringView)> &isKnownType) const
{
    return loadFile(pathFor(id), isKnownType);
}

std::expected<Session, SessionLoadFailure>
SessionStore::loadFile(const QString &path, const std::function<bool(QStringView)> &isKnownType) const
{
    const std::expected<QByteArray, SessionError> bytes = readFile(path);
    if (!bytes) {
        // A file that is simply not there is not corrupt; nothing is moved.
        return std::unexpected(SessionLoadFailure{bytes.error(), path, QString{}});
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(*bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return std::unexpected(SessionLoadFailure{SessionError::NotJson, path, moveAside(path)});
    }
    if (!document.isObject()) {
        return std::unexpected(SessionLoadFailure{SessionError::NotAnObject, path, moveAside(path)});
    }

    const std::expected<Session, SessionError> session = sessionFromJson(document.object(), isKnownType);
    if (!session) {
        // A file from a later version is not damaged, so it stays where it is:
        // moving it would lose a session that a newer build could still open.
        const QString backup = session.error() == SessionError::SchemaTooNew ? QString{} : moveAside(path);
        return std::unexpected(SessionLoadFailure{session.error(), path, backup});
    }
    return *session;
}

QList<QUuid> SessionStore::list() const
{
    QList<QUuid> ids;
    const QDir directory(m_directory);
    const QStringList names = directory.entryList({u"*%1"_s.arg(kSessionFileSuffix)}, QDir::Files, QDir::Name);
    for (const QString &name : names) {
        const QUuid id = QUuid::fromString(QStringView{name}.chopped(kSessionFileSuffix.size()));
        if (!id.isNull()) {
            ids.append(id);
        }
    }
    std::ranges::sort(ids);
    return ids;
}

std::expected<void, SessionError> SessionStore::remove(const QUuid &id) const
{
    const QString path = pathFor(id);
    if (!QFile::exists(path)) {
        return std::unexpected(SessionError::FileNotFound);
    }
    if (!QFile::remove(path)) {
        return std::unexpected(SessionError::FileNotWritable);
    }
    return {};
}

} // namespace rb::core
