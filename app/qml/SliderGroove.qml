// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Templates as T

// A slider's groove, as the prototype draws the one on a tile's face:
// a thin trough in the well colour, filled in the tint up to the
// value.
//
// **One drawing for every slider in the window.** The control style's
// Slider is this groove in the accent, and a tile's face is this groove in
// its block's tint, drawn by the face itself so that it looks the same
// whichever style the window was started with (FaceSlider).
Rectangle {
    id: groove

    /// The slider this is the groove of.
    required property T.Slider control
    /// What the groove is filled with up to the value.
    property color tint: AppColors.accent

    x: groove.control.leftPadding
       + (groove.control.horizontal ? 0 : (groove.control.availableWidth - groove.width) / 2)
    y: groove.control.topPadding
       + (groove.control.horizontal ? (groove.control.availableHeight - groove.height) / 2 : 0)
    implicitWidth: groove.control.horizontal ? AppMetrics.fieldWidth : AppMetrics.sliderGrooveHeight
    implicitHeight: groove.control.horizontal ? AppMetrics.sliderGrooveHeight : AppMetrics.fieldWidth
    width: groove.control.horizontal ? groove.control.availableWidth : groove.implicitWidth
    height: groove.control.horizontal ? groove.implicitHeight : groove.control.availableHeight
    radius: AppMetrics.troughRadius
    color: AppColors.well
    // **Filled from the end the value starts at**: the right, when the
    // window runs right to left.
    scale: groove.control.horizontal && groove.control.mirrored ? -1 : 1

    /// Where the fill starts, as a position along the groove: zero when the
    /// range straddles it, the start of the range otherwise.
    ///
    /// **A setting either side of nothing fills from nothing**. A
    /// Balance or a Pan at "L 30" is thirty steps from the middle, not
    /// seventy from the left edge, and a groove filled from the left edge
    /// would draw the one as the other; a gain of −6 dB on a ±12 dB control
    /// is a cut, not half a boost. A range that does not cross zero — a
    /// volume, a level in dBFS — fills from its start as before.
    readonly property real origin: groove.control.from < 0 && groove.control.to > 0
        ? (0 - groove.control.from) / (groove.control.to - groove.control.from)
        : 0
    readonly property real fillStart: Math.min(groove.origin, groove.control.position)
    readonly property real fillEnd: Math.max(groove.origin, groove.control.position)

    Rectangle {
        // From the bottom when upright, as a fader fills.
        x: groove.control.horizontal ? groove.fillStart * groove.width : 0
        y: groove.control.horizontal ? 0 : (1 - groove.fillEnd) * groove.height
        width: groove.control.horizontal ? (groove.fillEnd - groove.fillStart) * groove.width : groove.width
        height: groove.control.horizontal ? groove.height : (groove.fillEnd - groove.fillStart) * groove.height
        radius: groove.radius
        color: groove.tint
    }
}
