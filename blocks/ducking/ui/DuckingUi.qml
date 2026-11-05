// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

/// The Ducking block's interface: the face its tile draws, the panel its
/// settings open in and its help page, as BlockRegister lists them
/// (ADR-0035). One it leaves out is the generic one.
BlockUi {
    typeIds: ["advanced.ducking"]

    /// The Ducking face: where it starts, how far down it takes the
    /// background, and how far down the background is while it is.
    face: DuckingFace {}

    /// The Ducking panel: the threshold and the ducked volume, and
    /// the timing folded away.
    panel: DuckingPanel {}

    help: DuckingHelp {}
}
