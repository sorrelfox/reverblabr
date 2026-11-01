// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Shapes
import org.kde.kirigami as Kirigami
import org.kde.ki18n

// Night Level's level in against level out. **Both axes are in LU from speech:**
// 0 is the speech, so the curve keeps its place as the level drifts, and +10
// reads as 10 LU louder than voices.
//
// **The dot is the 400 ms reading and the gain applied now**, so it leaves the
// curve while the gain is on its way. Its numbers are in the line under the
// graph and in the table, so nothing here has to be seen.
Item {
    id: graph

    required property NightLevelWords words
    property real cut: 0
    property real boost: 0
    /// The sound now, in LU from speech, and whether there is any.
    property bool live: false
    property real inLu: 0
    property real outLu: 0
    /// Where the peaks stop, in LU from speech, and whether that can be drawn.
    property bool showsPeakLine: false
    property real peakLu: 0

    readonly property real lowest: -40
    readonly property real highest: 30
    readonly property real leftRoom: Kirigami.Units.gridUnit * 1.6
    readonly property real bottomRoom: Kirigami.Units.gridUnit * 2.4
    readonly property real topRoom: Kirigami.Units.gridUnit * 1.2
    readonly property real plotWidth: Math.max(1, graph.width - graph.leftRoom)
    readonly property real plotHeight: Math.max(1, graph.height - graph.bottomRoom - graph.topRoom)

    /// The curve as the law draws it, for a test too.
    readonly property var curve: graph.words.curve(graph.cut, graph.boost)

    function clamped(lu: real): real {
        return Math.max(graph.lowest, Math.min(graph.highest, lu));
    }

    function atX(lu: real): real {
        return graph.leftRoom + (graph.clamped(lu) - graph.lowest) / (graph.highest - graph.lowest) * graph.plotWidth;
    }

    function atY(lu: real): real {
        return graph.topRoom + graph.plotHeight
            - (graph.clamped(lu) - graph.lowest) / (graph.highest - graph.lowest) * graph.plotHeight;
    }

    implicitHeight: AppMetrics.followsSystem ? Kirigami.Units.gridUnit * 14 : 270

    Accessible.role: Accessible.Graphic
    Accessible.name: KI18n.i18nc("@info:whatsthis Night Level's graph", "Level going out against level coming in, both from the speech level. The numbers are in the line below and in the table.")

    Rectangle {
        x: graph.leftRoom
        y: graph.topRoom
        width: graph.plotWidth
        height: graph.plotHeight
        radius: AppMetrics.troughRadius
        color: AppColors.well
    }

    // Under the floor nothing is lifted.
    Rectangle {
        id: floorStrip

        x: graph.leftRoom
        y: graph.topRoom
        width: graph.atX(graph.words.floor) - graph.leftRoom
        height: graph.plotHeight
        color: AppColors.muted
        opacity: 0.14
    }

    // What is left alone.
    Rectangle {
        x: graph.atX(-graph.words.bandBelow)
        y: graph.topRoom
        width: graph.atX(graph.words.bandAbove) - x
        height: graph.plotHeight
        color: AppColors.accentText
        opacity: 0.25
    }

    // The speech.
    Rectangle {
        x: graph.atX(0)
        y: graph.topRoom
        width: 1
        height: graph.plotHeight
        color: AppColors.accentText
    }

    // Where peaks stop.
    Rectangle {
        visible: graph.showsPeakLine
        x: graph.leftRoom
        y: graph.atY(graph.peakLu)
        width: graph.plotWidth
        height: 1
        color: AppColors.warningText
    }

    Repeater {
        model: [-40, -30, -20, -10, 0, 10, 20, 30]

        delegate: QQC2.Label {
            required property real modelData

            // The last one would stand half out of the plot.
            x: Math.min(graph.width - width, graph.atX(modelData) - width / 2)
            y: graph.topRoom + graph.plotHeight + Kirigami.Units.smallSpacing
            text: graph.words.common.plainNumber(modelData)
            font: AppFonts.small
            color: AppColors.muted
            Accessible.ignored: true
        }
    }

    Repeater {
        model: [-30, -10, 10, 30]

        delegate: QQC2.Label {
            required property real modelData

            x: graph.leftRoom - width - Kirigami.Units.smallSpacing
            y: graph.atY(modelData) - height / 2
            text: graph.words.common.plainNumber(modelData)
            font: AppFonts.small
            color: AppColors.muted
            Accessible.ignored: true
        }
    }

    QQC2.Label {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        text: KI18n.i18nc("@label an axis of Night Level's graph", "Level coming in, LU from speech")
        font: AppFonts.small
        color: AppColors.muted
        Accessible.ignored: true
    }

    QQC2.Label {
        text: KI18n.i18nc("@label an axis of Night Level's graph", "Level going out, LU from speech")
        font: AppFonts.small
        color: AppColors.muted
        Accessible.ignored: true
    }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        // Unchanged.
        ShapePath {
            strokeColor: AppColors.muted
            strokeWidth: 1
            strokeStyle: ShapePath.DashLine
            dashPattern: [3, 3]
            fillColor: "transparent"

            PathPolyline {
                path: [Qt.point(graph.atX(graph.lowest), graph.atY(graph.lowest)),
                       Qt.point(graph.atX(graph.highest), graph.atY(graph.highest))]
            }
        }

        ShapePath {
            strokeColor: AppColors.categoryTint(BlockPresentation.category("effect.nightlevel"))
            strokeWidth: 2
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin

            PathPolyline {
                path: graph.curve.map(point => Qt.point(graph.atX(point.in), graph.atY(point.out)))
            }
        }
    }

    QQC2.Label {
        id: floorLabel

        x: graph.leftRoom + Kirigami.Units.smallSpacing
        y: graph.topRoom + Kirigami.Units.smallSpacing
        width: floorStrip.width - Kirigami.Units.smallSpacing
        text: KI18n.i18nc("@label the part of Night Level's graph under its floor", "not lifted")
        font: AppFonts.small
        color: AppColors.muted
        wrapMode: Text.WordWrap
        Accessible.ignored: true
    }

    QQC2.Label {
        x: Math.max(graph.leftRoom, graph.atX(0) - width / 2)
        y: graph.topRoom + Kirigami.Units.smallSpacing
        horizontalAlignment: Text.AlignHCenter
        text: KI18n.i18nc("@label on Night Level's graph", "speech\nleft alone -5 to +3")
        font: AppFonts.small
        color: AppColors.text
        Accessible.ignored: true
    }

    QQC2.Label {
        id: cutLabel

        visible: graph.cut > 0
        x: graph.atX(16)
        y: graph.atY(4)
        text: graph.words.ratioText(graph.words.cutSlope, graph.cut)
        font: AppFonts.small
        color: AppColors.muted
        Accessible.ignored: true
    }

    QQC2.Label {
        id: liftLabel

        visible: graph.boost > 0
        x: graph.atX(-24)
        y: graph.atY(-12)
        text: graph.words.ratioText(graph.words.liftSlope, graph.boost)
        font: AppFonts.small
        color: AppColors.muted
        Accessible.ignored: true
    }

    QQC2.Label {
        anchors.right: parent.right
        anchors.rightMargin: Kirigami.Units.smallSpacing
        y: graph.topRoom + graph.plotHeight - height - Kirigami.Units.smallSpacing
        text: KI18n.i18nc("@label the dashed line on Night Level's graph", "dashed: unchanged")
        font: AppFonts.small
        color: AppColors.muted
        Accessible.ignored: true
    }

    Rectangle {
        id: nowDot

        visible: graph.live
        x: graph.atX(graph.inLu) - width / 2
        y: graph.atY(graph.outLu) - height / 2
        width: Kirigami.Units.smallSpacing * 2.5
        height: width
        radius: width / 2
        color: AppColors.liveMark
    }

    /// The dot and the ratios, for a test.
    readonly property alias dot: nowDot
    readonly property alias cutRatio: cutLabel
    readonly property alias liftRatio: liftLabel
}
