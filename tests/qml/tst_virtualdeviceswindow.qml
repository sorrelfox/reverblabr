// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Virtual Devices window: opened from the title bar's button and from
// the Settings menu, one of it however often it is asked for, framed as
// the main window is, and the size it was left at kept.
//
// **What only a compositor can show is not here**, as in tst_titlebar: that
// it sits above the main window and comes to the front is KWin's to do,
// and is checked in a real session.

import QtQuick
import QtQuick.Controls as QQC2
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "VirtualDevicesWindow"
    when: windowShown

    Component {
        id: mainWindow

        Main {}
    }

    function openWindow(): var {
        const window = createTemporaryObject(mainWindow, testCase);
        verify(window, "the main window is created");
        tryVerify(() => AppShell.hasSession, 1000, "the window came up with a session");
        return window;
    }

    /// The Settings menu of the menu bar, by its title.
    function settingsMenu(window: var): var {
        for (let i = 0; i < window.menus.count; ++i) {
            const menu = window.menus.menuAt(i);
            if (menu.title === "Se&ttings") {
                return menu;
            }
        }
        return null;
    }

    function cleanup() {
        Appearance.mode = SuiteAppearance;
    }

    function test_theButtonOpensTheWindow() {
        const window = testCase.openWindow();
        compare(window.virtualDevicesWindow, null, "the window was made before anybody asked for it");

        mouseClick(window.toolBar.virtualDevicesButton);
        tryVerify(() => window.virtualDevicesWindow !== null && window.virtualDevicesWindow.visible, 1000,
                  "the button opened no window");
        const devices = window.virtualDevicesWindow;
        compare(devices.title, "Virtual Devices");
        verify(devices.transientParent === window, "the window does not belong to the main window");
        verify(devices.placeholder.text.length > 0, "the empty window says nothing");
        devices.close();
    }

    function test_theSettingsMenuOpensTheSameWindow() {
        const window = testCase.openWindow();
        const settings = testCase.settingsMenu(window);
        verify(settings !== null, "no Settings menu");
        let found = false;
        for (let i = 0; i < settings.count; ++i) {
            const item = settings.itemAt(i);
            if (item.action === window.virtualDevicesAction) {
                found = true;
            }
        }
        verify(found, "Settings has no Virtual Devices entry");
        compare(window.virtualDevicesAction.text, "&Virtual Devices…");

        window.virtualDevicesAction.trigger();
        tryVerify(() => window.virtualDevicesWindow !== null && window.virtualDevicesWindow.visible, 1000,
                  "the menu entry opened no window");
        window.virtualDevicesWindow.close();
    }

    function test_askedForAgainTheOneWindowComesBack() {
        const window = testCase.openWindow();
        mouseClick(window.toolBar.virtualDevicesButton);
        tryVerify(() => window.virtualDevicesWindow !== null && window.virtualDevicesWindow.visible, 1000);
        const first = window.virtualDevicesWindow;

        // **Asked for while open**: the same window rather than a second
        // list of the same devices. That it comes to the front is KWin's to
        // show: offscreen, and in Qt's own terms, a window that belongs to
        // another is active whenever that one is, so `active` cannot tell.
        mouseClick(window.toolBar.virtualDevicesButton);
        verify(window.virtualDevicesWindow === first, "a second Virtual Devices window");
        verify(first.visible);

        // **Asked for after closing**: the same one, shown again.
        first.close();
        tryVerify(() => !first.visible, 1000, "the window did not close");
        window.virtualDevicesAction.trigger();
        verify(window.virtualDevicesWindow === first, "closing it lost the window");
        tryVerify(() => first.visible, 1000, "closed, the window did not come back");
        first.close();
    }

    function test_itIsFramedAsTheMainWindowIs() {
        const window = testCase.openWindow();
        window.virtualDevicesAction.trigger();
        tryVerify(() => window.virtualDevicesWindow !== null && window.virtualDevicesWindow.visible, 1000);
        const devices = window.virtualDevicesWindow;
        const frameless = (devices.flags & Qt.FramelessWindowHint) !== 0;
        compare(frameless, window.ownTitleBar, "the window's frame is not the main window's kind");
        compare(devices.titleBar.visible, window.ownTitleBar);
        compare(devices.chrome.active, window.ownTitleBar);
        if (window.ownTitleBar) {
            // **A title bar of its own**, with its three buttons and none of
            // the session's: nothing to run, no menus.
            compare(devices.menuBar, devices.titleBar);
            compare(devices.titleBar.height, AppMetrics.headerHeight);
            verify(devices.titleBar.closeButton.visible);
            verify(devices.titleBar.minimizeButton.visible);
            verify(devices.titleBar.maximizeButton.visible);
            compare(devices.chrome.resizeGrips.grips.count, 8);
            mouseClick(devices.titleBar.closeButton);
            tryVerify(() => !devices.visible, 1000, "the title bar's close did not close the window");
        } else {
            devices.close();
        }
    }

    function test_theWindowOpensAtTheSizeItWasLeftAt() {
        const window = testCase.openWindow();
        window.virtualDevicesAction.trigger();
        tryVerify(() => window.virtualDevicesWindow !== null && window.virtualDevicesWindow.visible, 1000);
        const devices = window.virtualDevicesWindow;
        devices.width = devices.minimumWidth + 37;
        devices.height = devices.minimumHeight + 23;
        tryCompare(devices, "width", devices.minimumWidth + 37);
        devices.close();
        const saved = WindowState.windowSize(devices.stateName);
        compare(saved.width, devices.minimumWidth + 37, "closing did not write the width down");
        compare(saved.height, devices.minimumHeight + 23, "closing did not write the height down");
    }
}
