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

// The Model block's tile: the model's folder over what the block is doing,
// and the Beta mark, always.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(face.blockId).enabled ?? true)
    readonly property alias reading: reading
    readonly property alias headlineLabel: headlineText
    readonly property alias captionLabel: captionText
    readonly property alias betaBadge: badge
    /// For the tooltip.
    readonly property string detail: reading.silence !== "" ? reading.silence : reading.caption

    implicitWidth: Kirigami.Units.gridUnit * 8
    implicitHeight: Kirigami.Units.gridUnit * 5

    Accessible.role: Accessible.StaticText
    Accessible.name: reading.folder !== "" ? reading.folderName : reading.stateWord
    Accessible.description: face.detail

    ModelState {
        id: reading

        blockId: face.blockId
        blockEnabled: face.blockEnabled
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

        BetaBadge {
            id: badge

            typeId: "effect.model"
            Layout.alignment: Qt.AlignLeft
        }

        ElidedLabel {
            id: headlineText

            text: reading.folder !== "" ? reading.folderName : reading.stateWord
            font: AppFonts.tileText
            color: reading.sounding ? AppColors.text : AppColors.muted
            elide: Text.ElideMiddle
            Layout.fillWidth: true
            Accessible.ignored: true
        }

        ElidedLabel {
            id: captionText

            text: reading.caption
            font: AppFonts.tileCaption
            color: reading.troubled ? AppColors.warningText : AppColors.muted
            elide: Text.ElideRight
            Layout.fillWidth: true
            Accessible.ignored: true
        }
    }
}
