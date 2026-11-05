// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

/// The Output Device block's interface: the face its tile draws, the panel
/// its settings open in and its help page, as BlockRegister lists them
/// (ADR-0035). One it leaves out is the generic one.
BlockUi {
    typeIds: ["output.device"]

    /// The two device blocks share one face, and the register settles
    /// which of them it is. **"Plays" or "records" is knowable from the
    /// type id**, so it is answered here once rather than by an `if` on the
    /// type id wherever a tile is drawn — and a property bound in the
    /// register is a property a Loader never has to hand over.
    face: DeviceFace {
        output: true
    }

    /// The Output Device and the Input Device panels: a device reference is
    /// a live list, a volume is a percentage, and a channel is one of the
    /// ones *that* device has — none of which the generic panel can know.
    panel: OutputDevicePanel {}

    help: OutputDeviceHelp {}
}
