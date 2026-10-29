// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The app shell offscreen: that the window is the shape the rest of
// the interface will be built into, and that what it says about the engine
// is what the engine said. What the colours look like on a real desktop is
// something a person has to look at.

import QtQuick
import QtTest
import org.kde.kirigami as Kirigami
import io.github.reverblabr

TestCase {
    id: testCase

    name: "MainWindow"

    /// How long a change the window notices may take to show. The session
    /// the window opens with arrives through the facade's signals.
    readonly property int noticeMs: 250

    Component {
        id: mainWindow

        Main {}
    }

    Component {
        id: triggerSpy

        SignalSpy {
            signalName: "triggered"
        }
    }

    function test_createsTranslatedWindow() {
        const window = createTemporaryObject(mainWindow, testCase);
        verify(window, "the main window is created");
        compare(window.title, "Reverblabr");
    }

    function test_theWindowShowsItself() {
        // **The one thing a test that makes its own window cannot notice by
        // accident.** A plain application window is not visible until it is
        // told to be, and Kirigami's own window types set that for you — so
        // it goes missing exactly when the window type changes, and the
        // application then starts with no window at all.
        const window = createTemporaryObject(mainWindow, testCase);
        compare(window.visible, true);
    }

    function test_hasTheRowsEverythingElseHangsIn() {
        const window = createTemporaryObject(mainWindow, testCase);
        verify(window.menus, "there is a menu bar");
        verify(window.toolBar, "there is a toolbar row under the title bar");
        verify(window.footer, "there is a status bar");
        verify(window.sidebar, "there is a sidebar");
        verify(window.canvas, "there is somewhere for the canvas to go");
    }

    /// The colour the window is filled with: its own, or, in a window with
    /// no frame, which is transparent round its rounded corners, the
    /// background that draws its shape.
    function windowColour(window: var): color {
        return window.ownTitleBar ? window.background.color : window.color;
    }

    function cleanup() {
        // The suite runs in the appearance its style goes with (tst_qml.cpp).
        Appearance.mode = SuiteAppearance;
    }

    // **Picking an appearance recolours the open window**, through
    // the menu items a person picks it with, and is remembered.
    function test_theAppearanceMenuRecoloursTheOpenWindow() {
        const window = createTemporaryObject(mainWindow, testCase);
        // The menu says which appearance the window is in: the one this
        // run of the suite starts in (the default itself is
        // test_appearance.cpp's to hold).
        compare(window.darkAppearanceAction.checked, Appearance.mode === Appearance.ReverblabrDark);
        compare(window.systemAppearanceAction.checked, Appearance.mode === Appearance.FollowSystem);

        window.systemAppearanceAction.trigger();
        compare(testCase.windowColour(window), window.Kirigami.Theme.backgroundColor);

        window.darkAppearanceAction.trigger();
        compare(testCase.windowColour(window), Qt.color("#1b1e20"));

        window.lightAppearanceAction.trigger();
        compare(Appearance.mode, Appearance.ReverblabrLight);
        compare(window.lightAppearanceAction.checked, true);
        compare(window.darkAppearanceAction.checked, false);
        compare(testCase.windowColour(window), Qt.color("#eff0f1"));

        window.systemAppearanceAction.trigger();
        compare(testCase.windowColour(window), window.Kirigami.Theme.backgroundColor);

        window.darkAppearanceAction.trigger();
        compare(testCase.windowColour(window), Qt.color("#1b1e20"));
    }

    function test_theMenusAreTheOnesTheDesignAsksFor() {
        const window = createTemporaryObject(mainWindow, testCase);
        const titles = [];
        for (let i = 0; i < window.menus.count; ++i) {
            titles.push(window.menus.menuAt(i).title);
        }
        // Settings is where a KDE application keeps what a person chooses
        // about the window itself: which appearance it has.
        compare(titles, ["&Session", "&Edit", "&View", "Se&ttings", "&Help"]);
    }

    function test_theWindowComesUpWithASessionAndRunIsOffered() {
        // **The window is never empty**: opening the application puts
        // back what was open, and with nothing remembered it makes one. So
        // there is always something to run, and Run is there and enabled
        // rather than greyed out waiting for somebody to make a session.
        const window = createTemporaryObject(mainWindow, testCase);
        tryVerify(() => AppShell.hasSession, testCase.noticeMs,
                  "the window came up with no session in it");
        verify(window.toolBar.runAction, "the toolbar row has the Run action");
        compare(window.toolBar.runAction.text, "Run");
        tryVerify(() => window.toolBar.runAction.enabled, testCase.noticeMs,
                  "Run was not offered for the session the window opened with");

        // And it is stopped, not running: a session that started making
        // sound because the application was opened would be a surprise.
        compare(AppShell.running, false);

        // **Run reaches the engine.** The window's own singleton has to be
        // attached to the facade, and it was not: it attached only in its
        // create function, which QML does not use here, so Run was offered
        // and did nothing. Every test of AppShell attached one by hand and
        // so could not see it; this presses the action the toolbar presses.
        window.toolBar.runAction.trigger();
        tryVerify(() => AppShell.running, testCase.noticeMs, "Run did not start the session");
        window.toolBar.runAction.trigger();
        tryVerify(() => !AppShell.running, testCase.noticeMs, "Stop did not stop the session");
    }

    function test_undoAndRedoTakeBackWhatWasDone() {
        // The actions were there, greyed out for good, and Ctrl+Z did
        // nothing anywhere in the application.
        const window = createTemporaryObject(mainWindow, testCase);
        tryVerify(() => AppShell.hasSession, testCase.noticeMs, "the window came up with no session");
        window.newSessionAction.trigger();
        tryVerify(() => SessionModel.sessionId === Sessions.currentId && !window.undoAction.enabled,
                  testCase.noticeMs, "a new session offered something to undo");
        compare(window.undoAction.text, "Undo");

        const block = SessionModel.addBlock("effect.volume", Qt.point(200, 200));
        tryVerify(() => window.undoAction.enabled, testCase.noticeMs, "adding a block left nothing to undo");
        compare(window.undoAction.text, "Undo Add Block");

        // With the keys, as a person does it.
        waitForRendering(window.contentItem);
        window.sessionCanvas.forceActiveFocus();
        keyClick(Qt.Key_Z, Qt.ControlModifier);
        tryVerify(() => SessionModel.blockIds.indexOf(block) < 0, testCase.noticeMs, "Ctrl+Z did not take the block back");
        verify(window.redoAction.enabled, "there was nothing to redo after an undo");
        compare(window.redoAction.text, "Redo Add Block");

        window.redoAction.trigger();
        tryVerify(() => SessionModel.blockIds.indexOf(block) >= 0, testCase.noticeMs, "Redo did not put the block back");

        // **A text field keeps Ctrl+Z for its own typing.**
        const search = window.blockLibrary.searchField;
        search.forceActiveFocus();
        search.text = "";
        keyClick(Qt.Key_V);
        keyClick(Qt.Key_O);
        keyClick(Qt.Key_Z, Qt.ControlModifier);
        wait(100);
        verify(SessionModel.blockIds.indexOf(block) >= 0, "Ctrl+Z in the search field undid the session");
        search.text = "";
    }

    function test_nothingSitsFlushAgainstTheWindowsEdges() {
        // The sidebar's tabs and search field, the status bar's text
        // and the Run button touched the window's left or right edge.
        // Everything a person reads or presses keeps the window's margin.
        const window = createTemporaryObject(mainWindow, testCase);
        tryVerify(() => AppShell.hasSession, testCase.noticeMs, "the window came up with no session");
        waitForRendering(window.contentItem);
        const margin = Kirigami.Units.largeSpacing;
        const flush = [];
        function walk(item) {
            for (const child of item.children) {
                if (!child.visible || child.width <= 0 || child.height <= 0) {
                    continue;
                }
                const reads = typeof child.text === "string" && (child.text.length > 0 || child.placeholderText !== undefined);
                if (reads) {
                    const at = child.mapToItem(null, 0, 0);
                    if (at.x < margin - 0.5 || at.x + child.width > window.width - margin + 0.5) {
                        flush.push((child.text || child.placeholderText) + " at " + Math.round(at.x) + ".."
                                   + Math.round(at.x + child.width) + " of " + window.width);
                    }
                }
                walk(child);
            }
        }
        walk(window.toolBar);
        walk(window.footer);
        walk(window.sidebar);
        compare(flush, [], "flush against the window's edge");
    }

    function test_newSessionAddsATabAndBringsItToTheFront() {
        const window = createTemporaryObject(mainWindow, testCase);
        tryVerify(() => AppShell.hasSession, testCase.noticeMs, "the window came up with no session");
        const before = Sessions.open.length;

        window.newSessionAction.trigger();

        tryVerify(() => Sessions.open.length === before + 1, testCase.noticeMs, "no tab was added");
        compare(Sessions.currentId, Sessions.open[Sessions.open.length - 1].id,
                "the new session is not the one in front");
        compare(Sessions.nameOf(Sessions.currentId), "Untitled");
        // **The frame follows the tab**, which is the one binding the whole
        // window hangs off.
        compare(AppShell.sessionId, Sessions.currentId);
    }

    function test_theLastSessionCannotBeClosed() {
        // A window with no session has nothing to show and no way back to
        // one, so Close is offered only once there is another to fall back
        // to — greyed out rather than missing, as Run is.
        const window = createTemporaryObject(mainWindow, testCase);
        tryVerify(() => Sessions.open.length >= 1, testCase.noticeMs, "the window came up with no session");
        while (Sessions.open.length > 1) {
            Sessions.closeSession(Sessions.open[Sessions.open.length - 1].id);
        }
        compare(Sessions.open.length, 1);
        compare(window.closeSessionAction.enabled, false);

        window.newSessionAction.trigger();
        tryVerify(() => Sessions.open.length === 2, testCase.noticeMs, "no second session");
        compare(window.closeSessionAction.enabled, true);

        window.closeSessionAction.trigger();
        tryVerify(() => Sessions.open.length === 1, testCase.noticeMs, "Close closed nothing");
    }

    function test_openSessionOffersTheSavedOnesAndSaysWhichAreOpen() {
        const window = createTemporaryObject(mainWindow, testCase);
        tryVerify(() => Sessions.ready, testCase.noticeMs, "the engine never listed the sessions");
        const openId = Sessions.currentId;

        window.openSessionAction.trigger();
        const dialog = window.openSessionDialog;
        tryVerify(() => dialog.opened, testCase.noticeMs, "the dialog did not open");

        // Every saved session is offered, the one in front included: opening
        // one that is open brings it to the front, which is worth being able
        // to do.
        verify(Sessions.saved.length >= 1, "there is at least the session the window opened with");
        compare(dialog.chosenId, openId, "the list did not start at the session in front");
        // **It has somewhere to draw itself.** A dialog with no size dims the
        // window and shows nothing, which is what a Loader's zero-sized item
        // gave it before.
        verify(dialog.width > 0 && dialog.height > 0, "the dialog has no size");

        dialog.close();
        tryVerify(() => !dialog.opened, testCase.noticeMs, "the dialog did not close");
    }

    function test_f2RenamesTheSessionWhenNoTileIsSelected() {
        // **F2 renames whatever is in front of the person pressing it.**
        // With a tile selected that is the block; with none it is the
        // session, and it used to be nothing at all.
        const window = createTemporaryObject(mainWindow, testCase);
        tryVerify(() => AppShell.hasSession, testCase.noticeMs, "the window came up with no session");
        window.sessionCanvas.clearSelection();

        window.renameSessionAction.trigger();
        const dialog = window.renameSessionDialog;
        tryVerify(() => dialog.opened, testCase.noticeMs, "the dialog did not open");
        verify(dialog.width > 0 && dialog.height > 0, "the dialog has no size");

        verify(dialog.nameField, "there is somewhere to type the name");
        compare(dialog.nameField.text, SessionModel.name, "the box does not start at the name it has");
        dialog.nameField.text = "Podcast";
        dialog.accept();

        tryVerify(() => SessionModel.name === "Podcast", testCase.noticeMs, "the session was not renamed");
        tryVerify(() => Sessions.nameOf(Sessions.currentId) === "Podcast", testCase.noticeMs,
                  "the tab still shows the old name");
    }

    function test_f2RenamesTheSelectedBlockWithItsSettingsOpen() {
        // With several settings panels able to stay open, F2 belongs
        // to the window alone, and renames the block that is selected
        // whatever panels are showing.
        const window = createTemporaryObject(mainWindow, testCase);
        tryVerify(() => AppShell.hasSession, testCase.noticeMs, "the window came up with no session");
        waitForRendering(window.contentItem);
        const canvas = window.sessionCanvas;
        const block = canvas.addBlockAt("effect.volume", 200, 200);
        canvas.pressOn(block, Qt.NoModifier);
        canvas.activateBlock(block);
        verify(canvas.settings.visible, "the settings did not open");
        canvas.forceActiveFocus();

        keyClick(Qt.Key_F2);
        compare(canvas.renamingId, block, "F2 did not start renaming the selected block");
        canvas.renamingId = "";
        SessionModel.removeBlocks([block]);
    }

    function test_deletingASessionAsksFirstAndThenTakesTheTabWithIt() {
        const window = createTemporaryObject(mainWindow, testCase);
        window.newSessionAction.trigger();
        tryVerify(() => Sessions.open.length >= 2, testCase.noticeMs, "there is a session to delete");
        const doomed = Sessions.currentId;
        const saved = Sessions.saved.length;

        // **Asked first, always**: there is no undo that reaches across a
        // file. Cancelling leaves everything where it was.
        window.deleteSessionAction.trigger();
        tryVerify(() => window.deleteSessionDialog.opened, testCase.noticeMs, "the dialog did not open");
        verify(window.deleteSessionDialog.width > 0 && window.deleteSessionDialog.height > 0,
               "the dialog has no size");
        window.deleteSessionDialog.close();
        tryVerify(() => !window.deleteSessionDialog.opened, testCase.noticeMs, "the dialog stayed open");
        verify(Sessions.isOpen(doomed), "cancelling deleted it anyway");
        compare(Sessions.saved.length, saved);

        // And going through with it takes the file and the tab.
        Sessions.deleteSession(doomed);
        tryVerify(() => !Sessions.isOpen(doomed), testCase.noticeMs, "the tab outlived the session");
        tryVerify(() => Sessions.saved.length === saved - 1, testCase.noticeMs, "it is still offered");
    }

    function test_thereIsATabPerOpenSessionAndClickingOneBringsItToTheFront() {
        const window = createTemporaryObject(mainWindow, testCase);
        tryVerify(() => Sessions.open.length >= 1, testCase.noticeMs, "the window came up with no session");
        while (Sessions.open.length > 1) {
            Sessions.closeSession(Sessions.open[Sessions.open.length - 1].id);
        }
        const first = Sessions.currentId;

        window.newSessionAction.trigger();
        tryVerify(() => Sessions.open.length === 2, testCase.noticeMs, "no second session");
        const second = Sessions.currentId;
        tryVerify(() => window.sessionTabs.count === 2, testCase.noticeMs, "there is not a tab per session");

        // **A tab per open session, and its name on it.**
        const firstTab = window.sessionTabs.tabFor(first);
        verify(firstTab, "there is no tab for the first session");
        compare(firstTab.sessionId, first);
        compare(firstTab.current, false, "the tab that is not in front says it is");
        verify(window.sessionTabs.tabFor(second).current, "the tab in front does not say so");

        // Clicking one brings it to the front, and the whole window follows.
        mouseClick(firstTab);
        tryVerify(() => Sessions.currentId === first, testCase.noticeMs, "clicking a tab changed nothing");
        tryVerify(() => AppShell.sessionId === first, testCase.noticeMs, "the frame did not follow the tab");
        tryVerify(() => firstTab.current, testCase.noticeMs, "the tab does not say it is in front");
    }

    function test_aTabsCloseButtonClosesThatSessionAndIsNotThereForTheLast() {
        const window = createTemporaryObject(mainWindow, testCase);
        tryVerify(() => Sessions.open.length >= 1, testCase.noticeMs, "the window came up with no session");
        while (Sessions.open.length > 1) {
            Sessions.closeSession(Sessions.open[Sessions.open.length - 1].id);
        }
        const only = Sessions.currentId;
        tryVerify(() => window.sessionTabs.count === 1, testCase.noticeMs, "one session, one tab");

        // **The button that cannot work is not there at all**, because a
        // window with no session has nothing to show and no way back.
        compare(window.sessionTabs.tabFor(only).closeButton.visible, false);

        window.newSessionAction.trigger();
        tryVerify(() => window.sessionTabs.count === 2, testCase.noticeMs, "no second tab");
        const second = Sessions.currentId;
        tryVerify(() => window.sessionTabs.tabFor(only).closeButton.visible, testCase.noticeMs,
                  "the close button did not come back with a second session");

        mouseClick(window.sessionTabs.tabFor(second).closeButton);
        tryVerify(() => !Sessions.isOpen(second), testCase.noticeMs, "the close button closed nothing");
        tryVerify(() => window.sessionTabs.count === 1, testCase.noticeMs, "the tab outlived the session");
        compare(Sessions.currentId, only);
    }

    function test_twoSessionsRunAtOnceAndEachTabShowsItsOwnLiveDot() {
        // **What the tabs must show.** The engine runs several sessions at
        // once; what this checks is that the row says so — a tab cannot ask
        // AppShell, which knows only about the session in front, so a dot
        // that came from there would light on whichever tab was clicked.
        const window = createTemporaryObject(mainWindow, testCase);
        tryVerify(() => Sessions.open.length >= 1, testCase.noticeMs, "the window came up with no session");
        while (Sessions.open.length > 1) {
            Sessions.closeSession(Sessions.open[Sessions.open.length - 1].id);
        }
        const first = Sessions.currentId;
        window.newSessionAction.trigger();
        tryVerify(() => Sessions.open.length === 2, testCase.noticeMs, "no second session");
        const second = Sessions.currentId;

        compare(window.sessionTabs.tabFor(first).live, false);
        compare(window.sessionTabs.tabFor(second).live, false);

        TestEngine.start(first);
        tryVerify(() => window.sessionTabs.tabFor(first).live, testCase.noticeMs,
                  "a running session's tab has no live dot");
        compare(window.sessionTabs.tabFor(first).liveDot.visible, true);
        // **The other tab is not lit**, which is the thing that would be
        // wrong if the dot came from the session in front.
        compare(window.sessionTabs.tabFor(second).live, false,
                "a session that is not running was shown as running");

        TestEngine.start(second);
        tryVerify(() => window.sessionTabs.tabFor(second).live, testCase.noticeMs,
                  "the second session did not run");
        compare(window.sessionTabs.tabFor(first).live, true, "starting one session stopped the other");

        TestEngine.stop(first);
        tryVerify(() => !window.sessionTabs.tabFor(first).live, testCase.noticeMs, "the dot stayed after Stop");
        compare(window.sessionTabs.tabFor(second).live, true, "stopping one session stopped the other");
        TestEngine.stop(second);
    }

    function test_theSettingsPanelIsShownOnlyWhenABlockIsOpen() {
        // The panel is on the canvas, beside its block, and takes no room
        // until there is something to show. Checked here rather than
        // on its own, because an item with no window is never effectively
        // visible whatever its own property says.
        const window = createTemporaryObject(mainWindow, testCase);
        verify(window.settingsPanel, "there is a settings panel on the canvas");
        compare(window.settingsPanel.visible, false);

        // **A real block.** The panel used to show a header for any string
        // at all, which is how this test came to open a block that was
        // never in the session; one whose block is not there closes itself.
        const blockId = window.sessionCanvas.addBlockAfterSelection("effect.volume");
        verify(blockId !== "", "a block was added to open the settings of");

        window.settingsPanel.open(blockId);
        compare(window.settingsPanel.visible, true);
        window.settingsPanel.close();
        compare(window.settingsPanel.visible, false);
    }

    function test_theSettingsPanelClosesWhenItsBlockGoes() {
        // Removing the block a panel is open on leaves the panel with
        // nothing to show and nothing to point at.
        const window = createTemporaryObject(mainWindow, testCase);
        const blockId = window.sessionCanvas.addBlockAfterSelection("effect.volume");
        verify(blockId !== "", "a block was added");

        window.settingsPanel.open(blockId);
        compare(window.settingsPanel.visible, true);

        SessionModel.removeBlocks([blockId]);
        tryCompare(window.settingsPanel, "visible", false, testCase.noticeMs,
                   "the panel stayed open on a block that is not there any more");
    }

    function test_theSidebarTabsAreTheMenusTabs() {
        // One piece of state, so the menu item and the tab bar cannot
        // disagree about which tab is showing.
        const window = createTemporaryObject(mainWindow, testCase);
        compare(window.sidebarIndex, 0);
        window.sidebarIndex = 1;
        compare(window.sidebar.currentIndex, 1);
    }

    function test_theStatusBarSaysNothingRatherThanZero() {
        // A session that is not running has no load and no rate, and a 0 %
        // would be a measurement of something.
        const window = createTemporaryObject(mainWindow, testCase);
        compare(window.footer.hintText.length > 0, true);
        verify(window.footer.dash.length > 0, "there is something to show in place of a number");
    }

    function test_elapsedIsCountedTheWayAClockIs() {
        const window = createTemporaryObject(mainWindow, testCase);
        compare(window.toolBar.formatElapsed(0), "0:00");
        compare(window.toolBar.formatElapsed(9), "0:09");
        compare(window.toolBar.formatElapsed(61), "1:01");
        compare(window.toolBar.formatElapsed(599), "9:59");
        // Hours only once there are any, so the common case is the short one.
        compare(window.toolBar.formatElapsed(3600), "1:00:00");
        compare(window.toolBar.formatElapsed(3671), "1:01:11");
        // **A session can be left running over a weekend.** 53:17:04 is a
        // number nobody reads as two days, so past a day the days are
        // counted separately and the clock starts again.
        compare(window.toolBar.formatElapsed(86400), "1d 0:00:00");
        compare(window.toolBar.formatElapsed(90061), "1d 1:01:01");
        compare(window.toolBar.formatElapsed(2 * 86400 + 3 * 3600 + 4 * 60 + 5), "2d 3:04:05");
        compare(window.toolBar.formatElapsed(86399), "23:59:59");
    }

    // --- the transport ---

    /// Where an item of the toolbar row starts, in the row's own frame.
    function leftOf(window, item) {
        return item.mapToItem(window.toolBar, 0, 0).x;
    }

    function test_theTransportReadsStatusClockAction() {
        // What is happening, for how long, what to do about it: the order a
        // person asks those questions in, with the button where the eye ends.
        const window = createTemporaryObject(mainWindow, testCase);
        const row = window.toolBar;
        verify(testCase.leftOf(window, row.status) < testCase.leftOf(window, row.clock),
               "the status is not to the left of the clock");
        verify(testCase.leftOf(window, row.clock) < testCase.leftOf(window, row.runButton),
               "the clock is not to the left of Run/Stop");
    }

    function test_runStopIsOneFilledButtonThatChangesWithTheSession() {
        const window = createTemporaryObject(mainWindow, testCase);
        const button = window.toolBar.runButton;
        tryVerify(() => button.enabled, testCase.noticeMs, "Run was not offered");

        // One action behind the button and the menu item, as before.
        compare(button.action, window.toolBar.runAction);

        // **Filled**, in the accent colour, to start.
        compare(button.text, "Run");
        compare(Qt.colorEqual(button.background.color, AppColors.accent), true,
                "Run is not filled with the accent colour");

        // Pressing it starts the session, and it turns the live colour.
        mouseClick(button);
        tryVerify(() => AppShell.running, testCase.noticeMs, "pressing Run did not start the session");
        compare(button.text, "Stop");
        tryVerify(() => Qt.colorEqual(button.fill, AppColors.live), testCase.noticeMs,
                  "Stop is not filled with the live colour");
        compare(Qt.colorEqual(button.ink, AppColors.inkOnLive), true, "Stop is not written in the ink for it");

        mouseClick(button);
        tryVerify(() => !AppShell.running, testCase.noticeMs, "pressing Stop did not stop the session");
        tryVerify(() => Qt.colorEqual(button.fill, AppColors.accent), testCase.noticeMs,
                  "stopped, the button did not go back to the accent colour");
    }

    function test_theClockIsLargeAndOnlyBrightWhileRunning() {
        const window = createTemporaryObject(mainWindow, testCase);
        const clock = window.toolBar.clock;
        tryVerify(() => window.toolBar.runAction.enabled, testCase.noticeMs, "Run was not offered");

        verify(clock.font.pointSize > window.toolBar.status.children[1].font.pointSize,
               "the clock is no bigger than the writing beside it");
        compare(Qt.colorEqual(clock.color, AppColors.muted), true, "a clock standing still is not quiet");

        window.toolBar.runAction.trigger();
        tryVerify(() => AppShell.running, testCase.noticeMs, "Run did not start the session");
        compare(Qt.colorEqual(clock.color, AppColors.text), true, "a running clock is not bright");

        window.toolBar.runAction.trigger();
        tryVerify(() => !AppShell.running, testCase.noticeMs, "Stop did not stop the session");
    }

    TextMetrics {
        id: timeMetrics
    }

    function test_theClockDoesNotChangeWidthAsItCounts() {
        // **The row must not shuffle** at 9:59 → 10:00, or 59:59 → 1:00:00.
        // The fixed-width font makes every digit the same width, but not
        // every time the same length; so the clock is given the widest time
        // short of a day from the start.
        const window = createTemporaryObject(mainWindow, testCase);
        const clock = window.toolBar.clock;
        timeMetrics.font = clock.font;
        for (const seconds of [0, 599, 600, 3599, 3600, 86399]) {
            timeMetrics.text = window.toolBar.formatElapsed(seconds);
            verify(timeMetrics.advanceWidth <= clock.width + 0.5,
                   window.toolBar.formatElapsed(seconds) + " is wider than the room the clock keeps: "
                   + timeMetrics.advanceWidth.toFixed(1) + " > " + clock.width.toFixed(1));
        }
    }

    // --- the connection mode ---

    function test_theMenuTheCanvasAndTheStatusBarAgreeOnTheConnectionMode() {
        // **One setting, three places to see it.** The menu item, the
        // toggle on the canvas and the hint in the status bar all follow the
        // session's own setting, so a change from any of them shows in all.
        const window = createTemporaryObject(mainWindow, testCase);
        tryVerify(() => AppShell.hasSession, testCase.noticeMs, "the window came up with no session");
        waitForRendering(window.contentItem);
        const menu = window.automaticConnectionsAction;
        const mode = window.sessionCanvas.connectionMode;
        SessionModel.setAutoConnect(true);

        compare(menu.checked, true);
        compare(mode.text, "Automatic connections");
        compare(window.footer.hintText, "Automatic connections: new blocks connect themselves");

        menu.trigger();
        compare(SessionModel.autoConnect, false, "the menu item did not change the mode");
        compare(mode.text, "Manual connections", "the canvas did not follow the menu");
        compare(window.footer.hintText, "Manual connections: Ctrl-drag",
                "the status bar did not follow the menu");

        mouseClick(mode);
        compare(SessionModel.autoConnect, true, "the canvas did not change the mode");
        compare(menu.checked, true, "the menu did not follow the canvas");
        compare(window.footer.hintText, "Automatic connections: new blocks connect themselves");
    }

    // --- the session tabs read as tabs ---

    /// Down to one session and then a second, with the second in front.
    function twoSessions(window) {
        tryVerify(() => Sessions.open.length >= 1, testCase.noticeMs, "the window came up with no session");
        while (Sessions.open.length > 1) {
            Sessions.closeSession(Sessions.open[Sessions.open.length - 1].id);
        }
        const first = Sessions.currentId;
        window.newSessionAction.trigger();
        tryVerify(() => Sessions.open.length === 2, testCase.noticeMs, "no second session");
        tryVerify(() => window.sessionTabs.count === 2, testCase.noticeMs, "there is not a tab per session");
        return [first, Sessions.currentId];
    }

    function test_theTabInFrontIsUnderlinedNotFilled() {
        const window = createTemporaryObject(mainWindow, testCase);
        const [behind, front] = testCase.twoSessions(window);
        const frontTab = window.sessionTabs.tabFor(front);
        const behindTab = window.sessionTabs.tabFor(behind);

        // **Not a selected row.** No fill in the accent colour.
        verify(!Qt.colorEqual(frontTab.background.color, AppColors.accent),
               "the tab in front is still filled with the accent colour");
        compare(frontTab.background.color.a, 0, "the tab in front has a fill at all");

        // A rule under it instead, and its name in a heavier weight.
        compare(frontTab.underline.visible, true, "the tab in front is not underlined");
        compare(Qt.colorEqual(frontTab.underline.color, AppColors.accentMark), true);
        verify(frontTab.underline.height >= 2, "the rule under the tab in front is a hairline");
        verify(frontTab.nameLabel.font.weight > behindTab.nameLabel.font.weight,
               "the tab in front is not set heavier than the one behind");
        compare(Qt.colorEqual(frontTab.nameLabel.color, AppColors.text), true);

        // The one behind is quiet.
        compare(behindTab.underline.visible, false, "the tab behind is underlined too");
        compare(Qt.colorEqual(behindTab.nameLabel.color, AppColors.muted), true);
    }

    function test_thePlusAtTheEndOfTheStripStartsASession() {
        const window = createTemporaryObject(mainWindow, testCase);
        const [, front] = testCase.twoSessions(window);
        waitForRendering(window.contentItem);
        const plus = window.sessionTabs.newTabButton;
        verify(plus, "there is no + in the tab strip");

        // **The menu's own action**, so the two are one thing.
        const triggered = createTemporaryObject(triggerSpy, testCase, { target: window.newSessionAction });
        compare(plus.enabled, window.newSessionAction.enabled);

        // After the last tab.
        const lastTab = window.sessionTabs.tabFor(front);
        verify(plus.mapToItem(window.sessionTabs, 0, 0).x >= lastTab.mapToItem(window.sessionTabs, lastTab.width, 0).x,
               "the + is not after the last tab");

        mouseClick(plus);
        compare(triggered.count, 1, "pressing + did not trigger the menu's New Session");
        tryVerify(() => Sessions.open.length === 3, testCase.noticeMs, "pressing + did not start a session");
        verify(Sessions.currentId !== front, "the new session did not come to the front");
    }
}

