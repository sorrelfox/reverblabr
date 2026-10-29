// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "graph/graphspec.h"

#include <string>
#include <vector>

namespace rb::graph
{

/// Which blocks the difference between two graphs can be heard through.
///
/// **An edit is faded, and this says where.** Swapping a graph steps the
/// signal from whatever the old one was putting out to whatever the new one
/// puts out, so a lane the edit changes has to be ramped down, swapped and
/// ramped back up ([ADR-0008]). A lane the edit does not reach must not be:
/// ramping it would put a dip in sound that was supposed to carry on
/// untouched, and the whole point of keeping block state across a recompile
/// is that it does carry on, sample for sample.
///
/// A block is named here when the sound leaving it can differ, which is the
/// case when the block itself has changed — it is new, or gone, or a
/// different instance, or it has been enabled or disabled — or when anything
/// that feeds it has. Wires count at the end they arrive at: a block whose
/// inputs are connected differently hears something else, and so does
/// everything downstream. A block whose **output** has been wired elsewhere
/// is not named — what leaves it depends on what arrives at it and on its
/// own state, never on who is listening.
///
/// It errs towards naming a block. Fading a lane that turns out not to have
/// changed costs a few milliseconds of dip; missing one costs a click.
///
/// The ids are those of \a after, in the order they appear in it, plus the
/// ids of blocks that \a before had and \a after does not.
[[nodiscard]] std::vector<std::string> changedBlocks(const GraphSpec &before, const GraphSpec &after);

} // namespace rb::graph
