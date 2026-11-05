// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

/// The Dehum block's interface: the face its tile draws, the panel its
/// settings open in and its help page, as BlockRegister lists them
/// (ADR-0035). One it leaves out is the generic one.
BlockUi {
    typeIds: ["advanced.dehum"]

    /// The Dehum face: the base it takes out and how much, a slider each.
    face: DehumFace {}

    /// The Dehum panel: 50 or 60 Hz, the fine tuning, the amount in
    /// decibels, and how many harmonics.
    panel: DehumPanel {}

    help: DehumHelp {}
}
