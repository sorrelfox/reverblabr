// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Templates as T
import org.kde.kirigami.primitives as Primitives
import io.github.reverblabr

// A tooltip.
//
// **The prototype's hints are the browser's**, so it has no tooltip of its
// own to copy. This one is a small piece of its popover: the raised surface
// of a panel's header in a border, a control's corners, the caption text,
// and a lighter shadow than a panel's, since it floats less far. Its words
// wrap rather than run across the window, because some of the help that
// moved off the panels into tooltips is a sentence or two.
T.ToolTip {
    id: control

    x: parent ? (parent.width - implicitWidth) / 2 : 0
    y: -implicitHeight - AppMetrics.mediumSpacing

    implicitWidth: Math.min(Math.max(implicitBackgroundWidth + leftInset + rightInset,
                                     implicitContentWidth + leftPadding + rightPadding),
                            AppMetrics.toolTipMaximumWidth)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    margins: AppMetrics.mediumSpacing
    horizontalPadding: AppMetrics.largeSpacing
    verticalPadding: AppMetrics.smallSpacing
    font: AppFonts.small

    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent | T.Popup.CloseOnReleaseOutsideParent

    contentItem: Text {
        text: control.text
        font: control.font
        wrapMode: Text.Wrap
        color: AppColors.text
    }

    background: Primitives.ShadowedRectangle {
        color: AppColors.surface2
        radius: AppMetrics.controlRadius
        border.width: 1
        border.color: AppColors.border
        shadow.size: control.popupType === T.Popup.Item ? AppMetrics.shadowSize / 2 : 0
        shadow.yOffset: AppMetrics.shadowOffset / 2
        shadow.color: AppColors.shadow
    }
}
