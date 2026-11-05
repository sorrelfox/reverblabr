// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

/// The Recorder block's interface: the face its tile draws, the panel its
/// settings open in and its help page, as BlockRegister lists them
/// (ADR-0035). One it leaves out is the generic one.
BlockUi {
    typeIds: ["output.recorder"]

    /// The Recorder: the format, how it records, a REC pill with the
    /// time while it records, and Pause and Split.
    face: RecorderFace {}

    /// The Recorder: what it is doing, Pause and Split file, and its
    /// General, Tags and Limits sections.
    panel: RecorderPanel {}

    help: RecorderHelp {}
}
