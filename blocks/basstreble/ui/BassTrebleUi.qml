// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

/// The Bass & Treble block's interface: the face its tile draws, the panel
/// its settings open in and its help page, as BlockRegister lists them
/// (ADR-0035). One it leaves out is the generic one.
BlockUi {
    typeIds: ["effect.basstreble"]

    /// The Bass & Treble face: both ends' lift or cut, a slider for each.
    face: BassTrebleFace {}

    /// The Bass & Treble panel: two sliders that say their lift or
    /// cut with a sign, and go back to 0 dB on a double-click.
    panel: BassTreblePanel {}

    help: BassTrebleHelp {}
}
