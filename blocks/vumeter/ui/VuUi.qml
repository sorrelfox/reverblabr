// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

/// The VU block's interface: the face its tile draws, the panel its
/// settings open in and its help page, as BlockRegister lists them
/// (ADR-0035). One it leaves out is the generic one.
BlockUi {
    typeIds: ["meter.vu"]

    /// The VU face: two small dials, as the design draws its VU tile.
    face: VuFace {}

    /// The VU panel: two dials with their marks, and which level
    /// reads 0 VU — a dial is a shape, not a list of settings.
    panel: VuPanel {}

    help: VuHelp {}
}
