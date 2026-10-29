// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Templates as T
import io.github.reverblabr

// A pane: the one the prototype draws, the zoom control floating in
// the canvas's corner — the surface in a border with a control's corners,
// holding its buttons two pixels in.
T.Pane {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: AppMetrics.hairSpacing

    background: Rectangle {
        color: AppColors.surface
        radius: AppMetrics.controlRadius
        border.width: 1
        border.color: AppColors.border
    }
}
