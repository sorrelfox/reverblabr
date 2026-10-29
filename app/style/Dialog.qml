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

// A dialog, drawn as the prototype's popover: the settings panel's
// card, with its header — the title in the strong text on the raised
// surface, a rule under it — and the buttons at the foot.
//
// The application's own dialogs are Kirigami's, which draw their card from
// Kirigami's colours and corner (the platform plugin gives it the design's)
// and use the button box and buttons below. This is for a plain one.
T.Dialog {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding,
                            implicitHeaderWidth,
                            implicitFooterWidth)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding
                             + (implicitHeaderHeight > 0 ? implicitHeaderHeight + spacing : 0)
                             + (implicitFooterHeight > 0 ? implicitFooterHeight + spacing : 0))

    padding: AppMetrics.panelPadding

    background: Primitives.ShadowedRectangle {
        color: AppColors.surface
        radius: AppMetrics.panelRadius
        border.width: 1
        border.color: AppColors.border
        shadow.size: control.popupType === T.Popup.Item ? AppMetrics.liftedShadowSize : 0
        shadow.yOffset: AppMetrics.liftedShadowOffset
        shadow.color: AppColors.liftedShadow
    }

    header: T.Label {
        text: control.title
        visible: control.title !== ""
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
        leftPadding: AppMetrics.panelPadding
        rightPadding: AppMetrics.panelPadding
        font: AppFonts.strong
        color: AppColors.text

        background: Rectangle {
            implicitHeight: AppMetrics.panelHeaderHeight
            // The card's top corners, and square below where the rule is.
            topLeftRadius: AppMetrics.panelRadius
            topRightRadius: AppMetrics.panelRadius
            color: AppColors.surface2

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: AppColors.border
            }
        }
    }

    footer: DialogButtonBox {
        visible: count > 0
    }

    T.Overlay.modal: Rectangle {
        color: AppColors.shadow
    }

    T.Overlay.modeless: Rectangle {
        color: "transparent"
    }
}
