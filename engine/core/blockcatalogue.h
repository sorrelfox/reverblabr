// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/session.h"
#include "dsp/blockregistry.h"

#include <QJsonObject>

#include <map>
#include <string>
#include <utility>

namespace rb::core
{

/// What blocks this build has, and what each of them can be asked.
///
/// **The parameter descriptors are the settings panel.** A block declares
/// its parameters once — id, type, range, choices, unit, taper — and the
/// generic panel is generated from that, so a new block needs no new
/// interface code at all. The same description drives the presets, the
/// session file and the facade ([docs/architecture.md](../../docs/architecture.md)).
///
/// **No names and no descriptions.** Everything here is a stable id; the
/// words a person reads are the interface's and are translated, keyed by
/// these ids (see docs/i18n.md). That is why this can be produced by the
/// engine at all, which has no idea what language anybody speaks.
///
/// The shape is described in the interface XML, beside the signal that
/// carries it.
/// **Choices this system cannot offer**, by block type and parameter id:
/// each choice, with the reason. A Recorder's M4A formats are the
/// case: a distribution's FFmpeg may not have their encoders.
using UnavailableChoices = std::map<std::pair<std::string, std::string>, std::map<std::string, std::string>>;

/// \a unavailable choices are left out of their parameter's "choices" and
/// listed under "unavailableChoices" with their reasons, so a panel built
/// from the catalogue offers only what works and can say why the rest is
/// not there.
[[nodiscard]] QJsonObject blockCatalogueJson(const dsp::BlockRegistry &registry,
                                             const UnavailableChoices &unavailable = {});

/// Why \a wire would be refused, as a stable name, or empty when it would be
/// taken. The names are the ones the interface already spells out:
/// "wirePortOutOfRange", "wireIntoSource", "wireOutOfOutput".
///
/// **The checks applyEdit() cannot make.** A session file says which type a
/// block is, not what shape that type has, and applyEdit() is handed nothing
/// but the session — it is the same applyEdit() the interface runs on its own
/// copy to follow a patch, and a client must not need a block registry to
/// follow one. So the engine makes these checks where the registry is, before
/// the edit gets that far; without them the wire is taken and the graph
/// compiler refuses the whole session later, when somebody presses Start.
///
/// Two things are asked of the registry: whether each end names a port the
/// block can have, and whether either end is a port the audio system fills
/// — a wire into a capture block's inputs, or out of an output
/// block's, is two things writing one signal.
///
/// A block whose type this build does not have is not checked: it is kept
/// exactly as the file held it, ports and all, and guessing at its shape
/// would be inventing one.
[[nodiscard]] QString wireRefusal(const Session &session, const dsp::BlockRegistry &registry, const Wire &wire);

} // namespace rb::core
