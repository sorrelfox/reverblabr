// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

/// The Volume block's interface: the face its tile draws, the panel its
/// settings open in and its help page, as BlockRegister lists them
/// (ADR-0035). One it leaves out is the generic one.
BlockUi {
    typeIds: ["effect.volume"]

    face: VolumeFace {}

    /// The Volume panel: a percentage over an amplitude, and a setting that
    /// moves the top of a slider — neither of which the generic panel can
    /// know about.
    panel: VolumePanel {}

    help: VolumeHelp {}
}
