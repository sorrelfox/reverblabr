// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import io.github.reverblabr

// The ring round a control that has the keyboard.
//
// **The prototype draws no focus at all on a button**; it has a mouse. The
// ring is the one it draws round a chosen tile, a soft band of the accent
// outside the edge (`box-shadow: 0 0 0 2px var(--accent-soft)`), so that
// the keyboard's place is marked in the design's own way rather than in a
// second one. It sits outside the control, where it covers nothing.
Rectangle {
    /// The shape it goes round: a control's background.
    required property real controlRadius
    /// Whether the control it goes round has the keyboard.
    required property bool shown

    visible: shown
    anchors.fill: parent
    anchors.margins: -AppMetrics.focusRingWidth
    radius: controlRadius + AppMetrics.focusRingWidth
    color: "transparent"
    border.width: AppMetrics.focusRingWidth
    border.color: AppColors.accentSoft
}
