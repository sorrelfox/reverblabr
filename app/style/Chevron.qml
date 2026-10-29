// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Shapes
import io.github.reverblabr

// The prototype's chevron: `M6 9l6 6 6-6` on its 24-unit grid, drawn
// eight pixels across with round ends, pointing down, or up when `up` is
// set. A list that drops down has one at its end, and a number's steps a
// pair.
Shape {
    id: chevron

    /// Which way it points.
    property bool up: false
    /// What it is drawn in.
    property color color: AppColors.text

    // The stroke's ends stay inside the eight pixels.
    readonly property real inset: AppMetrics.chevronStroke / 2
    readonly property real unit: (AppMetrics.chevronWidth - AppMetrics.chevronStroke) / 12

    implicitWidth: AppMetrics.chevronWidth
    implicitHeight: 6 * chevron.unit + AppMetrics.chevronStroke
    preferredRendererType: Shape.CurveRenderer

    ShapePath {
        strokeColor: chevron.color
        strokeWidth: AppMetrics.chevronStroke
        fillColor: "transparent"
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin
        startX: chevron.inset
        startY: chevron.inset + (chevron.up ? 6 * chevron.unit : 0)

        PathLine {
            x: chevron.inset + 6 * chevron.unit
            y: chevron.inset + (chevron.up ? 0 : 6 * chevron.unit)
        }
        PathLine {
            x: chevron.inset + 12 * chevron.unit
            y: chevron.inset + (chevron.up ? 6 * chevron.unit : 0)
        }
    }
}
