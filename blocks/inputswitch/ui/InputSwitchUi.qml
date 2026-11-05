// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

/// The Input Switch block's interface: the face its tile draws, the panel
/// its settings open in and its help page, as BlockRegister lists them
/// (ADR-0035). One it leaves out is the generic one.
BlockUi {
    typeIds: ["advanced.inputswitch"]

    /// The Input Switch face: A and B, the one passing chosen, and a
    /// click on the other to switch.
    face: InputSwitchFace {}

    /// The Input Switch panel: the two inputs and a switch standing
    /// between them.
    panel: InputSwitchPanel {}

    help: InputSwitchHelp {}
}
