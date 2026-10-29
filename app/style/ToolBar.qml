// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Templates as T
import io.github.reverblabr

// A bar across the window: the prototype's header and status bar,
// which are the window's own colour with a hairline in the border colour
// on the side that faces the content — under the header, over the status
// bar. Basic fills a bar with the button colour, which is the raised
// surface here and would make both a band of their own.
T.ToolBar {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    background: Rectangle {
        implicitHeight: AppMetrics.controlHeight
        color: AppColors.window

        Rectangle {
            width: parent.width
            height: 1
            y: control.position === T.ToolBar.Footer ? 0 : parent.height - 1
            color: AppColors.border
        }
    }
}
