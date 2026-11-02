// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// **The numbers round a plot**: its well and grid lines, and in gutters
// outside it the numbers and each axis's title, always there, with no hover
// asked for.
//
// It holds no scale of its own. The panel owns the mapping from a value to a
// place, and gives its own functions as xOf and yOf, in the plot's
// coordinates, so a tick and a handle cannot disagree, whatever the size of
// the plot. The panel lays the plot over this item, at leftGutter and
// topGutter, and everything the plot's mouse areas and tests use stays in the
// plot's own coordinates.
//
// A number that would touch the one before it is left out rather than
// drawn on top of it, which is what happens in a small card at large text.
Item {
    id: axes

    /// The item the values are drawn in, laid over this one.
    required property Item plot
    /// Where a value sits in the plot, the panel's own mappings.
    required property var xOf
    required property var yOf
    /// A value's number.
    required property var xText
    required property var yText
    /// The values that get a line and a number across and up, and the
    /// values up the plot that get a line and no number.
    property var xTicks: []
    property var yTicks: []
    property var yLines: []
    /// The value up the plot to draw heavier, as a gain of 0; or null.
    property var heavyY: null
    property string xTitle: ""
    property string yTitle: ""
    /// The widest number the left gutter is kept for, whichever view is on,
    /// so the plot does not shift when the view changes.
    property string widestYText: ""
    property real radius: AppMetrics.troughRadius

    readonly property int tickLength: 3
    readonly property real gap: Kirigami.Units.smallSpacing / 2
    readonly property real lineHeight: metrics.height
    /// The widest frequency, half of which reaches past the plot's right edge.
    readonly property real widestXText: Math.max(0, ...axes.xTicks.map(value => metrics.advanceWidth(axes.xText(value))))

    readonly property real leftGutter: Math.ceil(metrics.advanceWidth(axes.widestYText)) + axes.tickLength + axes.gap
    readonly property real rightGutter: Math.ceil(axes.widestXText / 2)
    readonly property real topGutter: Math.ceil(axes.lineHeight * 1.5)
    readonly property real bottomGutter: Math.ceil(axes.tickLength + 2 * axes.lineHeight + axes.gap)

    /// The values whose numbers fit, low to high along the axis.
    readonly property var shownX: {
        const kept = [];
        let end = -Infinity;
        for (const value of axes.xTicks) {
            const half = metrics.advanceWidth(axes.xText(value)) / 2;
            const at = axes.xOf(value);
            if (at - half >= end) {
                kept.push(value);
                end = at + half + axes.gap * 2;
            }
        }
        return kept;
    }
    readonly property var shownY: {
        const kept = [];
        let end = -Infinity;
        for (const value of [...axes.yTicks].sort((a, b) => axes.yOf(a) - axes.yOf(b))) {
            const at = axes.yOf(value);
            if (at - axes.lineHeight / 2 >= end) {
                kept.push(value);
                end = at + axes.lineHeight / 2;
            }
        }
        return kept;
    }

    /// The parts a test reads.
    readonly property alias xNumbers: xNumberRepeater
    readonly property alias yNumbers: yNumberRepeater
    readonly property alias xTitleLabel: xTitleText
    readonly property alias yTitleLabel: yTitleText

    FontMetrics {
        id: metrics

        font: AppFonts.small
    }

    Rectangle {
        x: axes.plot.x
        y: axes.plot.y
        width: axes.plot.width
        height: axes.plot.height
        color: AppColors.well
        radius: axes.radius
    }

    Repeater {
        model: axes.yLines.concat(axes.yTicks.filter(value => axes.yLines.indexOf(value) < 0))

        delegate: Rectangle {
            required property var modelData

            readonly property bool heavy: axes.heavyY !== null && modelData === axes.heavyY

            x: axes.plot.x
            y: axes.plot.y + Math.round(axes.yOf(modelData)) - (heavy ? 1 : 0)
            width: axes.plot.width
            height: heavy ? 2 : 1
            color: heavy ? AppColors.muted : AppColors.border
        }
    }

    Repeater {
        model: axes.xTicks

        delegate: Rectangle {
            required property var modelData

            x: axes.plot.x + Math.round(axes.xOf(modelData))
            y: axes.plot.y
            width: 1
            height: axes.plot.height
            color: AppColors.border
        }
    }

    Repeater {
        id: xNumberRepeater

        model: axes.shownX

        delegate: Item {
            id: xNumber

            required property var modelData

            x: axes.plot.x + Math.round(axes.xOf(xNumber.modelData))
            y: axes.plot.y + axes.plot.height
            width: 1
            height: axes.tickLength

            Rectangle {
                anchors.fill: parent
                color: AppColors.muted
            }

            QQC2.Label {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.bottom
                text: axes.xText(xNumber.modelData)
                font: AppFonts.small
                color: AppColors.muted
                Accessible.ignored: true
            }
        }
    }

    Repeater {
        id: yNumberRepeater

        model: axes.shownY

        delegate: Item {
            id: yNumber

            required property var modelData

            readonly property bool heavy: axes.heavyY !== null && yNumber.modelData === axes.heavyY

            x: axes.plot.x - axes.tickLength
            y: axes.plot.y + Math.round(axes.yOf(yNumber.modelData))
            width: axes.tickLength
            height: 1

            Rectangle {
                anchors.fill: parent
                color: AppColors.muted
            }

            QQC2.Label {
                anchors.right: parent.left
                anchors.rightMargin: axes.gap
                anchors.verticalCenter: parent.verticalCenter
                text: axes.yText(yNumber.modelData)
                font: AppFonts.small
                color: yNumber.heavy ? AppColors.text : AppColors.muted
                Accessible.ignored: true
            }
        }
    }

    // Level with the plot's top, so the words read across and take no width
    // from the plot.
    QQC2.Label {
        id: yTitleText

        width: axes.width
        text: axes.yTitle
        elide: Text.ElideRight
        font: AppFonts.small
        color: AppColors.muted
        Accessible.ignored: true
    }

    QQC2.Label {
        id: xTitleText

        x: axes.plot.x
        y: axes.height - axes.lineHeight
        width: axes.plot.width
        horizontalAlignment: Text.AlignHCenter
        text: axes.xTitle
        elide: Text.ElideRight
        font: AppFonts.small
        color: AppColors.muted
        Accessible.ignored: true
    }
}
