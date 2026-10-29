// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/editcommand.h"
#include "core/preset.h"

#include <QJsonArray>
#include <QString>

#include <expected>

namespace rb::dsp
{
class BlockRegistry;
}

namespace rb::core
{

struct Session;

/// What the two engines both do with presets.
///
/// **Here rather than in each of them**, because the mock exists to behave
/// exactly like the engine: a rule written twice is a rule that is only true
/// until somebody fixes one copy. Each engine keeps its own sessions and
/// reports its own errors; what a preset *means* is this.

/// The edit that puts \a preset on \a blockId.
///
/// **Applying a preset never clears anything.** What the preset does not
/// mention it does not touch, so a Volume preset that names only the gain
/// leaves the mute and the overdrive alone — and a block can grow a
/// parameter without every preset for it becoming wrong.
///
/// **Nor does it set what is not a preset's to set**: a parameter
/// the block keeps out of presets (dsp::ParamFlags::inPresets), such as a
/// hearing block's Ear, is left as it is even when the preset names it — a
/// preset saved before the rule, or written by hand. The registry says
/// which those are.
[[nodiscard]] std::expected<ApplyPreset, PresetError> applyPresetEdit(const dsp::BlockRegistry &registry,
                                                                      const Session &session,
                                                                      const QString &blockId,
                                                                      const Preset &preset);

/// What \a blockId is set to now, as a preset called \a name.
///
/// **Every parameter the block has, not only the ones somebody touched.** A
/// session holds a parameter only once it has been set, so a preset built
/// from the session alone would be missing everything left at its default —
/// and applying it later would leave those wherever the other block happened
/// to have them. The registry is asked for the defaults so that a preset is
/// the whole of what a person is hearing — less what the block keeps out of
/// presets (dsp::ParamFlags::inPresets).
[[nodiscard]] std::expected<Preset, PresetError>
presetOfBlock(const dsp::BlockRegistry &registry, const Session &session, const QString &blockId, const QString &name);

/// The presets as the facade sends them: `{id, name, factory, params}` each.
[[nodiscard]] QJsonArray presetsToJson(const QList<Preset> &presets);

} // namespace rb::core
