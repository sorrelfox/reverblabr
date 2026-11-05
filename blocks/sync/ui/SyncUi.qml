// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

/// The Sync block's interface: the face its tile draws, the panel its
/// settings open in and its help page, as BlockRegister lists them
/// (ADR-0035). One it leaves out is the generic one.
BlockUi {
    typeIds: ["advanced.sync"]

    /// The Sync face: the delay and a slider, as the design draws a value
    /// tile.
    face: SyncFace {}

    /// The Sync panel: the delay as a number to type to a hundredth
    /// of a millisecond, a square-law slider, and what it comes to in
    /// samples while the session runs.
    panel: SyncPanel {}

    help: SyncHelp {}
}
