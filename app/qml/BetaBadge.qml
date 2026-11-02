// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.ki18n

// **Says a block is a beta**, on its tile: a word in a small frame, for the
// block whose row in the presentation table carries the flag. Nothing here
// names a block, so taking the flag away takes this away.
Rectangle {
    id: badge

    /// The kind of block this is on.
    required property string typeId

    /// Whether this kind of block is a beta.
    readonly property bool shown: BlockPresentation.isBeta(badge.typeId)

    visible: badge.shown
    implicitWidth: word.implicitWidth + 2 * AppMetrics.faceSpacing
    implicitHeight: word.implicitHeight
    radius: AppMetrics.smallRadius
    color: "transparent"
    border.color: AppColors.warningText
    border.width: 1

    Accessible.role: Accessible.StaticText
    Accessible.name: BlockPresentation.betaNote()
    Accessible.ignored: !badge.shown

    QQC2.Label {
        id: word

        anchors.centerIn: parent
        text: KI18n.i18nc("@label a short mark on a block that is not finished yet", "Beta")
        font: AppFonts.tileCaption
        color: AppColors.warningText
        Accessible.ignored: true
    }
}
