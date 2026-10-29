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

// The FIR Filter block's tile: the impulse response's file name over
// its length, or that there is none or that the file cannot be used.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(face.blockId).enabled ?? true)
    readonly property alias reading: reading
    readonly property alias headlineLabel: headlineText
    readonly property alias captionLabel: captionText
    /// For the tooltip.
    readonly property string detail: reading.description !== "" ? reading.description : reading.detail

    implicitWidth: Kirigami.Units.gridUnit * 8
    implicitHeight: Kirigami.Units.gridUnit * 5

    Accessible.role: Accessible.StaticText
    Accessible.name: reading.headline
    Accessible.description: reading.detail

    FirFilterState {
        id: reading

        blockId: face.blockId
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

            text: reading.headline
            font: AppFonts.tileText
            color: face.blockEnabled && reading.file !== "" && !reading.unusable ? AppColors.text : AppColors.muted
            // A file name's end is its extension; the start is what tells
            // one from another.
            elide: Text.ElideMiddle
            Layout.fillWidth: true
            Accessible.ignored: true
        }

        ElidedLabel {
            id: captionText

            text: reading.detail
            font: AppFonts.tileCaption
            color: reading.unusable ? AppColors.warningText : AppColors.muted
            elide: Text.ElideRight
            Layout.fillWidth: true
            Accessible.ignored: true
        }
    }
}
