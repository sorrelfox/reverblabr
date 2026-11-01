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

// A number on a block's card: its name, a typed value, its unit, a slider, and
// what its lowest value means.
//
// **Two ways to one value, and the keys mean one thing.** The arrows and Page
// keys step in the field, and the slider is out of the Tab order.
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
    /// What the arrows and Page keys move it by, how many places show, and
    /// what the slider snaps to.
    property real stepSize: 1
    property real pageStepSize: 3
    property int decimals: 0
    /// Whether the slider spreads the range by ratio, as a time wants.
    property bool logarithmic: false

    /// The parts, so that a test drives what a person drives.
    readonly property alias slider: bar
    readonly property alias field: typed
    readonly property alias lowestNote: note
    readonly property bool noteShown: row.atLowest !== "" && row.value === row.from

    /// A value kept to the row's own steps.
    function stepped(number: real): real {
        return Number(number.toFixed(row.decimals));
    }

    /// A value's place on the slider, and back.
    function onSlider(number: real): real {
        return row.logarithmic ? Math.log(number) : number;
    }

    function offSlider(place: real): real {
        return row.logarithmic ? row.stepped(Math.round(Math.exp(place) / row.stepSize) * row.stepSize) : place;
    }

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
            decimals: row.decimals
            stepSize: row.stepSize
            pageStepSize: row.pageStepSize
            Accessible.description: row.spoken
            onValueEdited: number => SessionModel.setParam(row.blockId, row.paramId, row.stepped(number))
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
        from: row.onSlider(row.from)
        to: row.onSlider(row.to)
        stepSize: row.logarithmic ? 0 : row.stepSize
        snapMode: row.logarithmic ? QQC2.Slider.NoSnap : QQC2.Slider.SnapAlways
        activeFocusOnTab: false
        Accessible.name: row.label
        Accessible.description: row.spoken

        onMoved: SessionModel.setParam(row.blockId, row.paramId, row.offSlider(value), true)
        onPressedChanged: if (!pressed) {
            SessionModel.commitParam(row.blockId, row.paramId);
        }

        SessionBound {
            control: bar
            property: "value"
            value: row.onSlider(row.value)
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
