// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

/// The 10-Band EQ block's interface: the face its tile draws, the panel its
/// settings open in and its help page, as BlockRegister lists them
/// (ADR-0035). One it leaves out is the generic one.
BlockUi {
    typeIds: ["effect.equalizer10"]

    /// The 10-Band EQ face: its ten bands as ten bars.
    face: EqualizerFace {}

    /// The 10-Band EQ panel: ten upright sliders under a dB scale,
    /// the preamp, keeping the level, and Flatten.
    panel: EqualizerPanel {}

    help: EqualizerHelp {}
}
