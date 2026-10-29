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

// One of the Mixer's five strips: what is wired into it, and its
// fade down, mute, volume, fade up, solo and balance, in the order the
// design lays them out.
//
// **A strip with nothing wired in is shown, dimmed**, so the five inputs
// on the tile and the five strips here are always the same five.
ColumnLayout {
    id: strip

    /// Which strip, from nought.
    required property int index
    /// The panel, which does the sending.
    required property var mixer

    readonly property string suffix: String(strip.index + 1)
    readonly property real volume: strip.mixer.number("volume" + strip.suffix, 1)
    readonly property bool muted: strip.mixer.flag("mute" + strip.suffix)
    readonly property bool soloed: strip.mixer.flag("solo" + strip.suffix)
    readonly property real balance: strip.mixer.number("balance" + strip.suffix, 0)
    readonly property string source: strip.mixer.sourceOf(strip.index)

    /// The controls, for a test.
    readonly property alias title: titleText
    readonly property alias volumeSlider: volumeControl
    readonly property alias volumeReadout: volumeText
    readonly property alias muteButton: muteControl
    readonly property alias soloButton: soloControl
    readonly property alias fadeDownButton: fadeDown
    readonly property alias fadeUpButton: fadeUp
    readonly property alias balanceSlider: balanceControl

    /// A button's width with no more room than its words need: the Basic
    /// style never draws one narrower than 100 pixels otherwise.
    function fitted(button): real {
        if (!button) {
            return -1;
        }
        return Math.min(button.implicitWidth,
                        Math.ceil(button.implicitContentWidth + button.leftPadding + button.rightPadding));
    }

    /// **Folded onto three lines where one does not fit**: in the
    /// card the smallest window leaves, a strip's controls asked for 428
    /// pixels in a body of 245, and in the Basic style, where a button is
    /// never narrower than 100, for 568. Folded, the level is on one line
    /// (mute, volume, its number), the moves on the next (the two fades and
    /// Solo, each no wider than its words) and the balance on the third.
    /// The width is the one line's own, not the layout's, so folding cannot
    /// feed back into whether to fold.
    readonly property real oneLineWidth: fadeDown.implicitWidth + muteControl.implicitWidth
        + volumeControl.Layout.preferredWidth + volumeText.Layout.preferredWidth + fadeUp.implicitWidth
        + soloFrame.implicitWidth + leftEnd.implicitWidth + balanceControl.Layout.preferredWidth
        + rightEnd.implicitWidth + controls.columnSpacing * 8
    readonly property bool folded: strip.width < strip.oneLineWidth

    Layout.fillWidth: true
    spacing: AppMetrics.hairSpacing
    opacity: strip.source === "" ? 0.55 : 1.0

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: strip.suffix
            color: AppColors.muted
            font.family: AppFonts.monoFamily
            Layout.preferredWidth: Kirigami.Units.gridUnit
            Accessible.ignored: true
        }

        QQC2.Label {
            id: titleText

            text: strip.source !== "" ? strip.source
                                      : KI18n.i18nc("@info a Mixer strip nothing is wired into", "No source")
            font.bold: strip.source !== ""
            font.italic: strip.source === ""
            color: AppColors.text
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }

    // One line in the design's order while it fits; folded, three lines
    // whose columns line up: mute, fade to 0 % and L on the left, the two
    // sliders and fade to 100 % in the middle, the number, Solo and R on
    // the right.
    GridLayout {
        id: controls

        Layout.fillWidth: true
        columnSpacing: Kirigami.Units.smallSpacing
        rowSpacing: AppMetrics.hairSpacing

        QQC2.Button {
            id: fadeDown

            Layout.row: strip.folded ? 1 : 0
            Layout.column: 0
            Layout.preferredWidth: strip.folded ? strip.fitted(fadeDown) : -1
            Layout.minimumWidth: strip.folded ? strip.fitted(fadeDown) : -1

            text: KI18n.i18nc("@action:button fade this Mixer strip to silence", "↘ 0 %")
            font: AppFonts.small
            Accessible.name: KI18n.i18nc("@action:button", "Fade input %1 to 0 %", strip.suffix)
            QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip", "Fade to 0 % over half a second; with Shift, two seconds")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            onClicked: strip.mixer.fadeStrips([strip.index], 0, strip.mixer.slowHeld())
        }

        QQC2.ToolButton {
            id: muteControl

            Layout.row: 0
            Layout.column: strip.folded ? 0 : 1

            icon.name: strip.muted ? "audio-volume-muted" : "audio-volume-high"
            display: QQC2.AbstractButton.IconOnly
            checkable: true
            Accessible.name: KI18n.i18nc("@action:button silence one Mixer strip", "Mute input %1", strip.suffix)
            QQC2.ToolTip.text: Accessible.name
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            onToggled: SessionModel.setParam(strip.mixer.blockId, "mute" + strip.suffix, muteControl.checked)

            SessionBound {
                control: muteControl
                property: "checked"
                value: strip.muted
            }
        }

        QQC2.Slider {
            id: volumeControl

            Layout.row: 0
            Layout.column: strip.folded ? 1 : 2
            Layout.fillWidth: true
            Layout.preferredWidth: Kirigami.Units.gridUnit * 4
            Layout.minimumWidth: Kirigami.Units.gridUnit * 3
            from: 0
            to: 200
            Accessible.name: KI18n.i18nc("@label:slider", "Volume of input %1", strip.suffix)

            onMoved: SessionModel.setParam(strip.mixer.blockId, "volume" + strip.suffix, value / 100, true)
            onPressedChanged: if (!pressed) {
                SessionModel.commitParam(strip.mixer.blockId, "volume" + strip.suffix);
            }

            SessionBound {
                control: volumeControl
                property: "value"
                value: strip.volume * 100
                suspended: volumeControl.pressed
            }
        }

        QQC2.Label {
            id: volumeText

            Layout.row: 0
            Layout.column: strip.folded ? 2 : 3

            text: KI18n.i18nc("@info a volume as a percentage", "%1 %", Math.round(volumeControl.value))
            color: strip.muted ? AppColors.muted : AppColors.accentText
            font.family: AppFonts.monoFamily
            horizontalAlignment: Text.AlignRight
            // Room for a longer unit in another language.
            Layout.preferredWidth: Math.max(Kirigami.Units.gridUnit * 2.5, implicitWidth)
        }

        QQC2.Button {
            id: fadeUp

            Layout.row: strip.folded ? 1 : 0
            Layout.column: strip.folded ? 1 : 4
            Layout.preferredWidth: strip.folded ? strip.fitted(fadeUp) : -1
            Layout.minimumWidth: strip.folded ? strip.fitted(fadeUp) : -1

            text: KI18n.i18nc("@action:button fade this Mixer strip up to its full level", "↗ 100 %")
            font: AppFonts.small
            Accessible.name: KI18n.i18nc("@action:button", "Fade input %1 to 100 %", strip.suffix)
            QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip", "Fade to 100 % over half a second; with Shift, two seconds")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            onClicked: strip.mixer.fadeStrips([strip.index], 1, strip.mixer.slowHeld())
        }

        // A frame round the one choice, as a segmented row is drawn, so an
        // unchosen Solo reads as a button and not as a word.
        SegmentedFrame {
            id: soloFrame

            Layout.row: strip.folded ? 1 : 0
            Layout.column: strip.folded ? 2 : 5

            SegmentedChoice {
                id: soloControl

                text: KI18n.i18nc("@action:button hear only this Mixer strip and any others soloed", "Solo")
                font: AppFonts.small
                Accessible.name: KI18n.i18nc("@action:button", "Solo input %1", strip.suffix)
                QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip", "Hear this input and any others soloed; with Alt, only this one")
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                onClicked: strip.mixer.toggleSolo(strip.index, strip.mixer.exclusiveHeld())

                SessionBound {
                    control: soloControl
                    property: "checked"
                    value: strip.soloed
                }
            }
        }

        QQC2.Label {
            id: leftEnd

            Layout.row: strip.folded ? 2 : 0
            Layout.column: strip.folded ? 0 : 6
            // Folded, the L sits against the slider it names, under the
            // wider fade button.
            Layout.alignment: strip.folded ? Qt.AlignRight | Qt.AlignVCenter : Qt.AlignVCenter
            text: KI18n.i18nc("@label the left end of a balance slider", "L")
            color: AppColors.muted
            font: AppFonts.small
            Accessible.ignored: true
        }

        QQC2.Slider {
            id: balanceControl

            Layout.row: strip.folded ? 2 : 0
            Layout.column: strip.folded ? 1 : 7
            Layout.fillWidth: strip.folded
            Layout.preferredWidth: Kirigami.Units.gridUnit * 3.5
            // As the volume slider: a style's own width for a slider is far
            // more than a strip has (200 in Basic).
            Layout.minimumWidth: Kirigami.Units.gridUnit * 2.5
            from: -1
            to: 1
            Accessible.name: KI18n.i18nc("@label:slider", "Balance of input %1", strip.suffix)

            onMoved: SessionModel.setParam(strip.mixer.blockId, "balance" + strip.suffix, value, true)
            onPressedChanged: if (!pressed) {
                SessionModel.commitParam(strip.mixer.blockId, "balance" + strip.suffix);
            }

            SessionBound {
                control: balanceControl
                property: "value"
                value: strip.balance
                suspended: balanceControl.pressed
            }
        }

        QQC2.Label {
            id: rightEnd

            Layout.row: strip.folded ? 2 : 0
            Layout.column: strip.folded ? 2 : 8
            text: KI18n.i18nc("@label the right end of a balance slider", "R")
            color: AppColors.muted
            font: AppFonts.small
            Accessible.ignored: true
        }
    }
}
