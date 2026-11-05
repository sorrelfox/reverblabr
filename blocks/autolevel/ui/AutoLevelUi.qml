// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

/// The Auto Level block's interface: the face its tile draws, the panel its
/// settings open in and its help page, as BlockRegister lists them
/// (ADR-0035). One it leaves out is the generic one.
BlockUi {
    typeIds: ["effect.autolevel"]

    /// The Auto Level face: the boost it is applying, and how far
    /// that is towards the most it may add.
    face: AutoLevelFace {}

    /// The Auto Level panel: a switch and a sentence, with the
    /// target and the maximum folded under "Advanced".
    panel: AutoLevelPanel {}

    help: AutoLevelHelp {}
}
