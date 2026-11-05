// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick

// A voice level along a track, with a line at each mark.
//
// **One colour in every position, on purpose.** The bar says where a level is
// and the words say which side of a mark, so nothing here reads the position.
// A colour for out of range would be a warning, and would make the plain
// colour mean "fine". `fillColor` is a fixed token and not the accent, which
// follows the desktop and can be green.
Rectangle {
    id: bar

    /// The level, in LUFS.
    property real level: -120
    /// Whether there is a level to draw.
    property bool shown: false
    /// Half strength: a number held through a pause.
    property bool held: false
    /// The marks, Not set or LUFS.
    property real upper: -61
    property real lower: -61
    /// The card's full scale, rather than one fitted to the marks.
    property bool wholeScale: false
    /// What a screen reader calls it; none hides it.
    property string name: ""
    /// What a screen reader is told it reads.
    property string spoken: ""

    readonly property VoiceGaugeWords words: VoiceGaugeWords {}
    readonly property var range: bar.wholeScale ? ({ from: -60, to: 0 }) : bar.words.span(bar.upper, bar.lower)
    readonly property color fillColor: AppColors.voiceBar
    readonly property real fillShare: bar.shown ? bar.place(bar.level) : 0
    readonly property real fillStrength: bar.held ? 0.5 : 1
    readonly property alias fill: filled

    /// Where \a value sits along the track, from 0 to 1.
    function place(value: real): real {
        return Math.max(0, Math.min(1, (value - bar.range.from) / (bar.range.to - bar.range.from)));
    }

    implicitHeight: AppMetrics.stateDotSize / 2
    radius: AppMetrics.barRadius
    color: AppColors.well
    Accessible.ignored: bar.name === ""
    Accessible.role: Accessible.ProgressBar
    Accessible.name: bar.name
    Accessible.description: bar.spoken

    Rectangle {
        id: filled

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: parent.width * bar.fillShare
        radius: parent.radius
        color: bar.fillColor
        opacity: bar.fillStrength
    }

    Repeater {
        model: [bar.upper, bar.lower].filter(mark => bar.words.isSet(mark))

        delegate: Rectangle {
            id: line

            required property real modelData

            x: Math.round(bar.width * bar.place(line.modelData))
            y: -1
            width: 1
            height: bar.height + 2
            color: AppColors.text
        }
    }
}
