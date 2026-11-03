// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Window
import QtTest
import io.github.reverblabr

// **A settings panel outside the main window** (ADR-0032). Offscreen a window
// can be placed and read back, so this runs the plain window. What only a
// compositor can answer, the layer surface and the probe's popup, is in the
// ADR with what was seen on the desktop.
TestCase {
    id: testCase

    name: "PanelWindow"
    when: windowShown

    property string sessionId: ""

    Component {
        id: shownCanvas

        Window {
            property alias canvas: inner

            width: 800
            height: 600
            visible: true

            SessionCanvas {
                id: inner

                anchors.fill: parent
            }
        }
    }

    Component {
        id: mainWindow

        Main {}
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Outside");
        verify(testCase.sessionId !== "", "the mock opened a session");
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.sessionId = testCase.sessionId;
    }

    function cleanup() {
        SessionModel.removeBlocks(SessionModel.blockIds);
        WindowState.setDetachedPanels(testCase.sessionId, []);
    }

    /// A shown canvas with a panel detached for a new block.
    function detachedOn(window, type) {
        const canvas = window.canvas ?? window.sessionCanvas;
        const block = canvas.addBlockAt(type ?? "effect.volume", 200, 200);
        canvas.openSettings(block);
        canvas.settings.detachButton.clicked();
        return { canvas: canvas, block: block, panel: canvas.detachedPanelFor(block) };
    }

    function shown() {
        const window = createTemporaryObject(shownCanvas, null);
        verify(window !== null);
        tryVerify(() => window.canvas.mainWindowShowing, 2000, "the canvas's window is not showing");
        return window;
    }

    function test_thePanelLeavesAsTheSameItemWithWhatWasTyped() {
        const window = testCase.shown();
        const { canvas, block, panel } = testCase.detachedOn(window);
        panel.startRenaming();
        panel.nameField.text = "half typed";
        panel.bodyView.contentItem.contentY = 3;

        panel.outsideButton.clicked();

        tryVerify(() => panel.surface !== null, 2000, "the panel got no window");
        compare(canvas.detachedPanelFor(block), panel, "the panel was made again");
        verify(panel.Window.window !== window, "the panel is still in the main window");
        compare(panel.Window.window, panel.surface);
        compare(panel.nameField.text, "half typed", "what was typed went with the move");
        compare(panel.outside, true);
        verify(panel.surface.visible, "the panel's window is not up");

        panel.outsideButton.clicked();

        tryVerify(() => panel.surface === null, 2000, "the panel's window stayed");
        compare(panel.Window.window, window, "the panel did not come back");
        compare(panel.outside, false);
        compare(panel.nameField.text, "half typed", "what was typed went with the way back");
        compare(canvas.detachedPanelFor(block), panel);
    }

    function test_aSizeFromTheDesktopDoesNotCutTheCard() {
        const window = testCase.shown();
        const { panel } = testCase.detachedOn(window);
        panel.outsideButton.clicked();
        tryVerify(() => panel.surface !== null, 2000, "the panel got no window");
        const surface = panel.surface;
        const fits = () => surface.width === Math.trunc(panel.width) + surface.inset * 2
            && surface.height === Math.trunc(panel.height) + surface.inset * 2;
        tryVerify(fits, 2000, "the window was never the card's size");

        // What a layer surface's first answer does to a card that has grown
        // since it asked: nothing in the card has changed for it to go by.
        surface.setGeometry(surface.x, surface.y, surface.width - 60, surface.height - 40);

        tryVerify(fits, 2000, "the card stays cut");
    }

    function test_itKeepsThePlaceItHadOnTheScreen() {
        const window = testCase.shown();
        window.x = 300;
        window.y = 200;
        tryCompare(window, "x", 300);
        const { canvas, panel } = testCase.detachedOn(window);
        const inWindow = Qt.point(panel.x, panel.y);

        panel.outsideButton.clicked();
        tryVerify(() => panel.surface !== null);

        compare(panel.outsideLeft, 300 + inWindow.x, "the card jumped sideways on leaving");
        compare(panel.outsideTop, 200 + inWindow.y, "the card jumped on leaving");
        // The card, not its window: the window has the shadow around it.
        compare(panel.surface.x + panel.surface.inset, 300 + inWindow.x);
        compare(panel.surface.y + panel.surface.inset, 200 + inWindow.y);
        compare(panel.x, panel.surface.inset);
    }

    function test_theWayOutIsOfferedOnlyToADetachedPanel() {
        const window = testCase.shown();
        const canvas = window.canvas;
        const block = canvas.addBlockAt("effect.volume", 200, 200);
        canvas.openSettings(block);
        verify(!canvas.settings.outsideButton.visible, "a docked panel offers to leave the window");
        canvas.settings.detachButton.clicked();
        verify(canvas.detachedPanelFor(block).outsideButton.visible, "a detached panel has no way out");
    }

    function test_whichPanelsAreOutsideIsRemembered() {
        const home = testCase.sessionId;
        const window = testCase.shown();
        const { canvas, block, panel } = testCase.detachedOn(window);
        panel.outsideButton.clicked();
        tryVerify(() => panel.surface !== null);
        canvas.moveOutsidePanel(block, 40, 30);
        canvas.saveDetached();
        const left = panel.outsideLeft;
        const top = panel.outsideTop;

        const saved = WindowState.detachedPanels(home);
        compare(saved.length, 1);
        compare(saved[0].outside, true);
        compare(saved[0].left, left);
        compare(saved[0].top, top);

        // Another session in front puts it away, and back again brings it.
        TestEngine.createSession("Elsewhere");
        const elsewhere = testCase.sessionId;
        testCase.sessionId = home;
        SessionModel.sessionId = elsewhere;
        compare(canvas.detachedPanelFor(block), null, "another session showed this one's panel");
        SessionModel.sessionId = home;
        tryVerify(() => canvas.detachedPanelFor(block)?.surface ?? null !== null, 2000,
                  "the panel did not go back outside with its session");
        compare(canvas.detachedPanelFor(block).outsideLeft, left);

        // And a canvas made with the session showing, as at the next start.
        window.destroy();
        const again = testCase.shown();
        tryVerify(() => again.canvas.detachedPanelFor(block)?.surface ?? null !== null, 2000,
                  "the panel did not go back outside at the next start");
        compare(again.canvas.detachedPanelFor(block).outsideTop, top);
    }

    function test_aPlaceOnAScreenThatIsGoneIsBroughtBack() {
        const block = SessionModel.addBlock("effect.volume", Qt.point(100, 100));
        WindowState.setDetachedPanels(testCase.sessionId, [
            { blockId: block, x: 20, y: 20, outside: true, screen: "no such screen", left: 90000, top: 90000 }
        ]);
        const window = testCase.shown();
        const panel = window.canvas.detachedPanelFor(block);
        tryVerify(() => panel.surface !== null, 2000, "the panel did not go outside");
        const screen = window.screen;
        verify(panel.outsideLeft < screen.virtualX + screen.width, "the panel came back off the desktop");
        verify(panel.outsideTop < screen.virtualY + screen.height, "the panel came back off the desktop");
    }

    function outsidePanel(window) {
        const parts = testCase.detachedOn(window);
        parts.panel.outsideButton.clicked();
        tryVerify(() => parts.panel.surface !== null, 2000, "the panel got no window");
        parts.surface = parts.panel.surface;
        return parts;
    }

    function test_itsWindowGoesWithItsCloseButton() {
        const { canvas, block, panel, surface } = testCase.outsidePanel(testCase.shown());
        panel.closeButton.clicked();
        compare(canvas.detachedCount, 0);
        tryVerify(() => surface === null || !surface.visible, 2000, "closing the panel left its window up");
    }

    function test_itsWindowGoesWithAttaching() {
        const { canvas, block, panel, surface } = testCase.outsidePanel(testCase.shown());
        panel.detachButton.clicked();
        compare(canvas.settings.blockId, block, "attaching did not put the panel beside its block");
        tryVerify(() => surface === null || !surface.visible, 2000, "attaching left its window up");
    }

    function test_itsWindowGoesWithItsBlock() {
        const { canvas, block, surface } = testCase.outsidePanel(testCase.shown());
        SessionModel.removeBlocks([block]);
        compare(canvas.detachedCount, 0);
        tryVerify(() => surface === null || !surface.visible, 2000, "deleting the block left its window up");
        verify(SessionModel.canUndo);
        SessionModel.undo();
    }

    function test_itsWindowGoesWithTheSession() {
        const home = testCase.sessionId;
        const { canvas, surface } = testCase.outsidePanel(testCase.shown());
        TestEngine.createSession("Other");
        const other = testCase.sessionId;
        testCase.sessionId = home;
        SessionModel.sessionId = other;
        tryVerify(() => surface === null || !surface.visible, 2000, "switching session left its window up");
    }

    function test_itGoesAndComesBackWithTheMainWindow() {
        const window = testCase.shown();
        const { panel, surface } = testCase.outsidePanel(window);
        window.close();
        tryVerify(() => !surface.visible, 2000, "the main window's going left the panel up");
        verify(panel.surface === surface, "hiding the panel lost it");
        window.show();
        tryVerify(() => surface.visible, 2000, "the panel did not come back with the main window");
    }

    /// Gives \a surface the keyboard. **Not `active`**, which a window is
    /// also while its transient parent has the keyboard: that would send the
    /// key to the main window, where everything tested here works anyway.
    function giveKeyboardTo(surface) {
        surface.requestActivate();
        tryVerify(() => surface.activeFocusItem !== null, 2000, "the panel's window did not get the keyboard");
    }

    /// Presses a panel's header at a free spot, in the window it is in.
    function headerPoint(panel) {
        return panel.headerGrab.mapToItem(panel.Window.window.contentItem, panel.headerGrab.width / 3,
                                          panel.headerGrab.height / 2);
    }

    function test_draggingPastTheEdgeTakesItOut() {
        // The screen here is small, and past its edge a card is kept back: to
        // the left of a window placed clear of that edge there is room.
        const window = testCase.shown();
        window.x = 300;
        window.y = 100;
        tryCompare(window, "x", 300);
        const { canvas, panel } = testCase.detachedOn(window);
        const start = testCase.headerPoint(panel);
        const grab = panel.headerGrab.mapToItem(panel, panel.headerGrab.width / 3, panel.headerGrab.height / 2);

        mousePress(window.contentItem, start.x, start.y);
        mouseMove(window.contentItem, start.x + 8, start.y);
        verify(!panel.outside, "a small drag took the panel out");
        mouseMove(window.contentItem, -10, 300);
        tryVerify(() => panel.surface !== null, 2000, "going past the edge did not take the panel out");

        // **On the pointer**, as far off as it likes: the main window goes on
        // being told where the pointer is.
        mouseMove(window.contentItem, -40, 320);
        tryVerify(() => Math.abs(panel.outsideLeft - (300 - 40 - grab.x)) <= 1, 2000,
                  "the card is not under the pointer: " + panel.outsideLeft);
        verify(Math.abs(panel.outsideTop - (100 + 320 - grab.y)) <= 1, "the card is not under the pointer");
        mouseRelease(window.contentItem, -40, 320);

        compare(canvas.carriedBlock, "");
        compare(canvas.mainWatch.followsPointer, false, "the main window went on following the pointer");
        const saved = WindowState.detachedPanels(testCase.sessionId);
        compare(saved[0].outside, true, "a panel carried out was not written down");
    }

    function test_aPanelInsideStaysInsideThoughDragged() {
        const window = testCase.shown();
        const { canvas, panel } = testCase.detachedOn(window);
        const start = testCase.headerPoint(panel);
        mousePress(window.contentItem, start.x, start.y);
        mouseMove(window.contentItem, start.x + 30, start.y + 30);
        mouseRelease(window.contentItem, start.x + 30, start.y + 30);
        compare(panel.outside, false);
        compare(panel.surface, null);
    }

    function test_noWordFromTheDesktopKeepsItInside() {
        const window = testCase.shown();
        const { canvas, panel } = testCase.detachedOn(window);
        const start = testCase.headerPoint(panel);
        mousePress(window.contentItem, start.x, start.y);
        canvas.originProbe.known = false;
        mouseMove(window.contentItem, 900, 300);
        wait(50);
        mouseRelease(window.contentItem, 900, 300);
        compare(panel.outside, false, "a panel left for a place nobody can work out");
    }

    function test_droppedOverTheCanvasItIsBackInside() {
        const window = testCase.shown();
        const { canvas, block, panel } = testCase.outsidePanel(window);
        canvas.originProbe.measure();
        canvas.moveOutsidePanel(block, 10, 10);
        canvas.settleOutside(block);
        tryVerify(() => panel.surface === null, 2000, "a panel dropped on the canvas stayed outside");
        compare(panel.Window.window, window);
    }

    function test_droppedElsewhereItStaysOutside() {
        const window = testCase.shown();
        window.width = 500;
        tryCompare(window.canvas, "width", 500);
        const { canvas, block, panel } = testCase.outsidePanel(window);
        canvas.originProbe.measure();
        canvas.moveOutsidePanel(block, window.width, 0);
        canvas.settleOutside(block);
        wait(50);
        verify(panel.surface !== null, "a panel dropped off the canvas came back inside");
    }

    function test_theKeysThePanelDoesNotUseReachTheCanvas() {
        const window = testCase.shown();
        const { canvas, block, panel, surface } = testCase.outsidePanel(window);
        testCase.giveKeyboardTo(surface);
        canvas.selectOnly(block);

        // On the canvas, Del takes the selected block out from anywhere in
        // the window, and so here.
        keyClick(Qt.Key_Delete);
        tryCompare(canvas, "detachedCount", 0);
        compare(SessionModel.blockIds.indexOf(block), -1, "Del did not reach the canvas");
    }

    function test_escapeIsLeftToTheDockedPanelAsItIsOnTheCanvas() {
        const window = testCase.shown();
        const { canvas, block, panel, surface } = testCase.outsidePanel(window);
        const other = canvas.addBlockAt("effect.volume", 420, 200);
        canvas.openSettings(other);
        testCase.giveKeyboardTo(surface);

        keyClick(Qt.Key_Escape);

        compare(canvas.settings.blockId, "", "Esc did not close the docked panel");
        compare(canvas.detachedPanelFor(block), panel, "Esc closed the detached panel");
    }

    function test_theMainWindowsUndoWorksFromOutside() {
        const main = createTemporaryObject(mainWindow, null);
        tryVerify(() => main.sessionCanvas.mainWindowShowing, 2000);
        SessionModel.sessionId = testCase.sessionId;
        const block = SessionModel.addBlock("effect.volume", Qt.point(200, 200));
        const canvas = main.sessionCanvas;
        canvas.openSettings(block);
        canvas.settings.detachButton.clicked();
        const panel = canvas.detachedPanelFor(block);
        panel.outsideButton.clicked();
        tryVerify(() => panel.surface !== null, 2000);
        SessionModel.renameBlock(block, "Renamed");
        compare(SessionModel.block(block).nickname, "Renamed");

        // **As a layer surface is, with no transient parent.** Qt counts a
        // transient window as its parent for a shortcut, so with one the main
        // window's own action would answer here and prove nothing.
        panel.surface.transientParent = null;
        verify(main.shortcutActions.length > 0, "the main window has no actions to offer");
        compare(panel.surface.shortcuts.count, main.shortcutActions.length, "the panel's window has no shortcuts");
        testCase.giveKeyboardTo(panel.surface);
        keyClick(Qt.Key_Z, Qt.ControlModifier);

        tryCompare(SessionModel.block(block), "nickname", "", 2000, "undo did not reach the session");
    }

    function test_aNameTypedOutsideSurvivesTheWayBack() {
        const main = createTemporaryObject(mainWindow, null);
        tryVerify(() => main.sessionCanvas.mainWindowShowing, 2000);
        SessionModel.sessionId = testCase.sessionId;
        const block = SessionModel.addBlock("effect.volume", Qt.point(200, 200));
        const canvas = main.sessionCanvas;
        canvas.openSettings(block);
        canvas.settings.detachButton.clicked();
        const panel = canvas.detachedPanelFor(block);
        panel.outsideButton.clicked();
        tryVerify(() => panel.surface !== null, 2000);
        panel.surface.transientParent = null;
        testCase.giveKeyboardTo(panel.surface);

        panel.startRenaming();
        tryVerify(() => panel.nameField.activeFocus, 2000, "the name field did not get the keyboard");
        keyClick(Qt.Key_A);
        keyClick(Qt.Key_B);
        keyClick(Qt.Key_Return);
        compare(SessionModel.block(block).nickname, "ab", "typing in the panel's window did not rename the block");
        compare(panel.nameField.visible, false);
        compare(panel.nameField.activeFocus, false, "the hidden field kept the keyboard");

        // With the keyboard out of the field, Ctrl+Z is the window's undo,
        // and the block's name goes back.
        keyClick(Qt.Key_Z, Qt.ControlModifier);
        tryVerify(() => (SessionModel.block(block).nickname ?? "") === "", 2000,
                  "Ctrl+Z did not undo the rename: " + SessionModel.block(block).nickname);

        // Going home must not write the field's text over the block's name.
        keyClick(Qt.Key_Z, Qt.ControlModifier | Qt.ShiftModifier);
        tryVerify(() => SessionModel.block(block).nickname === "ab", 2000, "redo did not put the name back");
        panel.outsideButton.clicked();
        tryVerify(() => panel.surface === null, 2000);
        compare(SessionModel.block(block).nickname, "ab", "moving the panel changed the block's name");
    }

    function test_aPanelsPopupsAreWindowsOutsideAndItsOwnAgainInside() {
        const window = testCase.shown();
        const { canvas, panel } = testCase.detachedOn(window, "source.device");
        const popups = () => {
            const found = [];
            (function walk(item) {
                for (const child of item.children) {
                    if (child.popup !== undefined && child.popup.popupType !== undefined) {
                        found.push(child.popup);
                    }
                    walk(child);
                }
            })(panel);
            return found;
        };
        verify(popups().length > 0, "the panel has no combo box to look at");
        for (const popup of popups()) {
            compare(popup.popupType, QQC2.Popup.Item);
        }
        panel.outsideButton.clicked();
        tryVerify(() => panel.surface !== null);
        for (const popup of popups()) {
            compare(popup.popupType, QQC2.Popup.Window, "a popup would be clipped by the card's window");
        }
        panel.outsideButton.clicked();
        tryVerify(() => panel.surface === null);
        for (const popup of popups()) {
            compare(popup.popupType, QQC2.Popup.Item, "a popup stayed a window inside the main window");
        }
    }

    function test_aPanelsDialogsOpenOverTheMainWindow() {
        const window = testCase.shown();
        const { canvas, panel } = testCase.outsidePanel(window);
        compare(panel.presets.saveDialog.parent, canvas.mainOverlay,
                "a dialog would be clipped by the card's window");
    }

    function test_theProbeReadsAWindowThatKnowsItsPlace() {
        const window = testCase.shown();
        window.x = 321;
        window.y = 123;
        tryCompare(window, "x", 321);
        const probe = window.canvas.originProbe;
        probe.measure();
        compare(probe.known, true);
        compare(probe.origin, Qt.point(321, 123));
    }

    // What the popup on Wayland answers, which offscreen has no popup to give.
    function test_theProbeMeasuresFromTheWindowOnAnyScreen() {
        const window = testCase.shown();
        window.x = 300;
        window.y = 40;
        tryCompare(window, "x", 300);
        const probe = window.canvas.originProbe;
        probe.known = false;
        // Qt adds the window's own place to what the desktop said: here the
        // window is 200 along its screen and 300 is where the screen starts.
        probe.answer(300 - 200, 40 - 25);
        compare(probe.origin, Qt.point(200, 25), "the second screen's offset came into the origin");
        compare(probe.known, true);
    }

    function test_theProbeWaitsForAnAnswer() {
        const window = testCase.shown();
        const probe = window.canvas.originProbe;
        answers.target = probe;
        answers.clear();
        // Being put far away changes `x` and says nothing, and ending the wait
        // there would take the popup away before the desktop could answer.
        probe.answer(probe.farAway, probe.farAway);
        compare(answers.count, 0, "the probe took its own request for an answer");
        probe.answer(10, 20);
        compare(answers.count, 1, "the probe ignored an answer");
    }

    SignalSpy {
        id: answers

        signalName: "measured"
    }

    function test_aDragStaysInsideWhereAWindowCannotPlaceItself() {
        const window = testCase.shown();
        const { canvas, panel } = testCase.detachedOn(window);
        canvas.panelsPlaceThemselves = false;
        const start = testCase.headerPoint(panel);
        mousePress(window.contentItem, start.x, start.y);
        mouseMove(window.contentItem, -10, 300);
        wait(50);
        mouseRelease(window.contentItem, -10, 300);
        compare(panel.outside, false, "a plain Wayland window was sent to follow a pointer it can't");
        panel.outsideButton.clicked();
        tryVerify(() => panel.surface !== null, 2000, "the button did not take the panel out");
    }

    Component {
        id: bareSurface

        PanelWindow {}
    }

    // A layer surface picks its screen as it first appears, for good.
    function test_itIsNotShownBeforeItIsToldItsScreen() {
        const window = testCase.shown();
        const surface = createTemporaryObject(bareSurface, null, { canvas: window.canvas });
        verify(window.canvas.mainWindowShowing);
        compare(surface.visible, false, "the window came up before it was ready");
        surface.ready = true;
        tryCompare(surface, "visible", true);
    }

    function test_theMarginsOfAPanelAreKeptOnTheDesktop() {
        const window = testCase.shown();
        const canvas = window.canvas;
        const kept = canvas.keepOnDesktop(-5000, 90000, 100, 100, "");
        compare(kept.x, 0 - window.screen.virtualX, "kept past the desktop's left");
        verify(kept.y <= window.screen.height, "kept past the desktop's bottom");
    }
}
