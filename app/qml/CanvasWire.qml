// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Shapes
import org.kde.kirigami as Kirigami

// One connection, drawn.
//
// **A cubic Bézier that leaves and arrives horizontally.** Both control
// points sit on the same line as their end, so a wire meets a port square on
// however far apart the two tiles are and whichever way round they sit. That
// is what makes a canvas of them readable: every wire leaves a tile the same
// way, so the eye follows the curve instead of working out the angle.
//
// **Orange means audio is going through it, grey means it is not**
// ([ADR-0009], AppColors.live). The dash moving along a live wire is the one
// expensive thing the canvas draws — it costs more per frame than everything
// else together, because animating the offset marks every live wire's path
// dirty — so it is the canvas that decides whether the phase advances at
// all, and a wire only reads it.
//
// **It is sized to what it draws.** A Shape with no size of its own is a
// shape the scene graph has no bounding box for, which is how a wire ends up
// culled at one zoom and drawn at another. The path is therefore held in the
// item's own coordinates, offset from the session's.
Shape {
    id: wire

    /// The two ends, in the session's coordinates.
    property real fromX: 0
    property real fromY: 0
    property real toX: 0
    property real toY: 0

    /// How far the control points reach out from each end. The canvas works
    /// it out, so that hit-testing a click and drawing the curve cannot
    /// disagree about where the curve is.
    property real curvature: 30

    /// Whether audio is going through it at this moment.
    property bool live: false
    /// Whether the flow animation is running at all, and how far along it
    /// is. Both are the canvas's: one ticker for the whole canvas, so every
    /// wire moves in step and the canvas costs one timer rather than one
    /// per wire.
    property bool flowing: false
    property real flowPhase: 0

    /// Whether this is the wire that is selected.
    property bool selected: false
    /// Whether it is the loose end being dragged rather than a wire the
    /// session holds. It is drawn thinner and never animated: it is not a
    /// connection yet, and drawing it like one would say it was.
    property bool provisional: false

    /// What the wire is drawn in, and why. **Orange means audio is going
    /// through it and grey means it is not** ([ADR-0009], AppColors.live);
    /// the selection colour wins over both, because a wire somebody has
    /// picked out has to be findable whatever it is carrying.
    readonly property color strokeColor: wire.selected
        ? AppColors.accentMark
        : wire.live ? AppColors.liveMark : AppColors.wireMark

    /// How much room the curve needs outside the box its two ends make. The
    /// control points reach `curvature` sideways, and the stroke has width.
    readonly property real overhang: wire.curvature + Kirigami.Units.smallSpacing * 2

    x: Math.min(wire.fromX, wire.toX) - wire.overhang
    y: Math.min(wire.fromY, wire.toY) - wire.overhang
    width: Math.abs(wire.toX - wire.fromX) + wire.overhang * 2
    height: Math.abs(wire.toY - wire.fromY) + wire.overhang * 2

    // The curve renderer shades curves instead of flattening them into line
    // segments, so a wire stays smooth at 200 % as well as at 50 %
    // ([ADR-0009]). It costs about 0.3 ms a frame for sixty wires, which is
    // what the spike measured and what the budget was set knowing.
    preferredRendererType: Shape.CurveRenderer
    Accessible.ignored: true

    /// Whether the flow is the prototype's pale dashes running over a
    /// solid wire, rather than the wire itself dashed, which is how
    /// following the system draws it.
    readonly property bool paleFlow: !AppMetrics.followsSystem && wire.live && wire.flowing && !wire.selected

    ShapePath {
        strokeColor: wire.strokeColor
        strokeWidth: wire.selected ? 3 : wire.provisional ? 1 : 2
        fillColor: "transparent"
        capStyle: ShapePath.RoundCap
        // The dash pattern adds up to ten, which is why the phase the canvas
        // keeps is taken modulo ten: the dash then carries on where it was
        // rather than jumping when the number wraps.
        strokeStyle: wire.live && wire.flowing && AppMetrics.followsSystem ? ShapePath.DashLine
                                                                           : ShapePath.SolidLine
        dashPattern: [6, 4]
        dashOffset: -wire.flowPhase

        startX: wire.fromX - wire.x
        startY: wire.fromY - wire.y

        PathCubic {
            x: wire.toX - wire.x
            y: wire.toY - wire.y
            control1X: wire.fromX - wire.x + wire.curvature
            control1Y: wire.fromY - wire.y
            control2X: wire.toX - wire.x - wire.curvature
            control2Y: wire.toY - wire.y
        }
    }

    // **The prototype's flow**: six pixels of pale dash in every twenty-four,
    // over the orange (`stroke-dasharray: 6 18`). A shape of its own that is
    // not drawn at all unless it moves, so a wire that is not live, and
    // every wire while following the system, costs what it did.
    Shape {
        anchors.fill: parent
        visible: wire.paleFlow
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: AppColors.flowDash
            strokeWidth: 2
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            strokeStyle: ShapePath.DashLine
            // In units of the stroke's width: 6 and 18 pixels. Twelve units
            // a round, where the canvas's phase goes round in ten, so it is
            // scaled to carry on where it was when it wraps.
            dashPattern: [3, 9]
            dashOffset: -wire.flowPhase * 12 / 10

            startX: wire.fromX - wire.x
            startY: wire.fromY - wire.y

            PathCubic {
                x: wire.toX - wire.x
                y: wire.toY - wire.y
                control1X: wire.fromX - wire.x + wire.curvature
                control1Y: wire.fromY - wire.y
                control2X: wire.toX - wire.x - wire.curvature
                control2Y: wire.toY - wire.y
            }
        }
    }
}
