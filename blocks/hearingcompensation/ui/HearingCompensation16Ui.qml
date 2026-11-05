// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

/// The Hearing Compensation 16 block's interface: the face its tile draws,
/// the panel its settings open in and its help page, as BlockRegister lists
/// them (ADR-0035). One it leaves out is the generic one.
BlockUi {
    typeIds: ["hearing.compensation16"]

    /// The Hearing Compensation face: the lift for soft and for
    /// average sound across frequency, small.
    face: HearingCompensationFace {}

    /// The Hearing Compensation panel: the gain and levels views, a
    /// handle on every band, the band inspector, the audiogram and its fit.
    panel: HearingCompensationPanel {}

    help: HearingCompensation16Help {}
}
