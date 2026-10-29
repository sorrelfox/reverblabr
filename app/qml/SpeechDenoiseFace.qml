// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// The Speech Denoise tile, as the design draws it: the block's icon
// over the model it runs — or, in a session at another rate, that it only
// works at 48 kHz.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property string typeId: "advanced.speechdenoise"
    readonly property alias reading: reading
    readonly property alias lineLabel: lineText
    /// For the tooltip.
    readonly property string detail: reading.live ? reading.model + " — " + reading.status : reading.model

    implicitWidth: Kirigami.Units.gridUnit * 8
    implicitHeight: Kirigami.Units.gridUnit * 5

    Accessible.role: Accessible.StaticText
    Accessible.name: lineText.text

    SpeechDenoiseState {
        id: reading

        blockId: face.blockId
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: AppMetrics.faceMargin
        spacing: AppMetrics.faceSpacing

        Item {
            Layout.fillHeight: true
        }

        Kirigami.Icon {
            source: BlockPresentation.icon(face.typeId)
            color: AppColors.categoryTint(BlockPresentation.category(face.typeId))
            isMask: true
            implicitWidth: AppMetrics.faceIconSize
            implicitHeight: AppMetrics.faceIconSize
            Layout.alignment: Qt.AlignHCenter
            Accessible.ignored: true
        }

        ElidedLabel {
            id: lineText

            text: reading.atWrongRate ? reading.status : reading.model
            color: reading.atWrongRate ? AppColors.warningText : AppColors.muted
            font: AppFonts.faceLine
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
