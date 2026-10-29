// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes

// One VU needle on its dial: a half-circle band in the well colour,
// the part above 0 VU in the danger colour, and a needle from the pivot.
//
// **Drawn as the design draws a VU tile**: a band five parts thick on an arc
// of nineteen, in a box forty-six wide, with the needle one and a half
// parts thick in the text colour. The panel's larger dial adds the marks
// and their numbers outside the band, and a lamp that lights when the side
// has clipped.
//
// **Where the needle points is the scale's, not this file's.** VuScale says
// how far along the arc a reading is — in proportion to the amplitude, as a
// VU meter's scale always was — and the red band starts where it puts 0 VU,
// so a needle at the reference level is on the edge of the red.
//
// The needle's movement is the block's. What arrives here, sixty times a
// second, is already where a VU needle would be; smoothing it again would
// make it slower than the standard it was built to.
Item {
    id: gauge

    /// How far along the arc the needle is, 0 at the pin and 1 at +3 VU.
    property real position: 0
    /// Whether anything is running. An idle needle rests on its pin in the
    /// muted colour.
    property bool active: false
    /// Whether this side has reached full scale since the light was last
    /// put out.
    property bool clipped: false
    /// The marks and their numbers, for the panel's larger dial.
    property bool showMarks: false
    /// The side's name under the pivot, for the panel.
    property string label: ""
    /// The lamp beside the dial. A tile shows it only while it is lit.
    property bool showLamp: showMarks

    /// The design's units: the dial is 46 of them across.
    readonly property real unit: gauge.width / (gauge.showMarks ? 60 : 46)
    readonly property real band: 5 * gauge.unit
    readonly property real arcRadius: 19 * gauge.unit
    readonly property real centreX: gauge.width / 2
    /// Room above the band for the numbers, when there are numbers.
    readonly property real topRoom: gauge.showMarks ? 9 * gauge.unit : 3 * gauge.unit
    readonly property real centreY: gauge.topRoom + gauge.band / 2 + gauge.arcRadius
    /// Where 0 VU is on the arc, which is where the red starts.
    readonly property real redFrom: VuScale.positionOf(0)
    readonly property color needleColor: gauge.active ? AppColors.text : AppColors.muted

    /// The angle, in degrees clockwise from the pin, of a place on the arc.
    function angleOf(place: real): real {
        return 180 * Math.max(0, Math.min(1, place));
    }

    /// The number a mark is written with: "+2" above the reference, as a
    /// VU dial writes it.
    function markText(vu: real): string {
        return vu > 0 ? "+" + Math.round(vu) : String(Math.round(vu));
    }

    implicitWidth: 46
    implicitHeight: gauge.centreY + (gauge.showMarks ? 9 : 3) * gauge.unit

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: AppColors.well
            strokeWidth: gauge.band
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap

            PathAngleArc {
                centerX: gauge.centreX
                centerY: gauge.centreY
                radiusX: gauge.arcRadius
                radiusY: gauge.arcRadius
                startAngle: 180
                sweepAngle: 180
            }
        }

        ShapePath {
            strokeColor: AppColors.dangerMark
            strokeWidth: gauge.band
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap

            PathAngleArc {
                centerX: gauge.centreX
                centerY: gauge.centreY
                radiusX: gauge.arcRadius
                radiusY: gauge.arcRadius
                startAngle: 180 + gauge.angleOf(gauge.redFrom)
                sweepAngle: 180 - gauge.angleOf(gauge.redFrom)
            }
        }
    }

    // --- the marks, on the panel's dial ---
    Repeater {
        model: gauge.showMarks ? VuScale.marks : []

        delegate: Item {
            id: mark

            required property real modelData
            readonly property real angle: gauge.angleOf(VuScale.positionOf(mark.modelData))
            readonly property bool numbered: [-20, -10, -7, -5, -3, 0, 3].indexOf(Math.round(mark.modelData)) >= 0

            // A short line across the outside edge of the band.
            Rectangle {
                x: gauge.centreX - width / 2
                y: gauge.centreY - gauge.arcRadius - gauge.band / 2 - 2 * gauge.unit
                width: Math.max(1, gauge.unit * 0.5)
                height: 2 * gauge.unit
                color: mark.modelData > 0 ? AppColors.danger : AppColors.muted
                antialiasing: true
                transform: Rotation {
                    origin.x: Math.max(1, gauge.unit * 0.5) / 2
                    origin.y: gauge.arcRadius + gauge.band / 2 + 2 * gauge.unit
                    angle: mark.angle - 90
                }
            }

            Text {
                readonly property real reach: gauge.arcRadius + gauge.band / 2 + 5.5 * gauge.unit
                readonly property real radians: mark.angle * Math.PI / 180

                visible: mark.numbered
                text: gauge.markText(mark.modelData)
                font: AppFonts.meterScale
                color: mark.modelData > 0 ? AppColors.dangerText : AppColors.muted
                x: gauge.centreX - reach * Math.cos(radians) - width / 2
                y: gauge.centreY - reach * Math.sin(radians) - height / 2
                Accessible.ignored: true
            }
        }
    }

    Text {
        visible: gauge.label.length > 0
        text: gauge.label
        font: AppFonts.small
        color: AppColors.muted
        anchors.horizontalCenter: parent.horizontalCenter
        // Under the pivot, where the needle never goes.
        y: gauge.centreY + 2 * gauge.unit
        Accessible.ignored: true
    }

    // --- the needle ---
    Rectangle {
        id: needle

        x: gauge.centreX - width / 2
        y: gauge.centreY - height
        width: Math.max(1, 1.5 * gauge.unit)
        height: gauge.arcRadius + gauge.band / 2
        radius: width / 2
        color: gauge.needleColor
        antialiasing: true
        transformOrigin: Item.Bottom
        rotation: gauge.angleOf(gauge.active ? gauge.position : 0) - 90
    }

    Rectangle {
        visible: gauge.showMarks
        width: 3 * gauge.unit
        height: width
        radius: width / 2
        color: gauge.needleColor
        x: gauge.centreX - width / 2
        y: gauge.centreY - height / 2
    }

    // --- the lamp: red once the side has reached full scale ---
    Rectangle {
        id: lamp

        visible: gauge.showLamp || gauge.clipped
        width: (gauge.showMarks ? 4 : 5) * gauge.unit
        height: width
        radius: width / 2
        color: gauge.clipped ? AppColors.dangerMark : AppColors.well
        x: gauge.centreX + gauge.arcRadius - width / 2
        y: gauge.centreY + (gauge.showMarks ? 3 : -2.5) * gauge.unit
    }

    /// The needle and the lamp, so that a test can read what a person sees.
    readonly property alias needle: needle
    readonly property alias lamp: lamp
}
