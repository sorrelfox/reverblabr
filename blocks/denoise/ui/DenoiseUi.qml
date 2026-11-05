// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

/// The Denoise block's interface: the face its tile draws, the panel its
/// settings open in and its help page, as BlockRegister lists them
/// (ADR-0035). One it leaves out is the generic one.
BlockUi {
    typeIds: ["advanced.denoise"]

    /// The Denoise face: whether it has a noise to take out, and how
    /// much of it was learned.
    face: DenoiseFace {}

    /// The Denoise panel: the state and the Learn or Forget button,
    /// which no generated panel could offer, and its two settings.
    panel: DenoisePanel {}

    help: DenoiseHelp {}
}
