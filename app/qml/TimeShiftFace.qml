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

// What a Time Shift block shows on the canvas: where its play head
// is — "Live", "Paused", "−30 s" — as the design's text face, and under it
// the three buttons a person reaches for without opening the panel: pause,
// ten seconds back and ten forward.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(face.blockId).enabled ?? true)
    readonly property alias reading: reading
    readonly property alias headlineLabel: headlineText
    readonly property alias pauseButton: pauseControl
    readonly property alias backButton: backControl
    readonly property alias forwardButton: forwardControl

    implicitWidth: Kirigami.Units.gridUnit * 8
    implicitHeight: Kirigami.Units.gridUnit * 5

    Accessible.role: Accessible.StaticText
    Accessible.name: reading.headline

    TimeShiftState {
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
            color: face.blockEnabled && reading.live ? AppColors.text : AppColors.muted
            elide: Text.ElideRight
            Layout.fillWidth: true
            Accessible.ignored: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 0

            QQC2.Button {
                id: pauseControl

                icon.name: reading.paused ? "media-playback-start-symbolic" : "media-playback-pause-symbolic"
                display: QQC2.AbstractButton.IconOnly
                text: reading.paused ? KI18n.i18nc("@action:button play a paused Time Shift", "Play")
                                     : KI18n.i18nc("@action:button pause a Time Shift", "Pause")
                enabled: reading.live && face.blockEnabled
                flat: true
                Layout.fillWidth: true
                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                onClicked: reading.togglePause()
            }

            QQC2.Button {
                id: backControl

                text: KI18n.i18nc("@action:button jump ten seconds back, as short as the tile needs", "‹‹ 10 s")
                font: AppFonts.tileCaption
                enabled: reading.live && face.blockEnabled
                flat: true
                Layout.fillWidth: true
                Accessible.name: KI18n.i18nc("@action:button", "Jump back 10 seconds")
                // The face's own words are as short as a tile needs; the
                // whole of them is on hover.
                QQC2.ToolTip.text: Accessible.name
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                onClicked: reading.jumpBy(-10)
            }

            QQC2.Button {
                id: forwardControl

                text: KI18n.i18nc("@action:button jump ten seconds forward, as short as the tile needs", "10 s ››")
                font: AppFonts.tileCaption
                enabled: reading.live && face.blockEnabled && !reading.atLive
                flat: true
                Layout.fillWidth: true
                Accessible.name: KI18n.i18nc("@action:button", "Jump forward 10 seconds")
                // The face's own words are as short as a tile needs; the
                // whole of them is on hover.
                QQC2.ToolTip.text: Accessible.name
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                onClicked: reading.jumpBy(10)
            }
        }
    }
}
