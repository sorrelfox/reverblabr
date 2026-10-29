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

// The Mono Enhancer's settings: where the width starts, and a line
// saying what the block is for, as the design draws them.
//
// **A panel of its own** because the crossover is a frequency, which wants a
// logarithmic slider and a number said in hertz and kilohertz, and because
// the line under it is what tells somebody this is for mono recordings.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(panel.blockId) : ({})
    readonly property real crossover: panel.params.crossover ?? panel.defaultHz

    readonly property real lowestHz: 100
    readonly property real highestHz: 2000
    readonly property real defaultHz: 500

    /// The controls, so that a test drives what a person drives.
    readonly property alias slider: crossoverSlider
    readonly property alias valueLabel: valueText
    readonly property alias titleLabel: titleText
    readonly property alias explanationLine: explanation

    spacing: AppMetrics.panelRowGap

    FilterWords {
        id: words
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            id: titleText

            text: KI18n.i18nc("@label:slider where a Mono Enhancer's width starts", "Crossover frequency")
            color: AppColors.text
            Layout.fillWidth: true
        }

        QQC2.Label {
            id: valueText

            // The slider's own value, so the number follows a drag.
            text: words.frequencyText(words.frequencyAt(crossoverSlider.value, panel.lowestHz, panel.highestHz))
            color: AppColors.accentText
            font.family: AppFonts.monoFamily
            horizontalAlignment: Text.AlignRight
        }
    }

    QQC2.Slider {
        id: crossoverSlider

        Layout.fillWidth: true
        // Close under its name, as one row with it.
        Layout.topMargin: AppMetrics.pullIntoRow
        from: 0
        to: 1
        Accessible.name: KI18n.i18nc("@label:slider where a Mono Enhancer's width starts", "Crossover frequency")
        Accessible.description: valueText.text

        onMoved: SessionModel.setParam(panel.blockId, "crossover",
                                       Math.round(words.frequencyAt(value, panel.lowestHz, panel.highestHz)), true)
        onPressedChanged: if (!pressed) {
            SessionModel.commitParam(panel.blockId, "crossover");
        }

        SessionBound {
            control: crossoverSlider
            property: "value"
            value: words.positionOf(panel.crossover, panel.lowestHz, panel.highestHz)
            suspended: crossoverSlider.pressed
        }
    }

    InfoLine {
        id: explanation

        text: KI18n.i18nc("@info what the Mono Enhancer does",
                          "Spreads a mono recording across the two sides above this frequency, and leaves the bass in the middle. Played in mono it sounds as it did.")
    }
}
