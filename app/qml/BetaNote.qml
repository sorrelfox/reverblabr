// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

// **Says a block is a beta**, on its card and its help page: a line that is
// always there and cannot be closed, for the block whose row in the
// presentation table carries the flag.
QQC2.Label {
    id: note

    /// The kind of block this is about.
    required property string typeId

    /// Whether this kind of block is a beta.
    readonly property bool shown: BlockPresentation.isBeta(note.typeId)

    visible: note.shown
    text: BlockPresentation.betaNote()
    color: AppColors.warningText
    wrapMode: Text.WordWrap
    Layout.fillWidth: true
    Accessible.role: Accessible.StaticText
    Accessible.name: note.text
}
