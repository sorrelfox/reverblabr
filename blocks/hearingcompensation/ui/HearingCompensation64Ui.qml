// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

/// The Hearing Compensation 64 block's interface: the face its tile draws,
/// the panel its settings open in and its help page, as BlockRegister lists
/// them (ADR-0035). One it leaves out is the generic one.
BlockUi {
    typeIds: ["hearing.compensation64"]

    /// The same for Hearing Compensation 64, from the curves through
    /// its points.
    face: HearingCompensationFace {
        typeId: "hearing.compensation64"
    }

    /// Hearing Compensation 64's: the same, with a handle on each
    /// control point instead of each band.
    panel: HearingCompensationPanel {
        typeId: "hearing.compensation64"
    }

    help: HearingCompensation64Help {}
}
