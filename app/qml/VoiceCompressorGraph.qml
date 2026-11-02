// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import org.kde.kirigami as Kirigami
import org.kde.ki18n

// The Voice Compressor's curve: what comes out for a steady level in, with the
// line of no change behind it, the ceiling as a line, and a dot where the sound
// is now. The dot leaves the curve while the gain is on its way, which is what
// a compressor with a release does. Everything here is said in numbers on the
// card too, so nothing has to be seen.
Item {
    id: graph

    required property VoiceCompressorWords words
    property real compression: words.defaultCompression
    property real ceiling: words.defaultCeiling
    property real gate: words.gateOff
    /// The sound now, and whether there is any.
    property bool live: false
    property real inDb: -120
    property real outDb: -120

    readonly property real lowest: -60
    readonly property real side: AppMetrics.followsSystem ? Kirigami.Units.gridUnit * 12 : 216

    /// The curve as the law draws it, for a test too.
    readonly property var curve: graph.words.curve(graph.compression, graph.ceiling, graph.gate)
                                                  .filter(point => point.x >= graph.lowest)
    /// The curve in the plot's coordinates. **Up and down it is not held to
    /// the plot**, which clips it: a law that goes on falling is not a floor.
    readonly property var drawn: graph.curve.map(point => Qt.point(graph.xOf(point.x),
                                                                   plotArea.height * point.y / graph.lowest))
    readonly property alias axes: axesItem
    readonly property alias plot: plotArea

    function clamped(db: real): real {
        return Math.max(graph.lowest, Math.min(0, db));
    }

    function xOf(db: real): real {
        return plotArea.width * (graph.clamped(db) - graph.lowest) / -graph.lowest;
    }

    function yOf(db: real): real {
        return plotArea.height * -graph.clamped(db) / -graph.lowest;
    }

    implicitWidth: axesItem.leftGutter + graph.side + axesItem.rightGutter
    implicitHeight: axesItem.topGutter + graph.side + axesItem.bottomGutter

    Accessible.role: Accessible.Graphic
    Accessible.name: KI18n.i18nc("@info:whatsthis the Voice Compressor's graph", "Level going out against level coming in. The settings are in the rows under it.")

    PlotAxes {
        id: axesItem

        anchors.fill: parent
        plot: plotArea
        xOf: db => graph.xOf(db)
        yOf: db => graph.yOf(db)
        xText: value => graph.words.common.plainNumber(value)
        yText: value => graph.words.common.plainNumber(value)
        xTicks: [-60, -48, -36, -24, -12, 0]
        yTicks: [-60, -48, -36, -24, -12, 0]
        xTitle: KI18n.i18nc("@label the axis across the Voice Compressor's graph", "In, dBFS")
        yTitle: KI18n.i18nc("@label the axis up the Voice Compressor's graph", "Out, dBFS")
        widestYText: graph.words.common.plainNumber(-60)
    }

    Item {
        id: plotArea

        x: axesItem.leftGutter
        y: axesItem.topGutter
        width: Math.max(1, graph.width - axesItem.leftGutter - axesItem.rightGutter)
        height: Math.max(1, graph.height - axesItem.topGutter - axesItem.bottomGutter)
        clip: true

        Rectangle {
            id: ceilingLine

            y: Math.round(graph.yOf(graph.ceiling))
            width: parent.width
            height: 1
            color: AppColors.warningText
        }

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            // No change, for the curve to be read against.
            ShapePath {
                strokeColor: AppColors.muted
                strokeWidth: 1
                strokeStyle: ShapePath.DashLine
                dashPattern: [3, 3]
                fillColor: "transparent"

                PathPolyline {
                    path: [Qt.point(0, plotArea.height), Qt.point(plotArea.width, 0)]
                }
            }

            ShapePath {
                strokeColor: AppColors.accentText
                strokeWidth: 2
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin

                PathPolyline {
                    path: graph.drawn
                }
            }
        }

        Rectangle {
            id: nowDot

            visible: graph.live && graph.inDb >= graph.lowest
            x: graph.xOf(graph.inDb) - width / 2
            y: graph.yOf(graph.outDb) - height / 2
            width: Kirigami.Units.smallSpacing * 2
            height: width
            radius: width / 2
            color: AppColors.text
            Accessible.ignored: true
        }
    }
}
