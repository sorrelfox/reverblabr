// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import io.github.reverblabr

// What every field in the prototype sits in: the field colour inside
// a one-pixel border with a small corner, the border taking the accent
// while the field is being typed into or its list is open
// (`style-focus="border-color:var(--accent)"`). A text field, a list that
// drops down, a number and a text area all have it.
Rectangle {
    /// Whether the field has the keyboard, or its list is open.
    property bool active: false
    /// Whether the pointer is over it.
    property bool hovered: false

    implicitWidth: AppMetrics.fieldWidth
    implicitHeight: AppMetrics.mediumControlHeight
    radius: AppMetrics.controlRadius
    color: AppColors.field
    border.width: 1
    // **A step toward the text colour under the pointer**, which the
    // prototype leaves to the browser: enough to say the field is live,
    // not so much that it reads as the keyboard's.
    border.color: active ? AppColors.accentMark
                : hovered ? Qt.tint(AppColors.controlBorder, Qt.alpha(AppColors.muted, 0.35))
                : AppColors.controlBorder
}
