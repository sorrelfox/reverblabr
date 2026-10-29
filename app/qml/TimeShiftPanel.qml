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

// The Time Shift block's settings, as the design lays them out: the
// position as a scrubber, from the oldest audio kept at the left to live at
// the right; the whole transport under it, back 30, 10 and 3 seconds, play
// or pause, forward 3, 10 and 30, and Live; then how much to keep.
//
// **The transport needs the session running**: what is kept is what has
// arrived since it started. While it is stopped the controls wait.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(panel.blockId) : ({})

    /// The controls, so that a test drives what a person drives.
    readonly property alias reading: reading
    readonly property alias positionSlider: scrubber
    readonly property alias positionLabel: positionText
    readonly property alias pauseButton: pauseControl
    readonly property alias liveButton: liveControl
    readonly property alias minutesBox: minutesControl
    /// The jump buttons, back 30, 10 and 3 then forward 3, 10 and 30.
    readonly property var jumpButtons: [back30, back10, back3, forward3, forward10, forward30]

    /// The block's range for how much it keeps. The interface layer does not
    /// depend on the engine's, so it is written down twice; tst_timeshift.qml
    /// holds it against the catalogue.
    readonly property int longestMinutes: 60
    readonly property int defaultMinutes: 5

    spacing: AppMetrics.panelRowGap

    /// One of the jump buttons.
    component JumpButton: QQC2.Button {
        id: jump

        /// Seconds to move the head: negative back, positive forward.
        required property int seconds

        /// What the button does, in words: read out, and shown on hover.
        readonly property string longName: jump.seconds < 0
            ? KI18n.i18ncp("@action:button", "Jump back %1 second", "Jump back %1 seconds", -jump.seconds)
            : KI18n.i18ncp("@action:button", "Jump forward %1 second", "Jump forward %1 seconds", jump.seconds)

        text: KI18n.i18nc("@action:button a Time Shift jump, '10 s'", "%1 s", Math.abs(jump.seconds))
        enabled: reading.live && (jump.seconds < 0 || !reading.atLive)
        implicitWidth: Kirigami.Units.gridUnit * 2
        // **Seven to a row**: with a button's usual padding either
        // side, "10 s" had eight pixels left once a translation made it
        // longer. The words go edge to edge, and the whole of what the
        // button does is in its tooltip.
        horizontalPadding: Kirigami.Units.smallSpacing / 2
        Layout.fillWidth: true
        Accessible.name: jump.longName
        QQC2.ToolTip.text: jump.longName
        QQC2.ToolTip.visible: jump.hovered
        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
        onClicked: reading.jumpBy(jump.seconds)
    }

    TimeShiftState {
        id: reading

        blockId: panel.blockId
    }

    // --- where the head is ---
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label:slider where a Time Shift block's play head is", "Position")
            color: AppColors.text
            Layout.fillWidth: true
        }

        QQC2.Label {
            id: positionText

            text: scrubber.pressed ? reading.behind(-scrubber.value) : reading.headline
            color: AppColors.accentText
            font.family: AppFonts.monoFamily
        }
    }

    // **Live is the right end.** Dragging shows where it would go and jumps
    // there on letting go: a jump at every pixel would be a stutter of
    // crossfades nobody could follow.
    QQC2.Slider {
        id: scrubber

        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.pullIntoRow
        from: -Math.max(reading.available, 1)
        to: 0
        stepSize: 1
        enabled: reading.live
        Accessible.name: KI18n.i18nc("@label:slider", "Position")
        Accessible.description: positionText.text

        onMoved: if (!pressed) {
            reading.jumpTo(-value);
        }
        onPressedChanged: if (!pressed) {
            reading.jumpTo(-value);
        }

        // Follows the head, which moves without anybody touching anything
        // while it is paused, except while it is held.
        readonly property Connections follow: Connections {
            target: reading

            function onOffsetChanged() {
                if (!scrubber.pressed) {
                    scrubber.value = -reading.offset;
                }
            }
        }
    }

    // --- the transport ---
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            Layout.fillWidth: true

            QQC2.Label {
                text: KI18n.i18nc("@label the Time Shift buttons that go back", "← Jump back")
                color: AppColors.muted
                font: AppFonts.small
                Layout.fillWidth: true
            }

            QQC2.Label {
                text: KI18n.i18nc("@label the Time Shift buttons that go forward", "Jump forward →")
                color: AppColors.muted
                font: AppFonts.small
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing / 2

            JumpButton {
                id: back30

                seconds: -30
            }

            JumpButton {
                id: back10

                seconds: -10
            }

            JumpButton {
                id: back3

                seconds: -3
            }

            QQC2.Button {
                id: pauseControl

                icon.name: reading.paused ? "media-playback-start-symbolic" : "media-playback-pause-symbolic"
                display: QQC2.AbstractButton.IconOnly
                text: reading.paused ? KI18n.i18nc("@action:button play a paused Time Shift", "Play")
                                     : KI18n.i18nc("@action:button pause a Time Shift", "Pause")
                highlighted: true
                enabled: reading.live
                implicitWidth: Kirigami.Units.gridUnit * 3
                Layout.fillWidth: true
                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                onClicked: reading.togglePause()
            }

            JumpButton {
                id: forward3

                seconds: 3
            }

            JumpButton {
                id: forward10

                seconds: 10
            }

            JumpButton {
                id: forward30

                seconds: 30
            }

            QQC2.Button {
                id: liveControl

                text: KI18n.i18nc("@action:button back to what is arriving now, and playing", "Live")
                enabled: reading.live && (reading.paused || !reading.atLive)
                implicitWidth: Kirigami.Units.gridUnit * 2.5
                horizontalPadding: Kirigami.Units.smallSpacing / 2
                Layout.fillWidth: true
                QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip", "Back to what is arriving now, and play it")
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                onClicked: reading.goLive()
            }
        }
    }

    // --- how much is kept ---
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.panelRowGap
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label:spinbox how many minutes a Time Shift block keeps", "Minutes to keep")
            color: AppColors.text
            // A translation goes onto a second line rather than under what is
            // beside it.
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        QQC2.SpinBox {
            id: minutesControl

            from: 1
            to: panel.longestMinutes
            editable: true
            Accessible.name: KI18n.i18nc("@label:spinbox", "Minutes to keep")
            onValueModified: SessionModel.setParam(panel.blockId, "minutes", minutesControl.value)

            SessionBound {
                control: minutesControl
                property: "value"
                value: panel.params.minutes ?? panel.defaultMinutes
            }
        }
    }

    InfoLine {
        text: KI18n.i18nc("@info how the Time Shift block works",
                          "Everything that arrives is kept, so it can be paused, gone back through and caught up with. Up to ten minutes are kept in memory; more is kept on disk, in the cache folder, and gone when the session stops.")
    }
}
