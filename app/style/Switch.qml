// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T
import io.github.reverblabr

// A switch, as the prototype draws a panel's: a 34 × 18 pill, sunk
// into the well while off and filled with the accent while on, and a white
// knob with two pixels round it that runs from one end to the other.
T.Switch {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    padding: AppMetrics.smallSpacing
    horizontalPadding: 0
    spacing: AppMetrics.rowSpacing
    font: AppFonts.body
    opacity: control.enabled ? 1 : AppMetrics.disabledOpacity

    indicator: Rectangle {
        implicitWidth: AppMetrics.switchWidth
        implicitHeight: AppMetrics.switchHeight
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding)
                        : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: height / 2
        color: control.checked ? AppColors.accent : AppColors.well

        Rectangle {
            readonly property real travel: parent.width - width - AppMetrics.switchKnobInset * 2

            // `visualPosition` runs the other way in a right-to-left layout,
            // so the knob does too.
            x: AppMetrics.switchKnobInset + Math.max(0, Math.min(1, control.visualPosition)) * travel
            y: AppMetrics.switchKnobInset
            width: AppMetrics.switchKnobSize
            height: AppMetrics.switchKnobSize
            radius: width / 2
            // The one thing on the track that is not the track, so it has
            // to be seen on it, off and on.
            color: control.checked ? AppColors.textOnAccent : AppColors.knob

            Behavior on x {
                enabled: !control.down
                NumberAnimation {
                    // The prototype's `transition: left .12s`.
                    duration: 120
                }
            }
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
