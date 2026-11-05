// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

/// The Pitch Shift block's interface: the face its tile draws, the panel
/// its settings open in and its help page, as BlockRegister lists them
/// (ADR-0035). One it leaves out is the generic one.
BlockUi {
    typeIds: ["effect.pitchshift"]

    /// The Pitch Shift face: the shift, "+3 st", and a slider of
    /// whole semitones.
    face: PitchShiftFace {}

    /// The Pitch Shift panel: semitones and cents, each a slider with
    /// its number.
    panel: PitchShiftPanel {}

    help: PitchShiftHelp {}
}
