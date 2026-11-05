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

// What a Recorder shows on the canvas: the format and how it records
// — "MP3", "256 kbps · Stereo" — a REC pill with the time while it records,
// and Pause and Split buttons.
//
// **A text face, as the design draws one**, with the pill in the danger
// colour a recording light is, and the two buttons below it: they are the
// ones a person reaches for in the middle of a recording, which is why they
// are on the tile and not only in the panel.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(face.blockId) : ({})
    readonly property string format: face.params.format ?? "mp3"
    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(face.blockId).enabled ?? true)

    readonly property string recording: RecordingStatus.revision >= 0
        ? RecordingStatus.state(SessionModel.sessionId, face.blockId) : "idle"
    readonly property bool going: face.recording === "recording" || face.recording === "paused"
    readonly property bool isRecording: face.recording === "recording"
    readonly property bool isPaused: face.recording === "paused"
    /// Seconds recorded, read again every second while it records.
    property int elapsed: 0

    readonly property string headline: words.formatName(face.format)
    readonly property string caption: words.caption(face.params)
    readonly property string detail: words.statusText(face.recording, face.format,
                                                       RecordingStatus.stopReason(SessionModel.sessionId, face.blockId))

    readonly property alias headlineLabel: headlineText
    readonly property alias captionLabel: captionText
    readonly property alias pill: pill
    readonly property alias pillLabel: pillText
    readonly property alias pauseButton: pauseControl
    readonly property alias splitButton: splitControl
    readonly property alias words: words

    implicitWidth: Kirigami.Units.gridUnit * 8
    implicitHeight: Kirigami.Units.gridUnit * 5

    Accessible.role: Accessible.StaticText
    Accessible.name: face.headline
    Accessible.description: face.going ? face.detail + " " + RecordingStatus.clock(face.elapsed) : face.caption

    RecorderWords {
        id: words
    }

    function refresh() {
        face.elapsed = RecordingStatus.elapsedSeconds(SessionModel.sessionId, face.blockId);
    }

    onRecordingChanged: face.refresh()
    Component.onCompleted: face.refresh()

    Timer {
        interval: 1000
        repeat: true
        running: face.recording === "recording"
        onTriggered: face.refresh()
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

        RowLayout {
            Layout.fillWidth: true
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

            // **REC and the time, while it records**; the same pill, in the
            // muted colour, while it is paused.
            Rectangle {
                id: pill

                visible: face.going
                radius: height / 2
                color: face.recording === "recording" ? AppColors.dangerFill : AppColors.well
                implicitWidth: pillText.implicitWidth + AppMetrics.faceSpacing * 2
                implicitHeight: pillText.implicitHeight + 2

                QQC2.Label {
                    id: pillText

                    anchors.centerIn: parent
                    text: face.isRecording
                        ? KI18n.i18nc("@info:status a recording light and its time, 'REC 00:12:04'", "REC %1",
                                      RecordingStatus.clock(face.elapsed))
                        : KI18n.i18nc("@info:status a paused recording and its time", "PAUSED %1",
                                      RecordingStatus.clock(face.elapsed))
                    font: AppFonts.tileCaption
                    // White on the red that clears 4.5:1 under it.
                    color: face.recording === "recording" ? AppColors.inkOnDanger : AppColors.text
                    Accessible.ignored: true
                }
            }
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

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.faceSpacing

            QQC2.Button {
                id: pauseControl

                text: face.isPaused ? KI18n.i18nc("@action:button resume a paused recording", "Resume")
                                              : KI18n.i18nc("@action:button pause a recording", "Pause")
                font: AppFonts.tileCaption
                enabled: face.going && face.blockEnabled
                flat: true
                horizontalPadding: AppMetrics.smallSpacing
                Layout.fillWidth: true
                // Half a tile is little room for a translation; the
                // word is whole on hover.
                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered && implicitContentWidth > availableWidth
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                onClicked: face.recording === "paused" ? RecordingStatus.resume(SessionModel.sessionId, face.blockId)
                                                   : RecordingStatus.pause(SessionModel.sessionId, face.blockId)
            }

            QQC2.Button {
                id: splitControl

                text: KI18n.i18nc("@action:button end this recording's file and start the next", "Split")
                font: AppFonts.tileCaption
                enabled: face.recording === "recording" && face.blockEnabled
                flat: true
                horizontalPadding: AppMetrics.smallSpacing
                Layout.fillWidth: true
                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered && implicitContentWidth > availableWidth
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                onClicked: RecordingStatus.split(SessionModel.sessionId, face.blockId)
            }
        }
    }
}
