// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

/// The Parametric EQ block's interface: the face its tile draws, the panel
/// its settings open in and its help page, as BlockRegister lists them
/// (ADR-0035). One it leaves out is the generic one.
BlockUi {
    typeIds: ["advanced.parametriceq"]

    /// The Parametric EQ face: its response, small.
    face: ParametricEqFace {}

    /// The Parametric EQ panel: the response with a handle on each
    /// band, a chip to pick a band by, and the picked band's settings.
    panel: ParametricEqPanel {}

    help: ParametricEqHelp {}
}
