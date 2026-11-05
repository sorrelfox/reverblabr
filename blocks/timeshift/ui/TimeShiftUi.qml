// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

/// The Time Shift block's interface: the face its tile draws, the panel its
/// settings open in and its help page, as BlockRegister lists them
/// (ADR-0035). One it leaves out is the generic one.
BlockUi {
    typeIds: ["advanced.timeshift"]

    /// The Time Shift face: where the play head is, and pause and ten
    /// seconds either way under it.
    face: TimeShiftFace {}

    /// The Time Shift panel: the position as a scrubber, the whole
    /// transport, and how many minutes to keep.
    panel: TimeShiftPanel {}

    help: TimeShiftHelp {}
}
