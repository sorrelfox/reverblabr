// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

/// The System Audio block's interface: the face its tile draws, the panel
/// its settings open in and its help page, as BlockRegister lists them
/// (ADR-0035). One it leaves out is the generic one.
BlockUi {
    typeIds: ["source.system"]

    /// The System Audio face: the icon kind the block declares, with the
    /// one line the generic icon face cannot know — how much of the machine
    /// the block has been told to take.
    face: SystemAudioFace {}

    /// The System Audio panel: an exclusion list of match rules, which is
    /// not a list of settings — the generic panel would offer a row of
    /// strings to type, and the strings are the engine's spelling rather
    /// than anything a person knows.
    panel: SystemAudioPanel {}

    help: SystemAudioHelp {}
}
