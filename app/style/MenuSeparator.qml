// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Templates as T
import io.github.reverblabr

// The line between the groups of a menu: the border colour, as every
// rule the prototype draws is, with a little room above and below.
T.MenuSeparator {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    horizontalPadding: AppMetrics.smallSpacing
    verticalPadding: AppMetrics.smallSpacing

    contentItem: Rectangle {
        implicitWidth: AppMetrics.menuMinimumWidth - AppMetrics.menuPadding * 2
        implicitHeight: 1
        color: AppColors.border
    }
}
