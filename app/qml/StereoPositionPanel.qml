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

// The settings of a block with one left-to-right setting: Balance's,
// and any block that places sound between the two sides the same way.
//
// **As the design draws it**: the setting's name and where it is on one
// line, the number in the accent at the end where the eye lands, the slider
// under them at the panel's full width, and "L" and "R" under its two ends
// so the direction needs no explaining.
//
// **The generic panel could draw a slider from −100 to 100**, but not say
// "L 30": a number with a sign on it is the engine's spelling, not a side.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""
    /// The parameter the slider moves, −100 … +100.
    property string paramId: ""
    /// The setting's name: "Balance".
    property string title: ""

    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(panel.blockId) : ({})
    readonly property real position: panel.params[panel.paramId] ?? 0

    /// The controls themselves, so that a test can drive what a person
    /// drives rather than the function behind it.
    readonly property alias slider: positionSlider
    readonly property alias positionLabel: positionText
    readonly property alias titleLabel: titleText
    readonly property alias leftEnd: leftText
    readonly property alias rightEnd: rightText

    spacing: AppMetrics.panelRowGap

    StereoPositionWords {
        id: words
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            id: titleText

            text: panel.title
            color: AppColors.text
            Layout.fillWidth: true
        }

        QQC2.Label {
            id: positionText

            // **The slider's own value, not the session's**, so the number
            // follows a drag rather than jumping when it ends.
            text: words.longText(positionSlider.value)
            color: AppColors.accentText
            font.family: AppFonts.monoFamily
            horizontalAlignment: Text.AlignRight
        }
    }

    QQC2.Slider {
        id: positionSlider

        Layout.fillWidth: true
        // Close under its name, as one row with it.
        Layout.topMargin: AppMetrics.pullIntoRow
        from: -100
        to: 100
        stepSize: 1
        snapMode: QQC2.Slider.SnapAlways

        Accessible.name: panel.title
        Accessible.description: positionText.text

        onMoved: SessionModel.setParam(panel.blockId, panel.paramId, value, true)
        onPressedChanged: if (!pressed) {
            SessionModel.commitParam(panel.blockId, panel.paramId);
        }

        SessionBound {
            control: positionSlider
            property: "value"
            value: panel.position
            // A drag is the one time the pointer, not the session, says
            // where the handle is.
            suspended: positionSlider.pressed
        }
    }

    // **The two ends, named**, close under the slider as the design has
    // them: which way is left is obvious on a speaker and not on a line.
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.pullIntoRow

        QQC2.Label {
            id: leftText

            text: KI18n.i18nc("@label the left end of a Balance or Pan slider", "L")
            font: AppFonts.small
            color: AppColors.muted
            Accessible.ignored: true
            Layout.fillWidth: true
        }

        QQC2.Label {
            id: rightText

            text: KI18n.i18nc("@label the right end of a Balance or Pan slider", "R")
            font: AppFonts.small
            color: AppColors.muted
            horizontalAlignment: Text.AlignRight
            Accessible.ignored: true
        }
    }

    // **The presets row is the settings panel's**, not this one's.
}
