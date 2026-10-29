// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.ki18n

// The title bar of a window with no frame of KWin's, for any window
// of ours: whatever the window puts in it, then the window's own
// buttons, as the prototype draws them.
//
// **What a title bar does** is here, once: the empty part of the row moves
// the window, a double click maximises it, a right click asks for the window
// menu, and minimise, maximise and close end the row. What the row shows is
// the window's: the main window puts the session tabs, the transport and its
// theme and menu buttons in it; another window puts its title.
//
// **Only a row, while following the system** (ADR-0012): KWin draws the
// title bar then, so `titleBar` is false and this is a toolbar with the
// window's content in it and none of the above.
//
// ```qml
// QQC2.ApplicationWindow {
//     flags: chrome.active ? (Qt.Window | Qt.FramelessWindowHint) : Qt.Window
//     color: chrome.active ? "transparent" : AppColors.window
//     menuBar: WindowTitleBar {
//         titleBar: chrome.active
//         frameRadius: chrome.frameRadius
//         maximized: chrome.edgeToEdge
//         onWindowMenuRequested: chrome.showWindowMenu()
//         QQC2.Label { text: window.title }
//     }
//     WindowChrome { id: chrome; window: root; active: Appearance.ownStyle }
// }
// ```
QQC2.ToolBar {
    id: bar

    /// Drawn as the window's title bar: the buttons and gestures a title bar
    /// has. Fixed for the window's life, like the control style.
    property bool titleBar: false
    /// The window's corner, which the title bar's own top corners follow.
    property real frameRadius: 0
    /// Whether the window is drawn maximised: the maximise button then
    /// restores it. The window's to say, which knows its own state.
    property bool maximized: false

    /// What the row shows, left to right; it takes the room the buttons
    /// leave. The part of it nothing is drawn on moves the window.
    default property alias content: contentRow.data
    /// Buttons of the window's own before minimise, maximise and close, set
    /// apart by a rule on each side in a title bar (the main window's theme
    /// and menu buttons).
    property alias buttons: buttonRow.data
    /// Whether that group shows. Every button in it says for itself whether
    /// it does; a group with none showing would still take its room.
    property bool buttonsShown: bar.titleBar && buttonRow.children.length > 0

    /// The window this is the title bar of. The attached property hands back
    /// the window itself, which QML knows by another type than `Window`.
    readonly property var window: bar.Window.window

    /// A right click on the empty part of the title bar: the window menu,
    /// which the window asks the compositor for.
    signal windowMenuRequested()

    function toggleMaximized() {
        if (bar.maximized) {
            bar.window.showNormal();
        } else {
            bar.window.showMaximized();
        }
    }

    /// The title bar's own parts, for the tests.
    readonly property alias minimizeButton: minimizeButton
    readonly property alias maximizeButton: maximizeButton
    readonly property alias closeButton: closeButton
    readonly property alias emptySpace: emptySpace

    // **The prototype's 40 pixels** in the Reverblabr appearance; the
    // row's own height under KWin's title bar.
    //
    // **And a title bar's, whatever the appearance.** Picking Follow system
    // leaves the window frameless till the next start, and its bar was then
    // left to the style's height, which for this background is none: the
    // window's only handle, and its only close button, folded to nothing.
    Binding on implicitHeight {
        when: bar.titleBar || !AppMetrics.followsSystem
        value: AppMetrics.headerHeight
    }

    Accessible.role: bar.titleBar ? Accessible.TitleBar : Accessible.ToolBar

    // **The window's top corners are the title bar's**: with no frame, the
    // window is transparent outside its rounded shape, and a square bar
    // would fill the corners back in. The hairline under it stays.
    Binding {
        target: bar
        property: "background"
        when: bar.titleBar
        value: Kirigami.ShadowedRectangle {
            color: AppColors.window
            corners.topLeftRadius: bar.frameRadius
            corners.topRightRadius: bar.frameRadius

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: AppColors.border
            }
        }
    }

    // **The empty part of the title bar is the window's handle**.
    // Behind everything in the row, so a tab, a button or the clock takes its
    // own presses and only what lands on nothing reaches these: a drag moves
    // the window, a double click maximises it, a right click opens the
    // window menu — what a title bar does anywhere else.
    MouseArea {
        id: emptySpace

        /// Where the press was, and whether it has become a move yet: once
        /// the compositor has the pointer, the rest of the drag is its.
        property point pressedAt
        property bool moving: false

        anchors.fill: parent
        z: -1
        enabled: bar.titleBar
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: mouse => {
            emptySpace.pressedAt = Qt.point(mouse.x, mouse.y);
            emptySpace.moving = false;
            if (mouse.button === Qt.RightButton) {
                bar.windowMenuRequested();
            }
        }
        onPositionChanged: mouse => {
            if (emptySpace.moving || !(emptySpace.pressedButtons & Qt.LeftButton)) {
                return;
            }
            const dx = mouse.x - emptySpace.pressedAt.x;
            const dy = mouse.y - emptySpace.pressedAt.y;
            if (Math.hypot(dx, dy) >= Application.styleHints.startDragDistance) {
                emptySpace.moving = true;
                bar.window.startSystemMove();
            }
        }
        onDoubleClicked: mouse => {
            if (mouse.button === Qt.LeftButton) {
                bar.toggleMaximized();
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        // **Nothing against the window's edge**. The prototype's 6
        // and 8 in the Reverblabr appearance, with its 14 between groups.
        anchors.leftMargin: AppMetrics.followsSystem ? Kirigami.Units.largeSpacing : AppMetrics.mediumSpacing
        anchors.rightMargin: Kirigami.Units.largeSpacing
        spacing: AppMetrics.followsSystem ? Kirigami.Units.largeSpacing : AppMetrics.groupSpacing

        RowLayout {
            id: contentRow

            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: AppMetrics.followsSystem ? Kirigami.Units.largeSpacing : AppMetrics.groupSpacing
        }

        Rectangle {
            visible: bar.titleBar && bar.buttonsShown
            implicitWidth: 1
            implicitHeight: AppMetrics.headerDividerHeight
            color: AppColors.border
        }

        RowLayout {
            id: buttonRow

            visible: bar.buttonsShown
            spacing: AppMetrics.hairSpacing
        }

        Rectangle {
            visible: bar.titleBar
            implicitWidth: 1
            implicitHeight: AppMetrics.headerDividerHeight
            color: AppColors.border
        }

        RowLayout {
            visible: bar.titleBar
            spacing: AppMetrics.hairSpacing

            WindowButton {
                id: minimizeButton

                glyph: AppGlyphs.minimize
                glyphSize: AppMetrics.windowGlyphSize
                text: KI18n.i18nc("@action:button window", "Minimize")
                onClicked: bar.window.showMinimized()
            }

            WindowButton {
                id: maximizeButton

                glyph: bar.maximized ? AppGlyphs.restore : AppGlyphs.maximize
                glyphSize: AppMetrics.windowGlyphSize
                text: bar.maximized
                    ? KI18n.i18nc("@action:button window", "Restore")
                    : KI18n.i18nc("@action:button window", "Maximize")
                onClicked: bar.toggleMaximized()
            }

            // **The same close as the compositor's**: the window's own, so
            // whatever the window does on the way out — saving its size, the
            // open sessions — happens here too.
            WindowButton {
                id: closeButton

                glyph: AppGlyphs.close
                glyphSize: AppMetrics.windowGlyphSize
                destructive: true
                text: KI18n.i18nc("@action:button window", "Close")
                onClicked: bar.window.close()
            }
        }
    }
}
