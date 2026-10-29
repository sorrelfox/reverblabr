// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Templates as T

// The ring a slider is dragged by, as the prototype draws the one on a
// tile's face: the surface inside, the tint round it, and the accent
// round it instead while the keyboard has the slider.
Rectangle {
    id: ring

    /// The slider this is the handle of.
    required property T.Slider control
    /// The colour of the ring.
    property color tint: AppColors.accent
    /// Across the ring.
    property real size: AppMetrics.sliderHandleSize
    /// Whether the ring answers the pointer: a step lighter under it, a step
    /// darker while it is held. A tile's face has never done so, and does
    /// not start in *Follow system*.
    property bool responsive: false

    x: ring.control.leftPadding
       + (ring.control.horizontal ? ring.control.visualPosition * (ring.control.availableWidth - ring.width)
                                  : (ring.control.availableWidth - ring.width) / 2)
    y: ring.control.topPadding
       + (ring.control.horizontal ? (ring.control.availableHeight - ring.height) / 2
                                  : ring.control.visualPosition * (ring.control.availableHeight - ring.height))
    implicitWidth: ring.size
    implicitHeight: ring.size
    radius: ring.width / 2
    color: !ring.responsive ? AppColors.surface
         : ring.control.pressed ? AppColors.surface2
         : ring.control.hovered ? Qt.lighter(AppColors.surface, 1.2)
         : AppColors.surface
    border.width: AppMetrics.sliderHandleBorder
    border.color: ring.control.visualFocus ? AppColors.accentMark : ring.tint
}
