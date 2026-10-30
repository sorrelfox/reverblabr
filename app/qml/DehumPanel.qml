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

// The Dehum settings: the base frequency, how deep the notches go,
// and how many harmonics are taken out. And Auto, which follows a hum
// whose base drifts: Base is then where it starts looking, and the panel
// says what it is following, or that it is holding.
//
// **50 Hz and 60 Hz are one press away**, because nearly every hum is one or
// the other; the slider beside them is for the supply or the tape that runs
// a little off. The amount says what it does in decibels as well as the
// number, since "2.3" alone says nothing about how much quieter the hum is.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(panel.blockId) : ({})
    readonly property real base: panel.params.base ?? 50
    readonly property real amount: panel.params.amount ?? 5
    readonly property int harmonics: panel.params.harmonics ?? 10
    readonly property bool automatic: panel.params.auto ?? false

    /// Which of the block's published values is which (Dehum::kBaseValue,
    /// Dehum::kLockedValue).
    readonly property int baseValue: 0
    readonly property int lockedValue: 1
    readonly property var levels: Meters.frame >= 0 ? Meters.levels(panel.blockId) : ({})
    readonly property var values: panel.levels.values ?? []
    readonly property bool live: Meters.live && panel.values.length > panel.lockedValue

    /// The controls, so that a test drives what a person drives.
    readonly property alias mainsButtons: mainsFrame.row
    readonly property alias baseSlider: baseControl
    readonly property alias amountSlider: amountControl
    readonly property alias amountReadout: amountText
    readonly property alias harmonicsBox: harmonicsControl
    readonly property alias autoSwitch: autoControl
    readonly property alias autoStatus: autoStatusText
    readonly property alias words: words

    spacing: Kirigami.Units.largeSpacing

    DehumWords {
        id: words
    }

    Kirigami.FormLayout {
        Layout.fillWidth: true

        // The two supplies there are, and the fine tuning between them.
        SegmentedFrame {
            id: mainsFrame

            Kirigami.FormData.label: KI18n.i18nc("@label which mains frequency a Dehum block takes out", "Mains:")

            QQC2.ButtonGroup {
                id: mainsGroup
            }

            Repeater {
                model: [50, 60]

                delegate: SegmentedChoice {
                    required property var modelData

                    text: words.mainsText(modelData)
                    checked: Math.abs(panel.base - modelData) < 0.05
                    QQC2.ButtonGroup.group: mainsGroup
                    onClicked: SessionModel.setParam(panel.blockId, "base", modelData)
                }
            }
        }

        RowLayout {
            // "Base", not "Base frequency": the longer label made the form
            // too wide for the card, which then put every label above its
            // field. The readout says hertz.
            Kirigami.FormData.label: KI18n.i18nc("@label:slider a Dehum block's base frequency", "Base:")
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Slider {
                id: baseControl

                Layout.fillWidth: true
                from: 48
                to: 62
                stepSize: 0.1
                snapMode: QQC2.Slider.SnapAlways
                Accessible.name: KI18n.i18nc("@label:slider a Dehum block's base frequency", "Base frequency")

                onMoved: SessionModel.setParam(panel.blockId, "base", Math.round(value * 10) / 10, true)
                onPressedChanged: if (!pressed) {
                    SessionModel.commitParam(panel.blockId, "base");
                }

                SessionBound {
                    control: baseControl
                    property: "value"
                    value: panel.base
                    suspended: baseControl.pressed
                }
            }

            QQC2.Label {
                text: words.baseText(baseControl.value)
                font.family: AppFonts.monoFamily
                horizontalAlignment: Text.AlignRight
                // Room for a longer unit in another language.
                Layout.preferredWidth: Math.max(Kirigami.Units.gridUnit * 4, implicitWidth)
            }
        }

        QQC2.Switch {
            id: autoControl

            Kirigami.FormData.label: KI18n.i18nc("@label a Dehum block's Auto setting", "Auto:")
            text: KI18n.i18nc("@option:check a Dehum block follows a hum whose frequency drifts", "Follow a drifting hum")
            Accessible.name: KI18n.i18nc("@option:check", "Follow a drifting hum automatically")

            onToggled: SessionModel.setParam(panel.blockId, "auto", checked)

            SessionBound {
                control: autoControl
                property: "checked"
                value: panel.automatic
            }
        }

        // What Auto is doing, under it, for the same reason as the depth.
        QQC2.Label {
            id: autoStatusText

            Layout.fillWidth: true
            visible: panel.automatic
            text: words.autoStatus(panel.live,
                                   panel.live && panel.values[panel.lockedValue] > 0.5,
                                   panel.live ? panel.values[panel.baseValue] : panel.base)
            wrapMode: Text.WordWrap
            font: AppFonts.small
            color: AppColors.muted
        }

        RowLayout {
            Kirigami.FormData.label: KI18n.i18nc("@label:slider how much a Dehum block takes out", "Amount:")
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Slider {
                id: amountControl

                Layout.fillWidth: true
                from: 0
                to: 10
                stepSize: 0.1
                snapMode: QQC2.Slider.SnapAlways
                Accessible.name: KI18n.i18nc("@label:slider how much a Dehum block takes out", "Amount")

                onMoved: SessionModel.setParam(panel.blockId, "amount", Math.round(value * 10) / 10, true)
                onPressedChanged: if (!pressed) {
                    SessionModel.commitParam(panel.blockId, "amount");
                }

                SessionBound {
                    control: amountControl
                    property: "value"
                    value: panel.amount
                    suspended: amountControl.pressed
                }
            }

            QQC2.Label {
                text: words.amountText(amountControl.value)
                font.family: AppFonts.monoFamily
                horizontalAlignment: Text.AlignRight
                // Room for a longer unit in another language.
                Layout.preferredWidth: Math.max(Kirigami.Units.gridUnit * 4, implicitWidth)
            }
        }

        // What the amount does, under it: beside it, it made the form too
        // wide for the card, and the form put every label above its field.
        QQC2.Label {
            id: amountText

            text: words.depthText(amountControl.value)
            font: AppFonts.small
            color: AppColors.muted
        }

        QQC2.SpinBox {
            id: harmonicsControl

            Kirigami.FormData.label: KI18n.i18nc("@label:spinbox how many harmonics of the hum a Dehum block takes out", "Harmonics:")
            from: 1
            to: 10
            Accessible.name: KI18n.i18nc("@label:spinbox", "Harmonics")

            onValueModified: SessionModel.setParam(panel.blockId, "harmonics", value)

            SessionBound {
                control: harmonicsControl
                property: "value"
                value: panel.harmonics
            }
        }
    }

    QQC2.Label {
        Layout.fillWidth: true
        text: KI18n.i18nc("@info", "Takes the hum of the mains supply out of the sound, and the whine of its harmonics above it, leaving what lies between them alone.")
        wrapMode: Text.WordWrap
        font: AppFonts.small
        color: AppColors.muted
    }
}
