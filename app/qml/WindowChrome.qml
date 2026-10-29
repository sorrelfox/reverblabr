// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.ki18n

// **The window's shape and its edges**, for a window of ours with no frame:
// the rounded background, the hairline round it, the grips
// that resize it, the compositor's shadow, and the window menu with its
// Alt+Space. What a frame gives a window, less the title bar, which is a
// WindowTitleBar in the window's top slot.
//
// **Declared inside the window, drawn over all of it**: it moves itself to
// the window's root item, beside the header and the footer, and stays over
// everything the window draws and under its popups. None of it shows while
// `active` is false, which is when KWin frames the window.
//
// The window keeps two things for itself, because they are fixed when it is
// made: its `flags` (frameless while `active`) and a transparent `color`
// round the rounded corners.
Item {
    id: chrome

    /// The window this is the frame of.
    required property QQC2.ApplicationWindow window
    /// Whether the window draws its own frame: in the Reverblabr
    /// appearances, fixed for the window's life with the control style.
    property bool active: false
    /// Drawn as a maximised window is: square, with no border and no
    /// shadow, as the prototype draws one. Writable, so the design
    /// comparison can ask for the look it compares against.
    property bool edgeToEdge: chrome.window.visibility === Window.Maximized
                              || chrome.window.visibility === Window.FullScreen
    /// The window's corner: the prototype's 8 pixels while it has a frame of
    /// its own drawing, none while it is maximised or KWin frames it.
    readonly property real frameRadius: chrome.active && !chrome.edgeToEdge ? AppMetrics.windowRadius : 0

    /// The parts, for the tests.
    readonly property alias frameBorder: frameBorder
    readonly property alias resizeGrips: resizeGrips
    readonly property alias windowFrame: windowFrame
    readonly property alias windowMenu: windowMenu

    /// The window menu: the compositor's own where it can be asked for — it
    /// alone can keep a window above the others or move it to another
    /// desktop — and a smaller one of ours where it cannot.
    function showWindowMenu() {
        if (!windowFrame.showWindowMenu()) {
            windowMenu.popup();
        }
    }

    function toggleMaximized() {
        if (chrome.edgeToEdge) {
            chrome.window.showNormal();
        } else {
            chrome.window.showMaximized();
        }
    }

    // The window's root item, beside the header and the footer, rather than
    // the content between them. It has no size of its own — the window lays
    // its slots out by hand — so this takes the window's.
    parent: chrome.Window.contentItem
    width: chrome.window.width
    height: chrome.window.height
    z: 1000
    visible: chrome.active

    Rectangle {
        id: frameBorder

        anchors.fill: parent
        color: "transparent"
        radius: chrome.frameRadius
        border.width: chrome.edgeToEdge ? 0 : 1
        border.color: AppColors.border
    }

    ResizeGrips {
        id: resizeGrips

        anchors.fill: parent
        window: chrome.window
        enabled: !chrome.edgeToEdge
    }

    // **The design's shadow, drawn by the compositor**, and its window menu.
    WindowFrame {
        id: windowFrame

        window: chrome.window
        shadowed: chrome.active && !chrome.edgeToEdge
        radius: AppMetrics.windowRadius
    }

    // **Ours, where the compositor's cannot be asked for**: what a window
    // can do to itself. Keeping it above the others, or moving it to another
    // desktop, is the compositor's alone.
    QQC2.Menu {
        id: windowMenu

        QQC2.MenuItem {
            text: KI18n.i18nc("@action:inmenu window", "Minimize")
            onTriggered: chrome.window.showMinimized()
        }

        QQC2.MenuItem {
            text: chrome.edgeToEdge
                ? KI18n.i18nc("@action:inmenu window", "Restore")
                : KI18n.i18nc("@action:inmenu window", "Maximize")
            onTriggered: chrome.toggleMaximized()
        }

        QQC2.MenuSeparator {}

        QQC2.MenuItem {
            text: KI18n.i18nc("@action:inmenu window", "Close")
            icon.name: "window-close"
            onTriggered: chrome.window.close()
        }
    }

    // **Alt+Space opens the window menu**, as it does on a window with a
    // title bar — where the desktop has not taken the key for itself.
    Shortcut {
        sequence: "Alt+Space"
        enabled: chrome.active
        onActivated: chrome.showWindowMenu()
    }

    // **The background is the window's shape** in a window with no frame:
    // rounded, and transparent outside its corners. The style's own while
    // KWin frames the window.
    Binding {
        target: chrome.window
        property: "background"
        when: chrome.active
        value: Rectangle {
            color: AppColors.window
            radius: chrome.frameRadius
        }
    }
}
