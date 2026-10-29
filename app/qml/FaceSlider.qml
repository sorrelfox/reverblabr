// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls as QQC2

// The slider on a tile's face: the style's groove and ring, in the
// block's tint and at the face's size.
//
// **Drawn here, not left to the style**, because a face is the design's
// own drawing in every appearance: under *Follow system* the desktop's
// slider would put a desktop control in the middle of a tile, which is the
// one place the application has always drawn itself. Under the Reverblabr
// style it is the same groove and ring the style draws a panel's slider
// with, only smaller and in the tint.
QQC2.Slider {
    id: slider

    /// The block's colour, which the groove fills with and the ring is in.
    property color tint: AppColors.accent

    implicitHeight: ring.height
    padding: 0

    background: SliderGroove {
        control: slider
        tint: slider.tint
    }

    handle: SliderHandle {
        id: ring

        control: slider
        tint: slider.tint
        size: AppMetrics.faceSliderHandleSize
    }
}
