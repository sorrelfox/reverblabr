// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

/// The Multiband Compressor block's interface: the face its tile draws, the
/// panel its settings open in and its help page, as BlockRegister lists
/// them (ADR-0035). One it leaves out is the generic one.
BlockUi {
    typeIds: ["effect.multibandcompressor"]

    /// The Multiband Compressor face: each band's gain reduction
    /// hanging in its well, and where the bands meet.
    face: MultibandCompressorFace {}

    /// The Multiband Compressor panel: the bands with their splits
    /// to drag, the picked band's curve, and its controls.
    panel: MultibandCompressorPanel {}

    help: MultibandCompressorHelp {}
}
