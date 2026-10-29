// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/presetstore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace rb::core
{

namespace
{

/// Where the shipped presets are, inside the binary.
constexpr QLatin1StringView kFactoryRoot{":/io/github/reverblabr/presets/"};

[[nodiscard]] std::expected<QByteArray, PresetError> readFile(const QString &path)
{
    QFile file(path);
    if (!file.exists()) {
        return std::unexpected(PresetError::NotFound);
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(PresetError::FileNotReadable);
    }
    return file.readAll();
}

[[nodiscard]] std::expected<Preset, PresetError> parse(const QByteArray &bytes)
{
    QJsonParseError parsed{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parsed);
    if (parsed.error != QJsonParseError::NoError || !document.isObject()) {
        return std::unexpected(PresetError::NotJson);
    }
    return presetFromJson(document.object());
}

} // namespace

PresetStore::PresetStore()
    : m_directory(defaultDirectory())
{
}

PresetStore::PresetStore(QString directory)
    : m_directory(std::move(directory))
{
}

QString PresetStore::defaultDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/presets"_L1;
}

QString PresetStore::pathFor(QStringView typeId, QStringView id) const
{
    return m_directory + u'/' + typeId.toString() + u'/' + id.toString() + kPresetFileSuffix;
}

QList<Preset> PresetStore::factoryPresetsFor(QStringView typeId)
{
    QList<Preset> presets;
    const QDir shipped(kFactoryRoot + typeId.toString());
    if (!shipped.exists()) {
        return presets;
    }
    // **In the order they ship**, which the file names carry as a number:
    // a factory list is a sequence somebody chose — Default first, then the
    // gentle one, then the fierce one — and sorting it by name would put
    // "Voice" before "Default" for no reason anybody could see.
    const QFileInfoList files = shipped.entryInfoList({u"*"_s + kPresetFileSuffix}, QDir::Files, QDir::Name);
    for (const QFileInfo &file : files) {
        const std::expected<QByteArray, PresetError> bytes = readFile(file.absoluteFilePath());
        if (!bytes) {
            continue;
        }
        std::expected<Preset, PresetError> preset = parse(*bytes);
        if (!preset || preset->typeId != typeId) {
            continue;
        }
        // The file is called "10-gentleLeveling.json": the number orders the
        // list and the rest is the stable id the words are looked up by.
        const QString base = file.completeBaseName();
        const qsizetype dash = base.indexOf(u'-');
        preset->id = dash >= 0 ? base.mid(dash + 1) : base;
        preset->factory = true;
        preset->name.clear(); // the interface translates the id
        presets.append(*preset);
    }
    return presets;
}

QList<Preset> PresetStore::presetsFor(QStringView typeId) const
{
    QList<Preset> presets = factoryPresetsFor(typeId);

    QList<Preset> mine;
    const QDir directory(m_directory + u'/' + typeId.toString());
    const QFileInfoList files = directory.entryInfoList({u"*"_s + kPresetFileSuffix}, QDir::Files, QDir::Name);
    for (const QFileInfo &file : files) {
        const std::expected<QByteArray, PresetError> bytes = readFile(file.absoluteFilePath());
        if (!bytes) {
            continue; // a preset that will not open is not one to offer
        }
        std::expected<Preset, PresetError> preset = parse(*bytes);
        if (!preset || preset->typeId != typeId) {
            continue;
        }
        preset->id = file.completeBaseName();
        preset->factory = false;
        if (preset->name.isEmpty()) {
            // A file somebody made by hand, or copied in, with no name in
            // it: what it is called is what the file is called.
            preset->name = preset->id;
        }
        mine.append(*preset);
    }
    std::ranges::sort(mine, [](const Preset &left, const Preset &right) {
        return QString::localeAwareCompare(left.name, right.name) < 0;
    });
    presets.append(mine);
    return presets;
}

std::expected<Preset, PresetError> PresetStore::preset(QStringView typeId, QStringView id) const
{
    // **The person's own first.** A user preset that took a factory one's id
    // is the one they saved, and it is what they mean by that name.
    const std::expected<QByteArray, PresetError> bytes = readFile(pathFor(typeId, id));
    if (bytes) {
        std::expected<Preset, PresetError> mine = parse(*bytes);
        if (mine && mine->typeId == typeId) {
            mine->id = id.toString();
            mine->factory = false;
            if (mine->name.isEmpty()) {
                mine->name = mine->id;
            }
            return mine;
        }
        if (mine) {
            return std::unexpected(PresetError::WrongType);
        }
        return std::unexpected(mine.error());
    }
    const QList<Preset> shipped = factoryPresetsFor(typeId);
    const auto found = std::ranges::find_if(shipped, [id](const Preset &preset) { return preset.id == id; });
    if (found == shipped.end()) {
        return std::unexpected(PresetError::NotFound);
    }
    return *found;
}

std::expected<Preset, PresetError> PresetStore::save(const Preset &preset, bool replace) const
{
    if (preset.typeId.isEmpty() || preset.params.isEmpty()) {
        return std::unexpected(PresetError::NotAPreset);
    }
    // **The name is the file name**, so it is checked before anything is
    // written rather than after something has been written somewhere else.
    const QString name = preset.name.trimmed();
    if (!isValidPresetName(name)) {
        return std::unexpected(PresetError::BadName);
    }
    const QString path = pathFor(preset.typeId, name);
    if (!replace && QFile::exists(path)) {
        return std::unexpected(PresetError::AlreadyExists);
    }

    const QString directoryPath = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(directoryPath)) {
        return std::unexpected(PresetError::FileNotWritable);
    }

    Preset written = preset;
    written.id = name;
    written.name = name;
    written.factory = false;

    // Written beside the real file and renamed over it, for the reason a
    // session is: a preset half written is one that will not load, and it
    // would be the one somebody was about to rely on.
    const QByteArray bytes = QJsonDocument(presetToJson(written)).toJson(QJsonDocument::Indented);
    const QString temporaryPath = path + ".tmp"_L1;
    {
        QFile temporary(temporaryPath);
        if (!temporary.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return std::unexpected(PresetError::FileNotWritable);
        }
        if (temporary.write(bytes) != bytes.size() || !temporary.flush()) {
            temporary.close();
            QFile::remove(temporaryPath);
            return std::unexpected(PresetError::FileNotWritable);
        }
    }
    QFile::remove(path); // QFile::rename will not replace an existing file
    if (!QFile::rename(temporaryPath, path)) {
        QFile::remove(temporaryPath);
        return std::unexpected(PresetError::FileNotWritable);
    }
    return written;
}

std::expected<Preset, PresetError> PresetStore::rename(QStringView typeId, QStringView id, QStringView name) const
{
    const QString wanted = name.trimmed().toString();
    if (!isValidPresetName(wanted)) {
        return std::unexpected(PresetError::BadName);
    }
    const QString from = pathFor(typeId, id);
    if (!QFile::exists(from)) {
        // Either it is a factory preset, which cannot be renamed, or there
        // is no such preset at all. Both are worth telling apart.
        const QList<Preset> shipped = factoryPresetsFor(typeId);
        const bool isFactory = std::ranges::any_of(shipped, [id](const Preset &preset) { return preset.id == id; });
        return std::unexpected(isFactory ? PresetError::ReadOnly : PresetError::NotFound);
    }
    if (wanted == id.toString()) {
        return preset(typeId, id); // renamed to what it is called already
    }
    const QString to = pathFor(typeId, wanted);
    if (QFile::exists(to)) {
        return std::unexpected(PresetError::AlreadyExists);
    }

    std::expected<Preset, PresetError> loaded = preset(typeId, id);
    if (!loaded) {
        return loaded;
    }
    loaded->id = wanted;
    loaded->name = wanted;
    // **Written under the new name, then the old one removed.** The other
    // way round, an interrupted rename loses the preset; this way the worst
    // case is two copies of it, which a person can see and deal with.
    const std::expected<Preset, PresetError> saved = save(*loaded);
    if (!saved) {
        return saved;
    }
    QFile::remove(from);
    return saved;
}

std::expected<void, PresetError> PresetStore::remove(QStringView typeId, QStringView id) const
{
    const QString path = pathFor(typeId, id);
    if (!QFile::exists(path)) {
        const QList<Preset> shipped = factoryPresetsFor(typeId);
        const bool isFactory = std::ranges::any_of(shipped, [id](const Preset &preset) { return preset.id == id; });
        return std::unexpected(isFactory ? PresetError::ReadOnly : PresetError::NotFound);
    }
    if (!QFile::remove(path)) {
        return std::unexpected(PresetError::FileNotWritable);
    }
    return {};
}

} // namespace rb::core
