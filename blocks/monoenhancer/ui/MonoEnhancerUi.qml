// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

/// The Mono Enhancer block's interface: the face its tile draws, the panel
/// its settings open in and its help page, as BlockRegister lists them
/// (ADR-0035). One it leaves out is the generic one.
BlockUi {
    typeIds: ["effect.monoenhancer"]

    /// The Mono Enhancer face: where the width starts, and a slider.
    face: MonoEnhancerFace {}

    /// The Mono Enhancer panel: a crossover on a logarithmic slider,
    /// and a line saying what the block is for.
    panel: MonoEnhancerPanel {}

    help: MonoEnhancerHelp {}
}
