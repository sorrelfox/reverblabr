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

// What a Simple Compressor shows on the canvas: the mode, and under it
// how hard it holds back and from where — "Gentle", "2:1 · -18 dB".
//
// **A text face, as the design draws one**: the words on the left, the line
// in the tile's semi-bold and the caption muted under it. The one control
// is a list, which is better in the panel than on a tile a drag moves.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property string mode: SessionModel.revision >= 0
        ? (SessionModel.param(face.blockId, "mode") ?? CompressorModes.defaultId)
        : CompressorModes.defaultId
    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(face.blockId).enabled ?? true)

    /// The mode's name, and what it does in numbers.
    readonly property string headline: words.modeName(face.mode)
    readonly property string caption: words.summary(face.mode)
    /// Everything there is to say, for the tooltip.
    readonly property string detail: words.explanation(face.mode)

    readonly property alias headlineLabel: headlineText
    readonly property alias captionLabel: captionText
    readonly property alias words: words

    implicitWidth: Kirigami.Units.gridUnit * 8
    implicitHeight: Kirigami.Units.gridUnit * 5

    Accessible.role: Accessible.StaticText
    Accessible.name: face.headline
    Accessible.description: face.caption

    CompressorWords {
        id: words
    }

    ColumnLayout {
        anchors {
            left: parent.left
            right: parent.right
            verticalCenter: parent.verticalCenter
            leftMargin: AppMetrics.faceMargin
            rightMargin: AppMetrics.faceMargin
        }
        spacing: AppMetrics.faceSpacing

        ElidedLabel {
            id: headlineText

            text: face.headline
            font: AppFonts.tileText
            color: face.blockEnabled ? AppColors.text : AppColors.muted
            elide: Text.ElideRight
            Layout.fillWidth: true
            Accessible.ignored: true
        }

        ElidedLabel {
            id: captionText

            text: face.caption
            font: AppFonts.tileCaption
            color: AppColors.muted
            elide: Text.ElideRight
            Layout.fillWidth: true
            Accessible.ignored: true
        }
    }
}
