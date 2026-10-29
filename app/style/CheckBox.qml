// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.impl
import QtQuick.Shapes
import QtQuick.Templates as T
import io.github.reverblabr

// A check box, as a panel's are drawn in the prototype: a 15-pixel
// box with a small corner, the field's colour inside a border, filled with
// the accent and ticked in white once it is checked; the label ten pixels
// to its side.
T.CheckBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    padding: AppMetrics.smallSpacing
    horizontalPadding: 0
    // **A line of text tall**, as the prototype's rows are: the room
    // above and below a choice is the gap between rows, which the panel
    // around it sets, rather than padding of its own on top of that.
    verticalPadding: 0
    spacing: AppMetrics.rowSpacing
    font: AppFonts.body
    opacity: control.enabled ? 1 : AppMetrics.disabledOpacity

    indicator: Rectangle {
        readonly property bool marked: control.checkState !== Qt.Unchecked

        implicitWidth: AppMetrics.indicatorSize
        implicitHeight: AppMetrics.indicatorSize
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding)
                        : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: AppMetrics.smallRadius
        color: marked ? AppColors.accent : AppColors.field
        border.width: 1
        border.color: marked ? AppColors.accent : control.visualFocus || control.hovered ? AppColors.accentMark : AppColors.controlBorder

        // The prototype's tick: a 10-pixel drawing of `M5 12l5 5 9-10` on a
        // 24-unit grid, three units thick, with round ends.
        Shape {
            id: tick

            readonly property real unit: AppMetrics.indicatorSize * 10 / 15 / 24

            anchors.centerIn: parent
            width: 24 * tick.unit
            height: 24 * tick.unit
            visible: control.checkState === Qt.Checked
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: AppColors.textOnAccent
                strokeWidth: 3 * tick.unit
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                startX: 5 * tick.unit
                startY: 12 * tick.unit

                PathLine {
                    x: 10 * tick.unit
                    y: 17 * tick.unit
                }
                PathLine {
                    x: 19 * tick.unit
                    y: 7 * tick.unit
                }
            }
        }

        // Some but not all: a bar, as a list's "all of these" says it.
        Rectangle {
            anchors.centerIn: parent
            width: parent.width / 2
            height: 2
            radius: height / 2
            color: AppColors.textOnAccent
            visible: control.checkState === Qt.PartiallyChecked
        }

        FocusRing {
            shown: control.visualFocus
            controlRadius: parent.radius
        }
    }

    contentItem: CheckLabel {
        leftPadding: control.indicator && !control.mirrored ? control.indicator.width + control.spacing : 0
        rightPadding: control.indicator && control.mirrored ? control.indicator.width + control.spacing : 0

        text: control.text
        font: control.font
        color: AppColors.text
    }
}
