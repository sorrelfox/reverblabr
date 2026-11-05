// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/blockregistry.h"

namespace rb::blocks
{

/// Puts every block this build has into \a registry.
///
/// **One place that knows what exists.** The application, the tests and
/// anything else that runs a session all ask for the same set, so a block
/// that has been written is a block every one of them has.
///
/// False when a type id was already taken, which is a mistake in the
/// registration rather than something a user can cause.
[[nodiscard]] bool registerBuiltinBlocks(dsp::BlockRegistry &registry);

} // namespace rb::blocks
