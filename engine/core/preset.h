// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

#include <cstdint>
#include <expected>
#include <string_view>

namespace rb::core
{

/// Named settings for one kind of block.
///
/// **A preset is a block type and some parameter values, and nothing else.**
/// Not a block: it carries no id, no position, no nickname and no wires, so
/// the same preset applies to every Volume block in every session. What it
/// does not mention it does not change — a preset written by an earlier
/// version sets the parameters it knew about and leaves the rest where they
/// are, which is what lets a block grow a parameter without every preset for
/// it becoming wrong.
///
/// **Locale-independent, always.** The values are JSON numbers, so they are
/// written with a full stop whatever the reader's locale does to a decimal
/// separator; a preset saved in de_DE opens in en_GB and the other way
/// about. This is why the values never travel as the text a panel shows.
struct Preset {
    /// Stable, and the file name a user preset is stored under. For a
    /// factory preset this is an id such as "gentleLeveling", which is
    /// never shown: the interface looks the words up (docs/i18n.md).
    QString id;
    /// The block type it belongs to, for example "effect.volume".
    QString typeId;
    /// What a person called it. **Empty for a factory preset**, whose name
    /// is translated from its id and is not this build's to write down.
    QString name;
    /// The parameter values, exactly as a block's `params` object holds
    /// them.
    QJsonObject params;
    /// **Read-only, and not because of a file permission.** A factory preset
    /// ships with the application; saving over one would leave a person with
    /// no way back to what the block was tuned to.
    bool factory = false;

    [[nodiscard]] bool operator==(const Preset &other) const = default;
};

/// Why a preset could not be read, written or named.
enum class PresetError : std::uint8_t {
    NotFound,
    /// A name that is empty, or that cannot be a file name.
    BadName,
    /// Something of that name is already there, and the caller did not say
    /// to replace it.
    AlreadyExists,
    /// A factory preset cannot be written to, renamed or deleted.
    ReadOnly,
    FileNotReadable,
    FileNotWritable,
    NotJson,
    /// The JSON is valid but is not a preset: no type, or no parameters.
    NotAPreset,
    /// The preset is for another kind of block.
    WrongType,
};

[[nodiscard]] std::string_view presetErrorName(PresetError error) noexcept;

/// The preset as a file holds it, and back.
///
/// The file is `{"schema": 1, "type": "effect.volume", "name": "Loud",
/// "params": {…}}`. An id is not written: a user preset's id is its file
/// name, and a factory preset's is the name of the file it ships as, so
/// writing it twice would be two answers to one question.
[[nodiscard]] QJsonObject presetToJson(const Preset &preset);
[[nodiscard]] std::expected<Preset, PresetError> presetFromJson(const QJsonObject &json);

/// The schema version this build writes. A file with a higher one is refused
/// rather than half understood.
inline constexpr int kPresetSchemaVersion = 1;

/// What a preset file is called.
inline constexpr QLatin1StringView kPresetFileSuffix{".json"};

/// Whether \a params matches \a preset for the parameters the preset names.
///
/// **What makes the menu say "Manual".** A block whose values are a preset's
/// is showing that preset whether or not anybody clicked it, and a block
/// with one value changed is not showing it any more. Parameters the preset
/// does not mention are not compared, because applying it would not have
/// touched them.
[[nodiscard]] bool presetMatches(const Preset &preset, const QJsonObject &params);

/// Whether \a name can be a preset's, which is to say whether it can be a
/// file name on any sensible system. **Checked before anything is written**,
/// so that a name with a slash in it is refused rather than quietly saving
/// somewhere else.
[[nodiscard]] bool isValidPresetName(QStringView name);

} // namespace rb::core
