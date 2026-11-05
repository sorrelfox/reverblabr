// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

/// The Speech Denoise block's interface: the face its tile draws, the panel
/// its settings open in and its help page, as BlockRegister lists them
/// (ADR-0035). One it leaves out is the generic one.
BlockUi {
    typeIds: ["advanced.speechdenoise"]

    /// The Speech Denoise face: the block's icon over the model it
    /// runs, or that it only works at 48 kHz.
    face: SpeechDenoiseFace {}

    /// The Speech Denoise panel: whether it hears speech, which no
    /// generated panel could show, and its two settings.
    panel: SpeechDenoisePanel {}

    help: SpeechDenoiseHelp {}
}
