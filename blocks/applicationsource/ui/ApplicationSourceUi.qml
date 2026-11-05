// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

/// The Application block's interface: the face its tile draws, the panel
/// its settings open in and its help page, as BlockRegister lists them
/// (ADR-0035). One it leaves out is the generic one.
BlockUi {
    typeIds: ["source.application"]

    /// The Application face: the program's own icon and name, and whether
    /// anything is coming out of it — neither of which any generic face
    /// could know to ask the inventory for.
    face: ApplicationSourceFace {}

    /// The Application panel: which program the block listens to, which is
    /// a match rule chosen from what is playing rather than three engine
    /// spellings typed into three boxes.
    panel: ApplicationSourcePanel {}

    help: ApplicationSourceHelp {}
}
