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

// Anything that pops up over the window, as the prototype's one
// popover is drawn: the settings panel's surface, border, corners, padding
// and resting shadow. A modal one dims the window behind it by the shadow's
// darkness.
T.Popup {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: AppMetrics.panelPadding

    background: Primitives.ShadowedRectangle {
        color: AppColors.surface
        radius: AppMetrics.panelRadius
        border.width: 1
        border.color: AppColors.border
        shadow.size: control.popupType === T.Popup.Item ? AppMetrics.shadowSize : 0
        shadow.yOffset: AppMetrics.shadowOffset
        shadow.color: AppColors.shadow
    }

    T.Overlay.modal: Rectangle {
        color: AppColors.shadow
    }

    T.Overlay.modeless: Rectangle {
        color: "transparent"
    }
}
