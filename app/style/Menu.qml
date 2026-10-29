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

// A menu ([ADR-0012]).
//
// **The prototype defines its menus and never opens one**, so a menu is
// drawn as the one popover it does draw, the settings panel: the surface in
// a border, the panel's six-pixel corners and its resting shadow, and rows
// at a control's height inside a small margin.
//
// **Every colour is a token, none is the palette's.** Qt gives a menu a
// palette of its own from the desktop — on a light desktop, a light one —
// and a menu that drew with it opened light in a dark window.
T.Menu {
    id: control

    /// Whether any row can be ticked, so that every row keeps room for the
    /// tick and the words of all of them start in one column.
    readonly property bool hasCheckables: {
        for (let i = 0; i < control.count; ++i) {
            const item = control.itemAt(i) as T.MenuItem;
            if (item && item.checkable) {
                return true;
            }
        }
        return false;
    }

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    margins: 0
    padding: AppMetrics.menuPadding
    // A menu opened from a row of another sits over that menu's margin, so
    // the rows of the two line up.
    overlap: AppMetrics.menuPadding
    font: AppFonts.body

    delegate: MenuItem {}

    contentItem: ListView {
        // **As wide as the widest row**, so a row's words and its key are
        // never cut short; a list view has no width of its own to offer.
        implicitWidth: Math.max(contentWidth,
                                contentItem.children.reduce((widest, row) => Math.max(widest, row.implicitWidth), 0))
        implicitHeight: contentHeight
        model: control.contentModel
        interactive: Window.window
                     ? contentHeight + control.topPadding + control.bottomPadding > control.height
                     : false
        clip: true
        currentIndex: control.currentIndex

        ScrollIndicator.vertical: ScrollIndicator {}
    }

    background: Primitives.ShadowedRectangle {
        implicitWidth: AppMetrics.menuMinimumWidth
        implicitHeight: AppMetrics.mediumControlHeight
        color: AppColors.surface
        radius: AppMetrics.panelRadius
        border.width: 1
        border.color: AppColors.border
        // **A shadow only inside the window.** A menu in a window of its
        // own gets the compositor's, and one drawn here would be cut off at
        // that window's edge.
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
