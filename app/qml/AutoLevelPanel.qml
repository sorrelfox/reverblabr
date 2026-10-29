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

// Auto Level's settings: on or off, a line saying what it does, and —
// folded away — how loud it aims for and how far it may lift.
//
// **On or off is the whole of it for most people**, as the design has it:
// a switch and a sentence. The two numbers behind it are under "Advanced",
// for a source that needs a different level or a noisy one that should not
// be lifted as far.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(panel.blockId) : ({})
    readonly property bool boostOn: panel.params.boost ?? true
    readonly property real target: panel.params.target ?? words.defaultTarget
    readonly property real maximum: panel.params.maximumBoost ?? words.defaultMaximum

    /// The controls, so that a test drives what a person drives.
    readonly property alias boostSwitch: boostControl
    readonly property alias explanationLine: explanation
    readonly property alias advancedSection: advanced
    readonly property alias targetSlider: targetControl
    readonly property alias targetLabel: targetText
    readonly property alias maximumSlider: maximumControl
    readonly property alias maximumLabel: maximumText
    readonly property alias words: words

    spacing: AppMetrics.panelRowGap

    AutoLevelWords {
        id: words
    }

    QQC2.Switch {
        id: boostControl

        Layout.fillWidth: true
        text: KI18n.i18nc("@option:check Auto Level on or off", "Lift quiet passages")
        onToggled: SessionModel.setParam(panel.blockId, "boost", boostControl.checked)

        SessionBound {
            control: boostControl
            property: "checked"
            value: panel.boostOn
        }
    }

    InfoLine {
        id: explanation

        text: KI18n.i18nc("@info what Auto Level does",
                          "Brings quiet passages up towards a steady loudness and leaves loud ones alone. Good for a voice that comes and goes, or a source whose level keeps changing.")
    }

    CollapsibleSection {
        id: advanced

        title: KI18n.i18nc("@title:group the less usual Auto Level settings", "Advanced")

        // --- how loud it aims for ---
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                text: KI18n.i18nc("@label:slider the loudness Auto Level lifts towards", "Target loudness")
                color: AppColors.text
                Layout.fillWidth: true
            }

            QQC2.Label {
                id: targetText

                text: words.loudnessText(targetControl.value)
                color: AppColors.accentText
                font.family: AppFonts.monoFamily
            }
        }

        QQC2.Slider {
            id: targetControl

            Layout.fillWidth: true
            Layout.topMargin: AppMetrics.pullIntoRow
            from: words.lowestTarget
            to: words.highestTarget
            stepSize: 1
            snapMode: QQC2.Slider.SnapAlways
            enabled: panel.boostOn
            Accessible.name: KI18n.i18nc("@label:slider", "Target loudness")
            Accessible.description: targetText.text

            onMoved: SessionModel.setParam(panel.blockId, "target", value, true)
            onPressedChanged: if (!pressed) {
                SessionModel.commitParam(panel.blockId, "target");
            }

            SessionBound {
                control: targetControl
                property: "value"
                value: panel.target
                suspended: targetControl.pressed
            }
        }

        // --- how far it may lift ---
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: AppMetrics.panelRowGap
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                text: KI18n.i18nc("@label:slider the most Auto Level may add", "Maximum boost")
                color: AppColors.text
                Layout.fillWidth: true
            }

            QQC2.Label {
                id: maximumText

                text: words.decibelText(maximumControl.value)
                color: AppColors.accentText
                font.family: AppFonts.monoFamily
            }
        }

        QQC2.Slider {
            id: maximumControl

            Layout.fillWidth: true
            Layout.topMargin: AppMetrics.pullIntoRow
            from: 0
            to: words.highestMaximum
            stepSize: 1
            snapMode: QQC2.Slider.SnapAlways
            enabled: panel.boostOn
            Accessible.name: KI18n.i18nc("@label:slider", "Maximum boost")
            Accessible.description: maximumText.text

            onMoved: SessionModel.setParam(panel.blockId, "maximumBoost", value, true)
            onPressedChanged: if (!pressed) {
                SessionModel.commitParam(panel.blockId, "maximumBoost");
            }

            SessionBound {
                control: maximumControl
                property: "value"
                value: panel.maximum
                suspended: maximumControl.pressed
            }
        }
    }
}
