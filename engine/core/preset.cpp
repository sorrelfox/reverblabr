// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/preset.h"

#include <QJsonValue>

using namespace Qt::StringLiterals;

namespace rb::core
{

namespace
{

constexpr QLatin1StringView kSchema{"schema"};
constexpr QLatin1StringView kType{"type"};
constexpr QLatin1StringView kName{"name"};
constexpr QLatin1StringView kParams{"params"};

/// The characters a file name may not hold. **A slash is the dangerous one**
/// — a preset called "../../thing" would be written outside the presets
/// directory — and the rest are here because a name that cannot be a file
/// name on one desktop is a preset that does not travel.
constexpr QLatin1StringView kForbidden{"/\\:*?\"<>|"};

} // namespace

std::string_view presetErrorName(PresetError error) noexcept
{
    switch (error) {
    case PresetError::NotFound:
        return "notFound";
    case PresetError::BadName:
        return "badName";
    case PresetError::AlreadyExists:
        return "alreadyExists";
    case PresetError::ReadOnly:
        return "readOnly";
    case PresetError::FileNotReadable:
        return "fileNotReadable";
    case PresetError::FileNotWritable:
        return "fileNotWritable";
    case PresetError::NotJson:
        return "notJson";
    case PresetError::NotAPreset:
        return "notAPreset";
    case PresetError::WrongType:
        return "wrongType";
    }
    return "unknown";
}

QJsonObject presetToJson(const Preset &preset)
{
    QJsonObject json{
        {kSchema, kPresetSchemaVersion},
        {kType, preset.typeId},
        {kParams, preset.params},
    };
    // **A factory preset carries no name.** Its words are translated from
    // its id, and a name in the file would be a second answer that goes
    // stale the moment the catalogue's does not.
    if (!preset.name.isEmpty()) {
        json.insert(kName, preset.name);
    }
    return json;
}

std::expected<Preset, PresetError> presetFromJson(const QJsonObject &json)
{
    // **A later version's file is refused, not half read.** Its parameters
    // might mean something this build would set wrongly.
    if (json.value(kSchema).toInt(kPresetSchemaVersion) > kPresetSchemaVersion) {
        return std::unexpected(PresetError::NotAPreset);
    }
    const QJsonValue type = json.value(kType);
    const QJsonValue params = json.value(kParams);
    if (!type.isString() || type.toString().isEmpty() || !params.isObject()) {
        return std::unexpected(PresetError::NotAPreset);
    }
    Preset preset;
    preset.typeId = type.toString();
    preset.name = json.value(kName).toString();
    preset.params = params.toObject();
    return preset;
}

bool presetMatches(const Preset &preset, const QJsonObject &params)
{
    for (auto value = preset.params.constBegin(); value != preset.params.constEnd(); ++value) {
        // **A parameter the block does not have does not match.** A preset
        // from a later version naming something this build never sets is
        // not one this block is showing.
        if (params.value(value.key()) != value.value()) {
            return false;
        }
    }
    // A preset that says nothing matches nothing, rather than everything:
    // otherwise an empty preset would be what every block was showing.
    return !preset.params.isEmpty();
}

bool isValidPresetName(QStringView name)
{
    const QString trimmed = name.trimmed().toString();
    if (trimmed.isEmpty() || trimmed == "."_L1 || trimmed == ".."_L1) {
        return false;
    }
    // **The whole name, not the trimmed one.** A name with a slash in it is
    // refused wherever the slash is.
    for (const QChar letter : name) {
        if (letter.unicode() < u' ' || QLatin1StringView(kForbidden).contains(QChar(letter))) {
            return false;
        }
    }
    return true;
}

} // namespace rb::core
