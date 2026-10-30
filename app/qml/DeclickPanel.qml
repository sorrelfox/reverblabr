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

// The Declick block's settings, as the design lays them out: the
// threshold, with which way is more sensitive under its ends, and a line
// saying what the block does; and while the session runs, how many clicks
// it has taken out.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(panel.blockId) : ({})
    readonly property real threshold: panel.params.threshold ?? panel.defaultThreshold

    /// Which of the block's published values is the count (Declick::
    /// kClicksValue).
    readonly property int clicksValue: 0
    readonly property var levels: Meters.frame >= 0 ? Meters.levels(panel.blockId) : ({})
    readonly property var values: panel.levels.values ?? []
    readonly property bool live: Meters.live && panel.values.length > panel.clicksValue

    /// The block's range; tst_declick.qml holds it against the catalogue.
    readonly property real highestThreshold: 20
    readonly property real defaultThreshold: 3

    /// The controls, so that a test drives what a person drives.
    readonly property alias slider: thresholdSlider
    readonly property alias valueLabel: valueText
    readonly property alias clicksLabel: clicksText

    spacing: AppMetrics.panelRowGap

    DeclickWords {
        id: words
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label:slider how far a click has to stand out to be taken out", "Threshold")
            color: AppColors.text
            Layout.fillWidth: true
        }

        QQC2.Label {
            id: valueText

            text: words.decibels(thresholdSlider.value)
            color: AppColors.accentText
            font.family: AppFonts.monoFamily
            horizontalAlignment: Text.AlignRight
        }
    }

    QQC2.Slider {
        id: thresholdSlider

        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.pullIntoRow
        from: 0
        to: panel.highestThreshold
        stepSize: 0.5
        snapMode: QQC2.Slider.SnapAlways
        Accessible.name: KI18n.i18nc("@label:slider", "Threshold")
        Accessible.description: valueText.text

        onMoved: SessionModel.setParam(panel.blockId, "threshold", value, true)
        onPressedChanged: if (!pressed) {
            SessionModel.commitParam(panel.blockId, "threshold");
        }

        SessionBound {
            control: thresholdSlider
            property: "value"
            value: panel.threshold
            suspended: thresholdSlider.pressed
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.pullIntoRow

        QQC2.Label {
            text: KI18n.i18nc("@label the low end of the Declick threshold", "More sensitive")
            color: AppColors.muted
            font: AppFonts.small
            Layout.fillWidth: true
        }

        QQC2.Label {
            text: KI18n.i18nc("@label the high end of the Declick threshold", "Less sensitive")
            color: AppColors.muted
            font: AppFonts.small
        }
    }

    QQC2.Label {
        id: clicksText

        visible: panel.live
        text: panel.live ? words.clicks(Math.round(panel.values[panel.clicksValue])) : ""
        color: AppColors.text
    }

    InfoLine {
        text: KI18n.i18nc("@info what the Declick block does",
                          "Takes out clicks and pops, from a record's crackle to a digital dropout, and fills in what was underneath from the sound either side. Lower the threshold to catch fainter clicks.")
    }
}
