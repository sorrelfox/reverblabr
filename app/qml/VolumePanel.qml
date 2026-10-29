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

// The Volume block's own settings panel.
//
// **A percentage is what a person turns.** Somebody matching one source
// against another is thinking in "half as loud" and "twice as loud", not in
// decibels, so the slider is in per cent — and what is written is the
// amplitude the audio is multiplied by, which is the same number said the
// other way.
//
// **Overdrive is the top of the slider, not a multiplier.** Choosing 4×
// does not make anything louder; it makes the slider able to. Turning it
// down again brings the stored gain with it, because a gain the slider
// cannot show is a gain nobody can undo.
ColumnLayout {
    id: panel

    /// The block whose settings these are. **Not a required property**: the
    /// panel is made from a Component in the register, which cannot see the
    /// loader's context, so the settings panel binds it once it exists.
    property string blockId: ""

    /// Re-read when the session changes, so a value set anywhere — the
    /// face, undo, a second window — is what the panel shows.
    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(panel.blockId) : ({})
    readonly property real gain: panel.params.gain ?? VolumeScale.defaultGain
    readonly property string overdrive: panel.params.overdrive ?? VolumeScale.defaultOverdrive
    readonly property bool muted: panel.params.muted === true
    readonly property real topPercent: VolumeScale.percentOf(VolumeScale.topAmplitude(panel.overdrive))

    /// The controls themselves, so that a test can drive what a person
    /// drives rather than the function behind it.
    readonly property alias slider: volumeSlider
    readonly property alias muteButton: muteControl
    readonly property alias overdriveButtons: overdriveRepeater
    readonly property alias percentLabel: percentText
    readonly property alias overdriveTitle: overdriveLabel

    /// What overdrive does, said once where somebody who wonders can find it.
    readonly property string overdriveHelp: KI18n.i18nc("@info:tooltip",
        "Overdrive raises the top of the slider, not the volume. Turning it down brings the volume with it.")

    spacing: AppMetrics.panelRowGap

    // The name a person reads for one of the overdrive settings. **Not from
    // the engine**: the catalogue holds stable ids and the words are the
    // interface's, translated and keyed by them.
    function overdriveLabel(choiceId: string): string {
        switch (choiceId) {
        case "off":
            return KI18n.i18nc("@option:check the volume slider stops at 100 %", "Off");
        case "2x":
            return KI18n.i18nc("@option:check the volume slider goes up to 200 %", "2×");
        case "3x":
            return KI18n.i18nc("@option:check the volume slider goes up to 300 %", "3×");
        case "4x":
            return KI18n.i18nc("@option:check the volume slider goes up to 400 %", "4×");
        }
        return choiceId;
    }

    /// Chooses an overdrive setting and brings the gain within it.
    ///
    /// **The stored gain comes down with the top.** The engine clamps what
    /// it is sent, so writing the smaller number here is all it takes for
    /// the slider to follow — and it means what is saved is what is heard.
    function chooseOverdrive(choiceId: string) {
        const current = panel.gain;
        SessionModel.setParam(panel.blockId, "overdrive", choiceId);
        const clamped = VolumeScale.clampedGain(current, choiceId);
        if (clamped !== current) {
            SessionModel.setParam(panel.blockId, "gain", clamped);
        }
    }

    // --- the volume itself, as the design draws it ---
    //
    // **The name and the number on one line, the slider under them** at the
    // panel's full width: the number is what somebody reads, so it is in the
    // accent and at the end of the line where the eye lands, and the slider
    // gets every pixel there is to aim with.
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            id: volumeTitle

            text: KI18n.i18nc("@label:slider", "Volume")
            color: AppColors.text
            Layout.fillWidth: true
        }

        QQC2.Label {
            id: percentText

            // **The slider's own value, not the session's.** A knob being
            // turned is not news the rest of the window hears about until
            // the gesture ends, and a number that stood still through a
            // drag and then jumped would be a number nobody could aim with.
            //
            // Whole per cent: a tenth of one is below what anybody can hear
            // and below what the slider can aim at.
            text: KI18n.i18nc("@info a gain as a percentage of the signal that arrived", "%1 %",
                              Math.round(volumeSlider.value))
            color: panel.muted ? AppColors.muted : AppColors.accentText
            font.family: AppFonts.monoFamily
            horizontalAlignment: Text.AlignRight
        }

        // **The mute is kept**, small, beside the number it silences. The
        // design leaves it out, but it is one of the block's own settings,
        // and a panel with no way to reach it would be a setting only a
        // preset could change.
        QQC2.ToolButton {
            id: muteControl

            icon.name: panel.muted ? "audio-volume-muted" : "audio-volume-high"
            display: QQC2.AbstractButton.IconOnly
            checkable: true
            // No taller than the line it sits on in the Reverblabr look, so
            // the slider stays as close under its name as the design has it.
            implicitHeight: volumeTitle.implicitHeight
                            + (AppMetrics.followsSystem ? Kirigami.Units.smallSpacing * 2 : 0)
            implicitWidth: implicitHeight
            // Room for the whole icon on a button no taller than the line.
            Binding on padding {
                when: !AppMetrics.followsSystem
                value: 0
            }
            Accessible.name: KI18n.i18nc("@action:button silence this block", "Mute")
            QQC2.ToolTip.text: Accessible.name
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            // The ramp is the block's: it fades rather than steps, so a mute
            // during a sentence is not a click.
            onToggled: SessionModel.setParam(panel.blockId, "muted", muteControl.checked)

            SessionBound {
                control: muteControl
                property: "checked"
                value: panel.muted
            }
        }
    }

    QQC2.Slider {
        id: volumeSlider

        Layout.fillWidth: true
        // Close under its name, as one row with it.
        Layout.topMargin: AppMetrics.pullIntoRow
        from: 0
        to: panel.topPercent
        // **A muted fader still moves.** Setting the level to come back to
        // while nothing is going out is half of what a mute is for, and a
        // slider that could not be touched until the mute came off would
        // make that two gestures instead of one.

        Accessible.name: KI18n.i18nc("@label:slider", "Volume")

        // While it moves the value is live: coalesced and sent sixty times a
        // second, which is what a smoothed gain can glide to.
        onMoved: SessionModel.setParam(panel.blockId, "gain", VolumeScale.amplitudeOf(value), true)
        // And the last one always arrives, whatever the timer was doing when
        // the button came up.
        onPressedChanged: if (!pressed) {
            SessionModel.commitParam(panel.blockId, "gain");
        }

        SessionBound {
            control: volumeSlider
            property: "value"
            value: VolumeScale.percentOf(panel.gain)
            // A drag is the one time the pointer, not the session, says
            // where the handle is; the session has the last word again the
            // moment the button comes up.
            suspended: volumeSlider.pressed
        }
    }

    // --- overdrive: what the slider's top is ---
    //
    // **A label and one segmented row**, the chosen one filled in the
    // accent. What overdrive does is said in the tooltip rather than in a
    // paragraph under it: it is read once, and then it is in the way.
    RowLayout {
        id: overdriveRow

        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.followsSystem ? Kirigami.Units.smallSpacing : AppMetrics.hairSpacing
        spacing: AppMetrics.labelSpacing

        Accessible.role: Accessible.Grouping
        Accessible.name: KI18n.i18nc("@label how far above 100 % the volume slider goes", "Overdrive")

        QQC2.Label {
            id: overdriveLabel

            text: KI18n.i18nc("@label how far above 100 % the volume slider goes", "Overdrive:")
            color: AppColors.muted
            QQC2.ToolTip.text: panel.overdriveHelp
            QQC2.ToolTip.visible: overdriveHover.hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

            HoverHandler {
                id: overdriveHover
            }
        }

        SegmentedFrame {
            id: overdriveFrame

            // Across the whole row, as it was drawn: sets its own width.
            shrinkable: false
            Layout.fillWidth: true

            Repeater {
                id: overdriveRepeater

                model: VolumeScale.overdriveChoices

                delegate: SegmentedChoice {
                    id: overdriveButton

                    required property string modelData

                    text: panel.overdriveLabel(overdriveButton.modelData)
                    autoExclusive: true
                    Layout.fillWidth: true
                    Accessible.name: text
                    QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip", "The volume slider goes up to %1 %",
                                                   Math.round(VolumeScale.percentOf(
                                                       VolumeScale.topAmplitude(overdriveButton.modelData))))
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    onClicked: panel.chooseOverdrive(overdriveButton.modelData)

                    SessionBound {
                        control: overdriveButton
                        property: "checked"
                        value: panel.overdrive === overdriveButton.modelData
                    }
                }
            }
        }
    }

    // **The presets row is the settings panel's**, not this one's.
    // Every block gets the same footer, wherever its own panel ends.
}
