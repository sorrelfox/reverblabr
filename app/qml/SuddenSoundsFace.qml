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
import org.kde.kirigami as Kirigami
import org.kde.ki18n

// What a Sudden Sounds shows on the canvas: what it takes off now as a number,
// held a second so that a cut of 2 ms can be seen, and the ear and Strength
// under it. No slider: four words on the card are all it has.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property SuddenSoundsState reading: SuddenSoundsState {
        blockId: face.blockId
    }
    readonly property SuddenSoundsWords words: face.reading.words

    readonly property string kind: face.reading.kind
    readonly property string dot: face.words.dotOf(face.kind)
    readonly property string headline: face.words.headlineOf(face.kind, face.reading.softenedDb)
    readonly property string caption: face.words.captionOf(face.kind, face.reading.ear, face.reading.strength)
    readonly property color tint: face.reading.blockEnabled
        ? AppColors.categoryTint(BlockPresentation.category("hearing.suddensounds"))
        : AppColors.muted

    readonly property alias headlineLabel: headlineText
    readonly property alias captionLabel: captionText

    implicitWidth: Kirigami.Units.gridUnit * 10
    implicitHeight: Kirigami.Units.gridUnit * 4

    ColumnLayout {
        anchors {
            left: parent.left
            right: parent.right
            verticalCenter: parent.verticalCenter
            leftMargin: Kirigami.Units.smallSpacing * 3
            rightMargin: Kirigami.Units.smallSpacing * 3
        }
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            id: headlineText

            Layout.fillWidth: true
            text: face.headline
            font: AppFonts.readout
            lineHeightMode: AppMetrics.followsSystem ? Text.ProportionalHeight : Text.FixedHeight
            lineHeight: AppMetrics.followsSystem ? 1.0 : AppFonts.readoutPixels
            color: face.kind === "ready" || face.kind === "softening" ? face.tint : AppColors.muted
            Accessible.ignored: true
        }

        QQC2.Label {
            id: captionText

            Layout.fillWidth: true
            visible: face.caption !== ""
            text: face.caption
            font: AppFonts.tileCaption
            color: face.kind === "softening" ? AppColors.liveText : AppColors.muted
            elide: Text.ElideRight
            Accessible.ignored: true
        }
    }
}
