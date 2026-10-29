// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A window of ours other than the main one, built from nothing but
// WindowTitleBar and WindowChrome: what the Virtual Devices window
// is made of. It must be a whole title bar and frame on its own, with
// nothing of the session's in it.
//
// **What only a compositor can show is not here**, as in tst_titlebar: the
// move and the resize hand the pointer to it, and offscreen there is none,
// so what is held is that the window asks.

import QtQuick
import QtQuick.Controls as QQC2
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "WindowChrome"
    when: windowShown

    Component {
        id: bareWindow

        QQC2.ApplicationWindow {
            id: window

            /// Whether the window draws its own frame; fixed when it is made.
            property bool frameless: true

            readonly property alias chrome: chrome
            readonly property alias titleBar: bar
            readonly property alias titleLabel: label

            width: 480
            height: 320
            visible: true
            title: "A window of ours"
            flags: window.frameless ? (Qt.Window | Qt.FramelessWindowHint) : Qt.Window
            color: window.frameless ? "transparent" : AppColors.window

            menuBar: WindowTitleBar {
                id: bar

                titleBar: window.frameless
                frameRadius: chrome.frameRadius
                maximized: chrome.edgeToEdge
                onWindowMenuRequested: chrome.showWindowMenu()

                QQC2.Label {
                    id: label

                    text: window.title
                }
            }

            WindowChrome {
                id: chrome

                window: window
                active: window.frameless
            }
        }
    }

    // The suite's appearance, whatever a test before this one left.
    function init() {
        Appearance.mode = SuiteAppearance;
    }

    function cleanup() {
        Appearance.mode = SuiteAppearance;
    }

    function open(frameless: bool): var {
        const window = createTemporaryObject(bareWindow, testCase, { frameless: frameless });
        verify(window, "the window is created");
        // Laid out: the title bar has its height and the title its width.
        tryVerify(() => window.titleBar.height > 0 && window.titleLabel.width > 0, 1000,
                  "the title bar was never laid out");
        return window;
    }

    /// Where \a item sits along the title bar, left to right.
    function xOf(window: var, item: var): real {
        return item.mapToItem(window.titleBar, 0, 0).x;
    }

    /// A point on the title bar that nothing in it takes: past the title.
    function emptyPoint(window: var): point {
        const bar = window.titleBar;
        const titleEnd = testCase.xOf(window, window.titleLabel) + window.titleLabel.width;
        return Qt.point((titleEnd + testCase.xOf(window, bar.minimizeButton)) / 2, bar.height / 2);
    }

    function test_theWindowsOwnContentThenTheWindowsThreeButtons() {
        const window = testCase.open(true);
        const bar = window.titleBar;
        compare(bar.Accessible.role, Accessible.TitleBar);
        if (!AppMetrics.followsSystem) {
            compare(bar.height, AppMetrics.headerHeight);
        }
        // **The title is the window's**, in the slot the bar leaves for it,
        // and the three buttons end the row in their order.
        verify(window.titleLabel.visible, "the window's own content is not in its title bar");
        const order = [window.titleLabel, bar.minimizeButton, bar.maximizeButton, bar.closeButton];
        for (let i = 1; i < order.length; ++i) {
            verify(testCase.xOf(window, order[i]) > testCase.xOf(window, order[i - 1]), "out of order at " + i);
        }
        for (const button of [bar.minimizeButton, bar.maximizeButton, bar.closeButton]) {
            verify(button.visible);
            compare(button.width, AppMetrics.windowButtonSize);
            compare(button.height, AppMetrics.windowButtonSize);
        }
        // **No group of buttons the window did not give it**: none showing,
        // and no pair of rules round an empty space.
        verify(!bar.buttonsShown, "an empty button group in a title bar that was given no buttons");
        compare(bar.closeButton.Accessible.name, "Close");
    }

    function test_theFrameIsDrawnRoundTheWholeWindowAndOverItsTitleBar() {
        const window = testCase.open(true);
        const chrome = window.chrome;
        verify((window.flags & Qt.FramelessWindowHint) !== 0, "the window kept KWin's frame");
        verify(chrome.visible);
        // **Beside the title bar, over it**: in the window's root item, not
        // in the content under the header.
        compare(chrome.parent, window.titleBar.parent, "the frame is not in the window's root item");
        verify(chrome.z > window.titleBar.z, "the title bar is drawn over the frame");
        compare(chrome.resizeGrips.width, window.width);
        compare(chrome.resizeGrips.height, window.height);
        compare(chrome.resizeGrips.grips.count, 8);
        verify(chrome.resizeGrips.enabled);
        compare(chrome.frameRadius, AppMetrics.windowRadius);
        compare(window.titleBar.frameRadius, AppMetrics.windowRadius);
        compare(chrome.frameBorder.border.width, 1);
        compare(chrome.frameBorder.radius, AppMetrics.windowRadius);
        verify(chrome.windowFrame.shadowed, "no shadow asked for");
        // **The background is the window's shape**: rounded, in the window
        // colour, and transparent round it.
        compare(window.background.radius, AppMetrics.windowRadius);
        verify(Qt.colorEqual(window.background.color, AppColors.window), "the background is not the window colour");
    }

    function test_theMaximiseButtonMaximisesAndTheFrameGoesSquare() {
        const window = testCase.open(true);
        const bar = window.titleBar;
        const chrome = window.chrome;
        mouseClick(bar.maximizeButton);
        tryVerify(() => chrome.edgeToEdge, 1000, "the maximise button did not maximise");
        verify(bar.maximized, "the title bar does not know the window is maximised");
        compare(bar.maximizeButton.text, "Restore");
        compare(chrome.frameRadius, 0);
        compare(chrome.frameBorder.border.width, 0);
        verify(!chrome.windowFrame.shadowed, "a shadow round a maximised window");
        verify(!chrome.resizeGrips.enabled, "a maximised window's edges still resize it");

        mouseClick(bar.maximizeButton);
        tryVerify(() => !chrome.edgeToEdge, 1000, "the restore button did not restore");
        compare(chrome.frameRadius, AppMetrics.windowRadius);
    }

    function test_theEmptyTitleBarMovesAndMaximisesTheWindow() {
        const window = testCase.open(true);
        const bar = window.titleBar;
        const at = testCase.emptyPoint(window);
        mouseDoubleClickSequence(bar, at.x, at.y);
        tryVerify(() => window.chrome.edgeToEdge, 1000, "a double click did not maximise");
        mouseDoubleClickSequence(bar, at.x, at.y);
        tryVerify(() => !window.chrome.edgeToEdge, 1000, "a second double click did not restore");

        // **The title is part of the handle**: a label takes no presses, so
        // a drag that starts on it moves the window, as on any title bar.
        const far = Application.styleHints.startDragDistance + 4;
        const onTitle = window.titleLabel.mapToItem(bar, window.titleLabel.width / 2, window.titleLabel.height / 2);
        mousePress(bar, onTitle.x, onTitle.y);
        mouseMove(bar, onTitle.x + far, onTitle.y);
        verify(bar.emptySpace.moving, "a drag on the title did not move the window");
        mouseRelease(bar, onTitle.x + far, onTitle.y);
    }

    function test_aRightClickAndAltSpaceOpenTheWindowMenu() {
        const window = testCase.open(true);
        const menu = window.chrome.windowMenu;
        const at = testCase.emptyPoint(window);
        // **Offscreen there is no compositor to ask**, so the menu is ours.
        mouseClick(window.titleBar, at.x, at.y, Qt.RightButton);
        tryVerify(() => menu.opened, 1000, "a right click opened no window menu");
        const words = [];
        for (let i = 0; i < menu.count; ++i) {
            const item = menu.itemAt(i);
            if (!(item instanceof QQC2.MenuSeparator)) {
                words.push(item.text);
            }
        }
        compare(words, ["Minimize", "Maximize", "Close"]);
        menu.close();
        tryVerify(() => !menu.visible, 1000);

        window.requestActivate();
        tryVerify(() => window.active, 1000, "the window did not become active");
        keyClick(Qt.Key_Space, Qt.AltModifier);
        tryVerify(() => menu.opened, 1000, "Alt+Space opened no window menu");
        menu.close();
    }

    function test_theWindowMenusMaximiseIsTheWindows() {
        const window = testCase.open(true);
        const menu = window.chrome.windowMenu;
        window.chrome.showWindowMenu();
        tryVerify(() => menu.opened, 1000);
        menu.itemAt(1).triggered();
        tryVerify(() => window.chrome.edgeToEdge, 1000, "the window menu's Maximize did not maximise");
        compare(menu.itemAt(1).text, "Restore");
    }

    function test_theCloseButtonClosesTheWindow() {
        const window = testCase.open(true);
        mouseClick(window.titleBar.closeButton);
        tryVerify(() => !window.visible, 1000, "the close button left the window open");
    }

    function test_underKWinsFrameItIsOnlyARow() {
        // **Following the system, KWin draws the title bar and the frame**:
        // the window keeps its frame and none of ours shows.
        const window = testCase.open(false);
        const bar = window.titleBar;
        compare(window.flags & Qt.FramelessWindowHint, 0);
        verify(!window.chrome.visible, "our frame over KWin's");
        verify(!window.chrome.windowFrame.shadowed, "a shadow of ours under KWin's");
        compare(window.chrome.frameRadius, 0);
        compare(bar.Accessible.role, Accessible.ToolBar);
        verify(!bar.minimizeButton.visible && !bar.maximizeButton.visible && !bar.closeButton.visible,
               "window buttons beside KWin's");
        verify(!bar.emptySpace.enabled, "the row moves a window KWin frames");
        verify(window.titleLabel.visible, "the window's own content went with the title bar");
    }
}
