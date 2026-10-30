// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// **Every control says what it is for**. The window with every block
// on the canvas, then every block's settings panel, the Files and Info
// tabs, the Virtual Devices and Sessions windows and the system tray's
// popover (with a session and a meter marked for the tray in it): each is walked
// through the accessibility tree — what a screen reader is told, not the
// item tree — and every control a person can act on that has no name is a
// failure, listed with where it is.
//
// An unnamed slider is read out as "slider, 40 %": which one, and 40 % of
// what, is exactly what somebody who cannot see the panel has no other way
// to learn.

import QtQuick
import QtQuick.Controls as QQC2
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "Accessibility"
    when: windowShown

    Component {
        id: mainWindow

        Main {
            width: 1400
            height: 900
        }
    }

    Component {
        id: unnamedSlider

        QQC2.Slider {
            objectName: "injectedSlider"
        }
    }

    /// What a role number is called, for a failure message.
    readonly property var roleNames: ({
            [Accessible.Button]: "button",
            [Accessible.CheckBox]: "check box",
            [Accessible.RadioButton]: "radio button",
            [Accessible.ComboBox]: "combo box",
            [Accessible.SpinBox]: "spin box",
            [Accessible.Slider]: "slider",
            [Accessible.Dial]: "dial",
            [Accessible.EditableText]: "text field",
            [Accessible.PageTab]: "tab",
            [Accessible.Link]: "link",
            [Accessible.ButtonMenu]: "menu button",
            [Accessible.ButtonDropDown]: "drop-down button",
            [Accessible.MenuItem]: "menu item"
        })

    function unfold(item) {
        if (item === null || item === undefined) {
            return;
        }
        if (item.toggle !== undefined && item.expanded !== undefined) {
            item.expanded = true;
        }
        for (const child of item.children ?? []) {
            testCase.unfold(child);
        }
    }

    /// The unnamed controls under \a root, as lines for a failure message.
    /// **A walk that found nothing is a failure too**: a screen not yet laid
    /// out, or not shown, has no controls to be unnamed.
    function unnamedUnder(screen, root) {
        const looked = TestAccessibility.census(root);
        verify(looked > 0, screen + ": the walk found nothing a screen reader would be told about");
        return TestAccessibility.unnamed(root).map(
            found => screen + ": " + (testCase.roleNames[found.role] ?? ("role " + found.role)) + " at " + found.path);
    }

    function rootOf(window) {
        return window.contentItem.parent ?? window.contentItem;
    }

    function openWindow() {
        const window = createTemporaryObject(mainWindow, testCase);
        tryVerify(() => AppShell.hasSession && BlockCatalogue.ready, 3000, "no session to put blocks in");
        waitForRendering(window.contentItem);
        return window;
    }

    /// One of every block the catalogue has, in rows.
    function everyBlock() {
        const made = [];
        let index = 0;
        for (const typeId of BlockCatalogue.types) {
            const id = SessionModel.addBlock(typeId, Qt.point(40 + (index % 6) * 260, 40 + Math.floor(index / 6) * 200));
            if (id !== "") {
                made.push({id: id, type: typeId});
            }
            ++index;
        }
        return made;
    }

    // **As a screen reader has it**: accessibility on, for this file only.
    function initTestCase() {
        TestAccessibility.setActive(true);
    }

    function cleanupTestCase() {
        TestAccessibility.setActive(false);
    }

    function cleanup() {
        SessionModel.removeBlocks(SessionModel.blockIds);
    }

    // **The audit finds what it is there to find**: a slider with no name,
    // put in a panel, is reported, and reported as a slider.
    function test_theAuditFindsAnUnnamedControl() {
        const window = testCase.openWindow();
        const canvas = window.sessionCanvas;
        const block = canvas.addBlockAt("effect.volume", 200, 200);
        canvas.openSettings(block);
        tryVerify(() => window.settingsPanel.visible, 1000);
        const slider = createTemporaryObject(unnamedSlider, window.settingsPanel);
        waitForRendering(window.settingsPanel);
        const found = TestAccessibility.unnamed(window.settingsPanel);
        verify(found.some(item => item.path.indexOf("injectedSlider") >= 0),
               "the audit missed a slider with no name: " + JSON.stringify(found));
    }

    function test_everyControlOnTheWindowHasAName() {
        const window = testCase.openWindow();
        testCase.everyBlock();
        waitForRendering(window.contentItem);
        let problems = [];
        window.sidebarIndex = window.sidebar.blocksTab;
        testCase.unfold(window.blockLibrary);
        waitForRendering(window.contentItem);
        problems = problems.concat(testCase.unnamedUnder("window", testCase.rootOf(window)));
        window.sidebarIndex = window.sidebar.filesTab;
        waitForRendering(window.contentItem);
        problems = problems.concat(testCase.unnamedUnder("files", window.filesPane));
        window.sidebarIndex = window.sidebar.infoTab;
        testCase.unfold(window.infoPane);
        waitForRendering(window.contentItem);
        problems = problems.concat(testCase.unnamedUnder("info", window.infoPane));
        // **The session as a list**, with every block in it.
        WindowState.listShown = true;
        tryVerify(() => window.sessionCanvas.sessionList.visible, 1000, "the list did not show");
        waitForRendering(window.contentItem);
        problems = problems.concat(testCase.unnamedUnder("list", window.sessionCanvas.sessionList));
        const rowsWithoutName = window.sessionCanvas.sessionList.allRows().filter(
            row => row.Accessible.name === "").map(row => row.blockId);
        WindowState.listShown = false;
        compare(rowsWithoutName.length, 0, "rows with no name: " + rowsWithoutName.join(", "));
        compare(problems.length, 0, "controls with no name:\n" + problems.join("\n"));
    }

    function test_everyControlInEveryPanelHasAName() {
        const window = testCase.openWindow();
        const canvas = window.sessionCanvas;
        const made = testCase.everyBlock();
        verify(made.length > 30, "only " + made.length + " blocks were made");
        let problems = [];
        for (const block of made) {
            canvas.closeSettings();
            canvas.openSettings(block.id);
            tryVerify(() => window.settingsPanel.blockId === block.id && window.settingsPanel.visible, 1000,
                      block.type + "'s panel did not open");
            testCase.unfold(window.settingsPanel);
            waitForRendering(window.settingsPanel);
            problems = problems.concat(testCase.unnamedUnder(block.type, window.settingsPanel));
        }
        canvas.closeSettings();
        compare(problems.length, 0, "controls with no name:\n" + problems.join("\n"));
    }

    function test_everyControlInTheOtherWindowsHasAName() {
        const window = testCase.openWindow();
        let problems = [];
        window.showVirtualDevices();
        tryVerify(() => window.virtualDevicesWindow !== null && window.virtualDevicesWindow.visible, 2000);
        waitForRendering(window.virtualDevicesWindow.contentItem);
        problems = problems.concat(testCase.unnamedUnder("virtual devices", testCase.rootOf(window.virtualDevicesWindow)));
        window.virtualDevicesWindow.close();
        window.showSessionsWindow();
        tryVerify(() => window.sessionsWindow !== null && window.sessionsWindow.visible, 2000);
        waitForRendering(window.sessionsWindow.contentItem);
        problems = problems.concat(testCase.unnamedUnder("sessions", testCase.rootOf(window.sessionsWindow)));
        window.sessionsWindow.close();

        // **The tray's popover, with a row in each list.**
        const sessionsBefore = Sessions.open.length;
        TestEngine.createSession("Audited from the tray");
        tryVerify(() => Sessions.open.length === sessionsBefore + 1, 1000);
        TestEngine.applyEdit(Sessions.open[Sessions.open.length - 1].id, JSON.stringify({
            op: "addBlock",
            block: {id: "auditedMeter", type: "meter.peakrms", params: {showInTray: true}},
            index: -1
        }));
        TrayState.requestPopover();
        tryVerify(() => window.trayPopover !== null && window.trayPopover.visible, 1000, "no popover opened");
        const popover = window.trayPopover;
        tryVerify(() => popover.overview.ready && popover.sessionList.count > 0 && popover.meterList.count > 0, 1000,
                  "the popover has no session or no meter to audit");
        waitForRendering(popover.contentItem);
        problems = problems.concat(testCase.unnamedUnder("tray popover", testCase.rootOf(popover)));
        popover.hide();
        compare(problems.length, 0, "controls with no name:\n" + problems.join("\n"));
    }
}
