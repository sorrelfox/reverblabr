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

// One of Auto Level's numbers: its name, a typed value, its unit, a slider,
// and what its lowest value means.
//
// **Two ways to one value, and the keys mean one thing.** Arrows step by 1 and
// Page keys by 3 in the field, and the slider is out of the Tab order.
ColumnLayout {
    id: row

    /// The block and the parameter this writes.
    required property string blockId
    required property string paramId
    /// What the value is called, for the eye and for a screen reader.
    required property string label
    /// The unit beside the field.
    required property string unit
    /// The value as a sentence would say it, for a screen reader.
    required property string spoken
    required property real value
    required property real from
    required property real to
    /// What is said under the row while the value is at its lowest.
    property string atLowest: ""

    /// The parts, so that a test drives what a person drives.
    readonly property alias slider: bar
    readonly property alias field: typed
    readonly property alias lowestNote: note
    readonly property bool noteShown: row.atLowest !== "" && row.value === row.from

    Layout.fillWidth: true
    spacing: 0

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: row.label
            color: row.enabled ? AppColors.text : AppColors.muted
            Layout.fillWidth: true
        }

        ValueField {
            id: typed

            Layout.preferredWidth: Kirigami.Units.gridUnit * 3
            label: row.label
            value: row.value
            from: row.from
            to: row.to
            decimals: 0
            stepSize: 1
            pageStepSize: 3
            Accessible.description: row.spoken
            onValueEdited: number => SessionModel.setParam(row.blockId, row.paramId, Math.round(number))
        }

        QQC2.Label {
            text: row.unit
            color: AppColors.muted
        }
    }

    QQC2.Slider {
        id: bar

        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.pullIntoRow
        from: row.from
        to: row.to
        stepSize: 1
        snapMode: QQC2.Slider.SnapAlways
        activeFocusOnTab: false
        Accessible.name: row.label
        Accessible.description: row.spoken

        onMoved: SessionModel.setParam(row.blockId, row.paramId, value, true)
        onPressedChanged: if (!pressed) {
            SessionModel.commitParam(row.blockId, row.paramId);
        }

        SessionBound {
            control: bar
            property: "value"
            value: row.value
            suspended: bar.pressed
        }
    }

    QQC2.Label {
        id: note

        Layout.fillWidth: true
        visible: row.noteShown
        text: row.atLowest
        color: AppColors.muted
        font: AppFonts.small
        wrapMode: Text.WordWrap
    }
}
