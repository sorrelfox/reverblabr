// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

/// The Frequency Raising block's interface: the face its tile draws, the
/// panel its settings open in and its help page, as BlockRegister lists
/// them (ADR-0035). One it leaves out is the generic one.
BlockUi {
    typeIds: ["hearing.frequencyraising"]

    /// The Frequency Raising face: the same as Frequency Lowering's,
    /// its moves going up.
    face: FrequencyRaisingFace {}

    /// The Frequency Raising panel: Frequency Lowering's, its moves
    /// going up, with harmonics as a third way.
    panel: FrequencyRaisingPanel {}

    help: FrequencyRaisingHelp {}
}
