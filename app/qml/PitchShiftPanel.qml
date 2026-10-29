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

// The Pitch Shift's settings: whole semitones, then cents on top, each
// a slider with its number, and what the block does.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(panel.blockId) : ({})
    readonly property int semitones: panel.params.semitones ?? 0
    readonly property real cents: panel.params.cents ?? 0

    /// The block's range; tst_pitchshift.qml holds it against the catalogue.
    readonly property int widestSemitones: 12
    readonly property real widestCents: 100

    /// The controls, so that a test drives what a person drives.
    readonly property alias semitonesSlider: semitonesSlider
    readonly property alias centsSlider: centsSlider
    readonly property alias semitonesLabel: semitonesText
    readonly property alias centsLabel: centsText
    readonly property alias shiftLabel: shiftText

    spacing: AppMetrics.panelRowGap

    PitchShiftWords {
        id: words
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label:slider how far the pitch is moved, in whole semitones", "Semitones")
            color: AppColors.text
            Layout.fillWidth: true
        }

        QQC2.Label {
            id: semitonesText

            text: words.semitones(semitonesSlider.value)
            color: AppColors.accentText
            font.family: AppFonts.monoFamily
            horizontalAlignment: Text.AlignRight
        }
    }

    QQC2.Slider {
        id: semitonesSlider

        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.pullIntoRow
        from: -panel.widestSemitones
        to: panel.widestSemitones
        stepSize: 1
        snapMode: QQC2.Slider.SnapAlways
        Accessible.name: KI18n.i18nc("@label:slider", "Semitones")
        Accessible.description: semitonesText.text

        onMoved: SessionModel.setParam(panel.blockId, "semitones", Math.round(value), true)
        onPressedChanged: if (!pressed) {
            SessionModel.commitParam(panel.blockId, "semitones");
        }

        SessionBound {
            control: semitonesSlider
            property: "value"
            value: panel.semitones
            suspended: semitonesSlider.pressed
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.pullIntoRow

        QQC2.Label {
            text: KI18n.i18nc("@label the low end of the Pitch Shift's semitones", "An octave down")
            color: AppColors.muted
            font: AppFonts.small
            Layout.fillWidth: true
        }

        QQC2.Label {
            text: KI18n.i18nc("@label the high end of the Pitch Shift's semitones", "An octave up")
            color: AppColors.muted
            font: AppFonts.small
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label:slider fine tuning of the pitch, in hundredths of a semitone", "Cents")
            color: AppColors.text
            Layout.fillWidth: true
        }

        QQC2.Label {
            id: centsText

            text: words.cents(centsSlider.value)
            color: AppColors.accentText
            font.family: AppFonts.monoFamily
            horizontalAlignment: Text.AlignRight
        }
    }

    QQC2.Slider {
        id: centsSlider

        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.pullIntoRow
        from: -panel.widestCents
        to: panel.widestCents
        stepSize: 1
        snapMode: QQC2.Slider.SnapAlways
        Accessible.name: KI18n.i18nc("@label:slider", "Cents")
        Accessible.description: centsText.text

        onMoved: SessionModel.setParam(panel.blockId, "cents", value, true)
        onPressedChanged: if (!pressed) {
            SessionModel.commitParam(panel.blockId, "cents");
        }

        SessionBound {
            control: centsSlider
            property: "value"
            value: panel.cents
            suspended: centsSlider.pressed
        }
    }

    QQC2.Label {
        id: shiftText

        text: KI18n.i18nc("@info the whole shift of a Pitch Shift, '+3.25 st'", "Moved by %1 in all", words.shift(panel.semitones + panel.cents / 100))
        color: AppColors.text
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    InfoLine {
        text: KI18n.i18nc("@info what the Pitch Shift block does",
                          "Moves the pitch up or down without the sound running faster or slower. A semitone is one step on a piano; twelve are an octave. The sound comes out a little late, and a change of pitch takes a moment to cross over.")
    }
}
