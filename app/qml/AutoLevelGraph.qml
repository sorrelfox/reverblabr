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

// Auto Level's level in against level out: what the settings do, fixed, with
// one dot for the sound now. Both axes run from -60 to 0 LUFS.
//
// **The dot is worked out, not measured**: across at the reading, up at the
// reading plus the gain, and the limiter is not in it. In a glide it sits off
// the curve and a pull drops it below; both are true. Its numbers are in the
// live line and the table, so nothing here has to be seen.
Item {
    id: graph

    /// The block's settings, as AutoLevelWords' rule takes them.
    required property var settings
    required property AutoLevelWords words
    /// The sound now, and whether there is any.
    property bool live: false
    property real inLufs: -60
    property real outLufs: -60

    readonly property real lowest: -60
    readonly property real highest: 0
    readonly property real leftRoom: Kirigami.Units.gridUnit * 1.6
    readonly property real bottomRoom: Kirigami.Units.gridUnit * 2.4
    readonly property real topRoom: Kirigami.Units.gridUnit * 1.2
    readonly property real plotWidth: Math.max(1, graph.width - graph.leftRoom)
    readonly property real plotHeight: Math.max(1, graph.height - graph.bottomRoom - graph.topRoom)

    /// The curve as the rule draws it, for a test too.
    readonly property var curve: graph.words.curve(graph.settings)

    function atX(lufs: real): real {
        return graph.leftRoom + (Math.max(graph.lowest, Math.min(graph.highest, lufs)) - graph.lowest)
            / (graph.highest - graph.lowest) * graph.plotWidth;
    }

    function atY(lufs: real): real {
        return graph.topRoom + graph.plotHeight - (Math.max(graph.lowest, Math.min(graph.highest, lufs)) - graph.lowest)
            / (graph.highest - graph.lowest) * graph.plotHeight;
    }

    implicitHeight: AppMetrics.followsSystem ? Kirigami.Units.gridUnit * 14 : 270

    Accessible.role: Accessible.Graphic
    Accessible.name: KI18n.i18nc("@info:whatsthis Auto Level's graph", "Level going out against level coming in. The numbers are in the line below and in the table.")

    Rectangle {
        x: graph.leftRoom
        y: graph.topRoom
        width: graph.plotWidth
        height: graph.plotHeight
        radius: AppMetrics.troughRadius
        color: AppColors.well
    }

    // Under the gate there is nothing to read.
    Rectangle {
        id: gateStrip

        x: graph.leftRoom
        y: graph.topRoom
        width: graph.atX(graph.words.gate) - graph.leftRoom
        height: graph.plotHeight
        color: AppColors.muted
        opacity: 0.14
    }

    // What is left alone.
    Rectangle {
        x: graph.atX(graph.settings.target - graph.settings.band)
        y: graph.topRoom
        width: Math.max(2, graph.atX(graph.settings.target + graph.settings.band) - x)
        height: graph.plotHeight
        color: AppColors.accentText
        opacity: 0.4
    }

    Repeater {
        model: [-60, -50, -40, -30, -20, -10, 0]

        delegate: QQC2.Label {
            required property real modelData

            x: graph.atX(modelData) - width / 2
            y: graph.topRoom + graph.plotHeight + Kirigami.Units.smallSpacing
            text: graph.words.plainNumber(modelData).replace("+", "")
            font: AppFonts.small
            color: AppColors.muted
            Accessible.ignored: true
        }
    }

    Repeater {
        model: [-60, -40, -20]

        delegate: QQC2.Label {
            required property real modelData

            x: graph.leftRoom - width - Kirigami.Units.smallSpacing
            y: graph.atY(modelData) - height / 2
            text: graph.words.plainNumber(modelData).replace("+", "")
            font: AppFonts.small
            color: AppColors.muted
            Accessible.ignored: true
        }
    }

    QQC2.Label {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        text: KI18n.i18nc("@label an axis of Auto Level's graph", "Level coming in, LUFS, last 3 s")
        font: AppFonts.small
        color: AppColors.muted
        Accessible.ignored: true
    }

    QQC2.Label {
        text: KI18n.i18nc("@label an axis of Auto Level's graph", "Level going out, LUFS")
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
            strokeColor: AppColors.categoryTint(BlockPresentation.category("effect.autolevel"))
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
        x: graph.leftRoom + Kirigami.Units.smallSpacing
        y: graph.topRoom + Kirigami.Units.smallSpacing
        width: gateStrip.width - Kirigami.Units.smallSpacing
        text: KI18n.i18nc("@label the part of Auto Level's graph under its gate", "too quiet to read")
        font: AppFonts.small
        color: AppColors.muted
        wrapMode: Text.WordWrap
        Accessible.ignored: true
    }

    QQC2.Label {
        id: bandLabel

        readonly property real wanted: graph.atX(graph.settings.target) - width / 2

        x: Math.max(graph.leftRoom, Math.min(graph.width - width, wanted))
        y: graph.topRoom + Kirigami.Units.smallSpacing
        horizontalAlignment: Text.AlignHCenter
        text: KI18n.i18nc("@label on Auto Level's graph, 'target -23' and under it 'left alone ±1 LU'", "target %1\nleft alone %2",
                          graph.words.plainNumber(graph.settings.target),
                          graph.words.bandText(graph.settings.band))
        font: AppFonts.small
        color: AppColors.text
        Accessible.ignored: true
    }

    QQC2.Label {
        anchors.right: parent.right
        anchors.rightMargin: Kirigami.Units.smallSpacing
        y: graph.topRoom + graph.plotHeight - height - Kirigami.Units.smallSpacing
        text: KI18n.i18nc("@label the dashed line on Auto Level's graph", "dashed: unchanged")
        font: AppFonts.small
        color: AppColors.muted
        Accessible.ignored: true
    }

    QQC2.Label {
        readonly property real lower: graph.settings.target - graph.settings.band

        visible: graph.settings.boostOn
        x: graph.atX(graph.lowest + (lower - graph.lowest) / 2)
        y: graph.atY(lower) + Kirigami.Units.smallSpacing * 6
        text: KI18n.i18nc("@label the part of Auto Level's curve above the dashed line", "lifted")
        font: AppFonts.small
        color: AppColors.muted
        Accessible.ignored: true
    }

    QQC2.Label {
        readonly property real upper: graph.settings.target + graph.settings.band

        visible: graph.settings.cutOn
        x: graph.atX(upper) + Kirigami.Units.largeSpacing
        y: graph.atY(upper) + Kirigami.Units.smallSpacing * 2
        text: KI18n.i18nc("@label the part of Auto Level's curve under the dashed line", "turned down")
        font: AppFonts.small
        color: AppColors.muted
        Accessible.ignored: true
    }

    Rectangle {
        id: nowDot

        visible: graph.live
        x: graph.atX(graph.inLufs) - width / 2
        y: graph.atY(graph.outLufs) - height / 2
        width: Kirigami.Units.smallSpacing * 2.5
        height: width
        radius: width / 2
        color: AppColors.liveMark
    }

    /// The dot, for a test.
    readonly property alias dot: nowDot
}
