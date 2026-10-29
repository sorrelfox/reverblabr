// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The window's own title bar: in the Reverblabr appearances the window
// has no frame and the header is its title bar, as the prototype draws it;
// with *Follow system* KWin frames it and nothing here shows. The suite runs
// under both (tst_qml.cpp), so each test says what it holds in each.
//
// **What only a compositor can show is not here**: moving and resizing hand
// the pointer to it, and the shadow and the window menu are its to draw.
// Those are checked in a real session; this holds what the window asks for.

import QtQuick
import QtQuick.Controls as QQC2
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "TitleBar"
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

    /// Where \a item sits along the header, left to right.
    function xOf(window: var, item: var): real {
        return item.mapToItem(window.toolBar, 0, 0).x;
    }

    function cleanup() {
        Appearance.mode = SuiteAppearance;
        WindowState.setMenuBarShown(true, false);
        WindowState.setMenuBarShown(false, true);
    }

    function test_theHeaderIsTheTitleBarOnlyInTheReverblabrAppearances() {
        const window = testCase.openWindow();
        const bar = window.toolBar;
        const frameless = (window.flags & Qt.FramelessWindowHint) !== 0;
        compare(frameless, window.ownTitleBar, "the window's frame is not the appearance's");
        compare(bar.titleBar, window.ownTitleBar);
        compare(bar.minimizeButton.visible, window.ownTitleBar);
        compare(bar.maximizeButton.visible, window.ownTitleBar);
        compare(bar.closeButton.visible, window.ownTitleBar);
        compare(bar.themeButton.visible, window.ownTitleBar);
        compare(window.resizeGrips.visible, window.ownTitleBar);
        compare(window.windowFrame.shadowed, window.ownTitleBar && !window.edgeToEdge);
        if (!window.ownTitleBar) {
            // **Follow system is the window it was**: KWin's frame, the menu
            // bar on top and the toolbar row under it.
            compare(window.menuBar, window.menus);
            compare(window.header, bar);
            verify(window.menus.visible, "the menu bar went missing under KWin's title bar");
            verify(!bar.menuButton.visible, "a menu button beside a menu bar that is up");
            return;
        }
        // **The title bar is the top row**, and the menu bar, hidden until
        // somebody asks for it, would come under it.
        compare(window.menuBar, bar);
        compare(window.header, window.menus);
        verify(!window.menus.visible, "the design's title bar has a menu bar nobody asked for");
        verify(bar.menuButton.visible, "no way to the menus with the menu bar hidden");
        compare(bar.height, AppMetrics.headerHeight);
        compare(bar.Accessible.role, Accessible.TitleBar);
    }

    function test_theTitleBarsEndIsThePrototypesInItsOrder() {
        const window = testCase.openWindow();
        if (!window.ownTitleBar) {
            skip("KWin draws the title bar while following the system");
        }
        const bar = window.toolBar;
        // Status, the clock, Run, then the theme, the menu, the virtual
        // devices, and the window's own three, each to the right of
        // the one before.
        const order = [bar.status, bar.clock, bar.runButton, bar.themeButton, bar.menuButton,
                       bar.virtualDevicesButton, bar.minimizeButton, bar.maximizeButton, bar.closeButton];
        for (let i = 1; i < order.length; ++i) {
            verify(testCase.xOf(window, order[i]) > testCase.xOf(window, order[i - 1]),
                   "out of order at " + i);
        }
        for (const button of [bar.themeButton, bar.menuButton, bar.virtualDevicesButton, bar.minimizeButton,
                              bar.maximizeButton, bar.closeButton]) {
            compare(button.width, AppMetrics.windowButtonSize);
            compare(button.height, AppMetrics.windowButtonSize);
        }
        // **Nothing against the window's edge**: the close button stands in
        // from it by the prototype's padding.
        const closeRight = testCase.xOf(window, bar.closeButton) + bar.closeButton.width;
        verify(closeRight <= bar.width - AppMetrics.mediumSpacing, "close is against the window's edge");
    }

    function test_theWindowButtonsSayWhatTheyAre() {
        const window = testCase.openWindow();
        if (!window.ownTitleBar) {
            skip("KWin's buttons, not ours, while following the system");
        }
        const bar = window.toolBar;
        compare(bar.minimizeButton.Accessible.name, "Minimize");
        compare(bar.maximizeButton.Accessible.name, "Maximize");
        compare(bar.closeButton.Accessible.name, "Close");
        compare(bar.menuButton.Accessible.name, "Menu");
        compare(bar.virtualDevicesButton.Accessible.name, "Virtual Devices");
        compare(bar.closeButton.Accessible.role, Accessible.Button);
        compare(bar.menuButton.Accessible.role, Accessible.ButtonMenu);
        compare(bar.virtualDevicesButton.Accessible.role, Accessible.Button);
        // Reached with Tab, like any other control.
        for (const button of [bar.themeButton, bar.menuButton, bar.virtualDevicesButton, bar.minimizeButton,
                              bar.maximizeButton, bar.closeButton]) {
            verify(button.focusPolicy & Qt.TabFocus, button.text + " cannot be reached with Tab");
        }
    }

    function test_theMenuButtonHoldsEveryMenuTheMenuBarHas() {
        const window = testCase.openWindow();
        const bar = window.menus;
        const button = window.mainMenu;
        // **The same menus, entry for entry**: the button's are a second set
        // of the bar's (a menu cannot be in both), so they are held together.
        function entries(menu: var): var {
            const list = [];
            for (let i = 0; i < menu.count; ++i) {
                const item = menu.itemAt(i);
                const sub = menu.menuAt(i);
                if (sub !== null) {
                    list.push({ title: sub.title, entries: entries(sub) });
                } else if (item instanceof QQC2.MenuSeparator) {
                    list.push("separator");
                } else {
                    list.push(item.action);
                }
            }
            return list;
        }
        compare(button.count, bar.count);
        for (let i = 0; i < bar.count; ++i) {
            const fromBar = bar.menuAt(i);
            const fromButton = button.menuAt(i);
            compare(fromButton.title, fromBar.title);
            const a = entries(fromBar);
            const b = entries(fromButton);
            compare(b.length, a.length, fromBar.title);
            for (let j = 0; j < a.length; ++j) {
                if (a[j].title !== undefined) {
                    compare(b[j].title, a[j].title);
                    compare(b[j].entries.length, a[j].entries.length);
                    for (let k = 0; k < a[j].entries.length; ++k) {
                        verify(b[j].entries[k] === a[j].entries[k], fromBar.title + " > " + a[j].title + " " + k);
                    }
                } else {
                    verify(b[j] === a[j], fromBar.title + ": entry " + j + " is not the menu bar's");
                }
            }
        }
    }

    function test_ctrlMShowsAndHidesTheMenuBarAndTheButtonTakesItsPlace() {
        const window = testCase.openWindow();
        const shownAtStart = window.menus.visible;
        compare(window.showMenuBarAction.checked, shownAtStart);
        compare(window.toolBar.menuButton.visible, !shownAtStart);

        keyClick(Qt.Key_M, Qt.ControlModifier);
        compare(window.menus.visible, !shownAtStart);
        compare(window.toolBar.menuButton.visible, shownAtStart);
        compare(WindowState.menuBarShown(window.ownTitleBar), !shownAtStart, "the choice was not kept");

        keyClick(Qt.Key_M, Qt.ControlModifier);
        compare(window.menus.visible, shownAtStart);
        compare(window.toolBar.menuButton.visible, !shownAtStart);
    }

    function test_theShortcutsWorkWithTheMenuBarHidden() {
        const window = testCase.openWindow();
        if (window.menus.visible) {
            keyClick(Qt.Key_M, Qt.ControlModifier);
        }
        verify(!window.menus.visible);
        // A key from each menu, none of them anywhere on screen.
        keyClick(Qt.Key_2, Qt.ControlModifier);
        compare(window.sidebarIndex, 1, "View's Ctrl+2 did not show the Files tab");
        keyClick(Qt.Key_3, Qt.ControlModifier);
        compare(window.sidebarIndex, 2, "View's Ctrl+3 did not show the Info tab");
        keyClick(Qt.Key_1, Qt.ControlModifier);
        compare(window.sidebarIndex, 0, "View's Ctrl+1 did nothing");
        const before = Sessions.open.length;
        keyClick(Qt.Key_N, Qt.ControlModifier);
        tryCompare(Sessions.open, "length", before + 1, 1000, "Session's Ctrl+N did nothing");
        Sessions.closeSession(Sessions.currentId);
        keyClick(Qt.Key_O, Qt.ControlModifier);
        tryVerify(() => window.openSessionDialog.opened, 1000, "Session's Ctrl+O did nothing");
        window.openSessionDialog.close();
    }

    function test_theTitleBarKeepsItsHeightWhenTheDesktopsLookIsPicked() {
        const window = testCase.openWindow();
        if (!window.ownTitleBar) {
            skip("KWin draws the title bar while following the system");
        }
        // **Picking Follow system keeps the frameless window till the next
        // start** (the control style is chosen once), so its title bar must
        // stay one: the only thing left to move it, maximise it or close it.
        Appearance.mode = Appearance.FollowSystem;
        verify(window.toolBar.titleBar);
        tryVerify(() => window.toolBar.height === AppMetrics.headerHeight, 1000,
                  "the title bar is " + window.toolBar.height + " pixels high under the desktop's look");
        verify(window.toolBar.closeButton.visible);
    }

    function test_theVirtualDevicesButtonIsThereInEveryAppearance() {
        const window = testCase.openWindow();
        const bar = window.toolBar;
        const button = bar.virtualDevicesButton;
        // **In every appearance**: under KWin's title bar too, with
        // the menu bar up, where the theme and menu buttons are not.
        verify(button.visible, "no Virtual Devices button");
        verify(button.enabled, "a Virtual Devices button that cannot be pressed");
        compare(button.width, AppMetrics.windowButtonSize);
        compare(button.text, "Virtual Devices");
        compare(button.Accessible.name, "Virtual Devices");
        verify(button.focusPolicy & Qt.TabFocus, "the Virtual Devices button cannot be reached with Tab");
        // After the menu button's place, and last before the window's own.
        verify(testCase.xOf(window, button) > testCase.xOf(window, bar.runButton), "before Run");
        if (window.ownTitleBar) {
            compare(String(button.glyph), String(AppGlyphs.virtualDevices));
            verify(testCase.xOf(window, button) > testCase.xOf(window, bar.menuButton), "before the menu");
            verify(testCase.xOf(window, button) < testCase.xOf(window, bar.minimizeButton), "after minimise");
        } else {
            compare(button.glyph, "audio-card");
            verify(window.menus.visible, "the menu bar should be up under KWin's title bar");
        }
    }

    function test_theThemeButtonSwitchesBetweenDarkAndLight() {
        const window = testCase.openWindow();
        if (!window.ownTitleBar) {
            skip("the theme button is the design title bar's");
        }
        const button = window.toolBar.themeButton;
        Appearance.mode = Appearance.ReverblabrDark;
        compare(String(button.glyph), String(AppGlyphs.sun));
        mouseClick(button);
        compare(Appearance.mode, Appearance.ReverblabrLight);
        compare(String(button.glyph), String(AppGlyphs.moon));
        mouseClick(button);
        compare(Appearance.mode, Appearance.ReverblabrDark);
    }

    function test_maximisedTheWindowIsSquareWithNoBorderAndNoShadow() {
        const window = testCase.openWindow();
        if (!window.ownTitleBar) {
            skip("KWin frames the window while following the system");
        }
        compare(window.frameRadius, AppMetrics.windowRadius);
        compare(window.frameBorder.border.width, 1);
        verify(window.windowFrame.shadowed, "no shadow asked for round a window with a frame of its own");
        verify(window.resizeGrips.enabled);

        mouseClick(window.toolBar.maximizeButton);
        tryVerify(() => window.edgeToEdge, 1000, "the maximise button did not maximise");
        compare(window.frameRadius, 0);
        compare(window.frameBorder.border.width, 0);
        verify(!window.windowFrame.shadowed, "a shadow round a maximised window");
        verify(!window.resizeGrips.enabled, "a maximised window's edges still resize it");
        compare(window.toolBar.maximizeButton.text, "Restore");
        compare(String(window.toolBar.maximizeButton.glyph), String(AppGlyphs.restore));

        mouseClick(window.toolBar.maximizeButton);
        tryVerify(() => !window.edgeToEdge, 1000, "the restore button did not restore");
        compare(window.frameRadius, AppMetrics.windowRadius);
    }

    function test_aDoubleClickOnTheEmptyTitleBarMaximises() {
        const window = testCase.openWindow();
        if (!window.ownTitleBar) {
            skip("KWin's title bar, not ours, while following the system");
        }
        const bar = window.toolBar;
        // Between the tabs and the status: nothing there takes the press.
        const x = (testCase.xOf(window, window.toolBar.status) + window.sessionTabs.width) / 2;
        const y = bar.height / 2;
        mouseDoubleClickSequence(bar, x, y);
        tryVerify(() => window.edgeToEdge, 1000, "a double click did not maximise");
        mouseDoubleClickSequence(bar, x, y);
        tryVerify(() => !window.edgeToEdge, 1000, "a second double click did not restore");

        // **A double click on a control is the control's**: on a session's
        // tab it picks the session, it does not maximise the window. (The
        // first tab, which is always in view; with many sessions open the
        // new-session button scrolls out of it.)
        window.sessionTabs.forceLayout();
        tryVerify(() => window.sessionTabs.itemAtIndex(0) !== null, 1000, "the first tab was not built");
        const tab = window.sessionTabs.itemAtIndex(0);
        mouseDoubleClickSequence(tab);
        compare(Sessions.currentId, tab.sessionId, "the tab did not take its own double click");
        verify(!window.edgeToEdge, "a double click on a tab maximised the window");
    }

    function test_aDragOnTheEmptyTitleBarMovesTheWindowButNotOnATab() {
        const window = testCase.openWindow();
        if (!window.ownTitleBar) {
            skip("KWin's title bar, not ours, while following the system");
        }
        const bar = window.toolBar;
        const space = bar.emptySpace;
        const x = (testCase.xOf(window, bar.status) + window.sessionTabs.width) / 2;
        const y = bar.height / 2;
        const far = Application.styleHints.startDragDistance + 4;
        // **Past the drag distance, the compositor is handed the pointer**
        // (offscreen there is none to take it, so what is held is the ask).
        mousePress(bar, x, y);
        mouseMove(bar, x + far / 2, y);
        verify(!space.moving, "a twitch moved the window");
        mouseMove(bar, x + far, y);
        verify(space.moving, "a drag on the empty title bar did not move the window");
        mouseRelease(bar, x + far, y);

        // **A tab is the tab's**: dragging one is not dragging the window.
        window.sessionTabs.forceLayout();
        tryVerify(() => window.sessionTabs.itemAtIndex(0) !== null, 1000, "the session's tab was not built");
        const tab = window.sessionTabs.itemAtIndex(0);
        space.moving = false;
        mousePress(tab, tab.width / 2, tab.height / 2);
        mouseMove(tab, tab.width / 2 + far, tab.height / 2);
        verify(!space.moving, "dragging a tab moved the window");
        mouseRelease(tab, tab.width / 2 + far, tab.height / 2);
    }

    function test_aRightClickOnTheEmptyTitleBarOpensTheWindowMenu() {
        const window = testCase.openWindow();
        if (!window.ownTitleBar) {
            skip("KWin's title bar, not ours, while following the system");
        }
        // **Offscreen there is no compositor to ask**, so it is ours: what a
        // window can do to itself.
        const x = (testCase.xOf(window, window.toolBar.status) + window.sessionTabs.width) / 2;
        mouseClick(window.toolBar, x, window.toolBar.height / 2, Qt.RightButton);
        tryVerify(() => window.windowMenu.opened, 1000, "no window menu");
        const words = [];
        for (let i = 0; i < window.windowMenu.count; ++i) {
            const item = window.windowMenu.itemAt(i);
            if (!(item instanceof QQC2.MenuSeparator)) {
                words.push(item.text);
            }
        }
        compare(words, ["Minimize", "Maximize", "Close"]);
        window.windowMenu.close();
    }

    function test_everyEdgeAndCornerResizes() {
        const window = testCase.openWindow();
        if (!window.ownTitleBar) {
            skip("KWin's frame resizes the window while following the system");
        }
        const grips = window.resizeGrips;
        compare(grips.grips.count, 8);
        // Which edges each grip moves, and the cursor that says so.
        const want = {};
        want[Qt.TopEdge | Qt.LeftEdge] = Qt.SizeFDiagCursor;
        want[Qt.BottomEdge | Qt.RightEdge] = Qt.SizeFDiagCursor;
        want[Qt.TopEdge | Qt.RightEdge] = Qt.SizeBDiagCursor;
        want[Qt.BottomEdge | Qt.LeftEdge] = Qt.SizeBDiagCursor;
        want[Qt.TopEdge] = Qt.SizeVerCursor;
        want[Qt.BottomEdge] = Qt.SizeVerCursor;
        want[Qt.LeftEdge] = Qt.SizeHorCursor;
        want[Qt.RightEdge] = Qt.SizeHorCursor;
        for (let i = 0; i < grips.grips.count; ++i) {
            const grip = grips.grips.itemAt(i);
            compare(grip.cursorShape, want[grip.edges], "edges " + grip.edges);
            // On the edge it moves, not somewhere inside the window.
            const at = grip.mapToItem(grips, 0, 0);
            if (grip.edges & Qt.LeftEdge) compare(at.x, 0);
            if (grip.edges & Qt.TopEdge) compare(at.y, 0);
            if (grip.edges & Qt.RightEdge) compare(at.x + grip.width, grips.width);
            if (grip.edges & Qt.BottomEdge) compare(at.y + grip.height, grips.height);
        }
        // Over the whole window, header and status bar included.
        compare(grips.width, window.width);
        compare(grips.height, window.height);
    }
}
