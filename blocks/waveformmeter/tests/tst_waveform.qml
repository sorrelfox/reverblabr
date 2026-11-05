// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Waveform block's tile and card offscreen, against the mock engine: the
// mock writes its made-up sound through the same pool a real block does, so
// a trace here has come the whole way. What the block and the drawing do is
// in blocks.WaveformMeter, app.ScopeView and app.ScopeItem.

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Window
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "Waveform"
    when: windowShown
    visible: true
    width: 640
    height: 480

    property string sessionId: ""

    readonly property var words: WaveformWords {}

    Component {
        id: otherWindow

        Window {
            width: 640
            height: 480
            visible: true
        }
    }

    function panelFor() {
        const panel = createTemporaryObject(BlockPanels.panelFor("meter.waveform"), testCase, { width: 520 });
        panel.blockId = "scope";
        return panel;
    }

    /// A panel with its Advanced part open, by a press on its heading as a
    /// person opens it: the settings are folded away to begin with.
    function openPanelFor() {
        const panel = panelFor();
        mouseClick(panel.advancedSection.toggle);
        verify(panel.advancedSection.expanded);
        return panel;
    }

    function faceFor() {
        const face = createTemporaryObject(BlockFaces.faceFor("meter.waveform"), testCase, { width: 200, height: 90 });
        face.blockId = "scope";
        return face;
    }

    function clickIn(repeater, id) {
        for (let index = 0; index < repeater.count; ++index) {
            const button = repeater.itemAt(index);
            if (button.modelData === id) {
                mouseClick(button);
                return;
            }
        }
        fail("no choice " + id);
    }

    function checkedIn(repeater) {
        for (let index = 0; index < repeater.count; ++index) {
            if (repeater.itemAt(index).checked) {
                return repeater.itemAt(index).modelData;
            }
        }
        return "";
    }

    function run() {
        TestEngine.start(testCase.sessionId);
        tryVerify(() => Meters.live, 2000, "the session runs");
    }

    function halt() {
        TestEngine.stop(testCase.sessionId);
        tryVerify(() => !Meters.live, 2000, "the session stopped");
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Waveform");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "scope", type: "meter.waveform" }, index: -1 }));
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "other", type: "meter.waveform" }, index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
        Meters.watch(testCase.sessionId);
    }

    function cleanupTestCase() {
        TestEngine.setFakeScopeLease(3000);
    }

    function init() {
        TestEngine.setFakeOverload(testCase.sessionId, false);
        TestEngine.setFakeFrozen(testCase.sessionId, false);
    }

    function cleanup() {
        if (Meters.live) {
            halt();
        }
        for (const [paramId, value] of [["hold", false], ["motion", "realTime"], ["style", "line"], ["spanMs", 200],
                                        ["trigger", false], ["channels", "both"], ["grid", true], ["gainDb", 0],
                                        ["autoFit", false], ["scale", "linear"], ["refresh", "hz30"]]) {
            SessionModel.setParam("scope", paramId, value);
        }
    }

    function test_theWordsMatchTheCatalogue() {
        const lists = { refresh: words.refreshRates, motion: words.motions, style: words.styles,
                        channels: words.channelChoices, scale: words.scales };
        for (const param of BlockCatalogue.params("meter.waveform")) {
            if (lists[param.id] !== undefined) {
                compare(param.choices, lists[param.id], param.id);
                for (const choice of param.choices) {
                    verify(ChoiceWords.hasWords("meter.waveform", param.id, choice), param.id + "/" + choice);
                }
            }
        }
    }

    function test_theCardsRangesAreTheBlocks() {
        const panel = panelFor();
        const param = id => BlockCatalogue.describeParam("meter.waveform", id);
        compare(panel.shortestSpan, param("spanMs").minimum);
        compare(panel.longestSpan, param("spanMs").maximum);
        compare(panel.lowestGain, param("gainDb").minimum);
        compare(panel.highestGain, param("gainDb").maximum);
    }

    function test_theSettingsAreFoldedUnderAdvancedAtFirst() {
        const panel = panelFor();
        const advanced = panel.advancedSection;
        verify(!advanced.expanded, "Advanced starts folded");
        verify(panel.scopeWell.visible, "the graph stays in view");
        verify(panel.holdButton.visible, "Hold stays in reach");
        verify(!panel.gridCheck.visible, "the settings are hidden");
        verify(!panel.motionChoices.itemAt(0).visible);
        mouseClick(advanced.toggle);
        verify(advanced.expanded);
        verify(panel.gridCheck.visible, "Advanced shows the settings");
        verify(panel.motionChoices.itemAt(0).visible);
        mouseClick(advanced.toggle);
        verify(!panel.gridCheck.visible, "and folds them away again");
    }

    function test_theLibraryOffersItWithTheMeters() {
        compare(BlockCatalogue.describe("meter.waveform").category, "meters");
        verify(BlockHelp.pageFor("meter.waveform") !== null);
    }

    function test_aTraceArrivesOnBothSurfacesFromOneFeed() {
        const panel = panelFor();
        const face = faceFor();
        run();
        tryVerify(() => panel.scope.showing, 2000, "the card has a trace");
        tryVerify(() => face.scope.showing, 2000, "the tile has a trace");
        compare(Meters.scopes.feedCount(), 1);
        verify(TestEngine.scopesGiven(testCase.sessionId).indexOf("scope") >= 0);
        compare(panel.statusLabel.text, words.status("live", false));
        compare(face.dot, "live");
        tryVerify(() => panel.scope.pictures > 3, 2000, "the trace goes on being drawn");
    }

    function test_severalBlocksEachGetTheirOwnSlot() {
        const one = faceFor();
        const two = createTemporaryObject(BlockFaces.faceFor("meter.waveform"), testCase, { width: 200, height: 90 });
        two.blockId = "other";
        run();
        tryVerify(() => one.scope.showing && two.scope.showing, 2000);
        const given = TestEngine.scopesGiven(testCase.sessionId);
        verify(given.indexOf("scope") >= 0 && given.indexOf("other") >= 0);
    }

    function test_nothingIsAskedForOnceNothingShows() {
        TestEngine.setFakeScopeLease(100);
        run();
        const panel = panelFor();
        tryVerify(() => TestEngine.scopesGiven(testCase.sessionId).length === 1, 2000);
        panel.visible = false;
        tryVerify(() => TestEngine.scopesGiven(testCase.sessionId).length === 0, 2000, "the slot was let go of");
        compare(Meters.scopes.feedCount(), 0);
        panel.visible = true;
        tryVerify(() => TestEngine.scopesGiven(testCase.sessionId).length === 1, 2000, "and asked for again");
        TestEngine.setFakeScopeLease(3000);
    }

    function test_noTraceIsLeftStandingAndAWordSaysWhy() {
        const panel = panelFor();
        const face = faceFor();
        compare(panel.feedState, "stopped");
        verify(!panel.scope.showing);
        compare(panel.scopeWell.absenceLabel.text, words.absence("stopped"));
        compare(face.scopeWell.absenceLabel.text, words.absence("stopped"));
        verify(panel.scopeWell.absenceLabel.visible);

        run();
        tryVerify(() => panel.scope.showing, 2000);
        verify(!panel.scopeWell.absenceLabel.visible);

        TestEngine.setFakeOverload(testCase.sessionId, true);
        tryCompare(panel, "feedState", "overloaded", 2000);
        verify(!panel.scope.showing);
        compare(panel.statusLabel.text, words.absence("overloaded"));
        compare(face.dot, "warning");
        TestEngine.setFakeOverload(testCase.sessionId, false);
        tryVerify(() => panel.scope.showing, 2000);

        TestEngine.setFakeFrozen(testCase.sessionId, true);
        tryCompare(panel, "feedState", "noNews", 2000);
        verify(!panel.scope.showing, "an engine that died leaves its last trace");
        TestEngine.setFakeFrozen(testCase.sessionId, false);
        tryVerify(() => panel.scope.showing, 2000);

        SessionModel.setBlockEnabled("scope", false);
        tryCompare(panel, "feedState", "off", 2000);
        verify(!panel.scope.showing);
        SessionModel.setBlockEnabled("scope", true);
        tryVerify(() => panel.scope.showing, 2000);

        halt();
        tryCompare(panel, "feedState", "stopped", 2000);
        verify(!panel.scope.showing);
    }

    function test_holdKeepsTheTraceAndSaysSo() {
        const panel = panelFor();
        run();
        tryVerify(() => panel.scope.showing, 2000);
        mouseClick(panel.holdButton);
        verify(panel.reader.hold);
        compare(panel.statusLabel.text, words.status("", true));
        wait(100);
        const pictures = panel.scope.pictures;
        wait(150);
        compare(panel.scope.pictures, pictures, "a held trace takes no new sound");
        halt();
        verify(panel.scope.showing, "it stays, as it was asked to");

        // Undo lets go of it, as of any other setting.
        SessionModel.undo();
        tryVerify(() => !panel.reader.hold, 2000);
        verify(!panel.holdButton.checked);
        tryVerify(() => !panel.scope.showing, 2000, "with no sound there is then nothing to show");
    }

    function test_everyControlWritesTheSessionAndAnUndoComesBack() {
        const panel = openPanelFor();
        clickIn(panel.styleChoices, "bars");
        compare(panel.reader.style, "bars");
        compare(panel.scope.style, "bars");
        clickIn(panel.motionChoices, "smooth");
        compare(panel.scope.motion, "smooth");
        clickIn(panel.refreshChoices, "hz60");
        compare(panel.scope.refreshHz, 60);
        clickIn(panel.channelChoices, "stacked");
        compare(panel.scope.channels, "stacked");
        clickIn(panel.scaleChoices, "decibels");
        compare(panel.scope.heightScale, "decibels");
        mouseClick(panel.triggerCheck);
        verify(panel.scope.trigger);
        mouseClick(panel.gridCheck);
        verify(!panel.scope.grid);
        mouseClick(panel.autoFitCheck);
        verify(panel.scope.autoFit);
        verify(!panel.gainRow.enabled, "Auto fit decides the gain");

        for (let step = 0; step < 8; ++step) {
            SessionModel.undo();
        }
        tryCompare(panel.scope, "style", "line", 2000);
        compare(checkedIn(panel.styleChoices), "line", "the card follows an undo");
        compare(checkedIn(panel.motionChoices), "realTime");
        compare(checkedIn(panel.refreshChoices), "hz30");
        compare(checkedIn(panel.channelChoices), "both");
        compare(checkedIn(panel.scaleChoices), "linear");
        verify(!panel.triggerCheck.checked);
        verify(panel.gridCheck.checked);
        verify(!panel.autoFitCheck.checked);
    }

    function test_aStyleShowsAtOnceInEveryAppearance() {
        const panel = panelFor();
        run();
        tryVerify(() => panel.scope.showing, 2000);
        for (const appearance of [Appearance.ReverblabrDark, Appearance.ReverblabrLight]) {
            Appearance.mode = appearance;
            compare(panel.scope.firstColor, AppColors.accent);
            compare(panel.scope.secondColor, AppColors.live);
            compare(panel.scope.gridColor, AppColors.scopeGrid);
            verify(panel.scope.firstColor !== panel.scope.secondColor);
            for (const style of words.styles) {
                SessionModel.setParam("scope", "style", style);
                compare(panel.scope.style, style);
            }
        }
        Appearance.mode = Appearance.ReverblabrDark;
    }

    function test_smoothAsksForEveryFrameAndRealTimeDoesNot() {
        const panel = panelFor();
        run();
        tryVerify(() => panel.scope.showing, 2000);
        verify(!panel.scope.ticking);
        SessionModel.setParam("scope", "motion", "smooth");
        tryVerify(() => panel.scope.ticking, 2000);
        halt();
        tryVerify(() => !panel.scope.ticking, 2000, "with no trace there is nothing to move");
    }

    function test_aRefreshFasterThanTheCyclesIsSaidToBe() {
        const panel = openPanelFor();
        run();
        tryVerify(() => panel.arrivals > 0, 2000);
        verify(!panel.refreshNote.visible, "30 a second is under what a cycle of 1024 brings");
        clickIn(panel.refreshChoices, "hz120");
        verify(panel.refreshNote.visible);
    }

    function test_theTriggerSaysWhenItFoundNothing() {
        const panel = openPanelFor();
        TestEngine.setFakeLevel(testCase.sessionId, "scope", 0);
        run();
        SessionModel.setParam("scope", "trigger", true);
        tryVerify(() => panel.scope.showing, 2000);
        tryVerify(() => panel.triggerNote.visible, 2000, "silence has no crossing");
        TestEngine.setFakeLevel(testCase.sessionId, "scope", 0.5);
        tryVerify(() => panel.scope.crossed, 2000, "a tone has");
        verify(!panel.triggerNote.visible);
    }

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
        id: shortCanvas

        Window {
            property alias canvas: inner

            width: 1100
            height: 300
            visible: true

            SessionCanvas {
                id: inner

                anchors.fill: parent
            }
        }
    }

    function test_aCardShorterThanItsSettingsScrollsTheFirstTimeItOpens() {
        // **The view scrolls through what it is told it holds**, and it was
        // told the height of the empty card it had been before its block:
        // in a window too short for the card, the first Waveform card to
        // open showed no scroll bar, and the settings below its picture
        // could not be reached until something made the card grow.
        const window = createTemporaryObject(shortCanvas, null);
        tryVerify(() => window.canvas.mainWindowShowing, 2000, "the canvas's window is not showing");
        const canvas = window.canvas;
        const block = canvas.addBlockAt("meter.waveform", 300, 100);
        canvas.openSettings(block);
        const panel = canvas.settings;
        tryVerify(() => panel.body.item !== null && panel.body.item.blockId === block, 2000);
        const view = panel.bodyView;
        tryVerify(() => view.contentHeight > view.height, 2000, "the card is not shorter than its settings");
        tryCompare(view.contentItem, "contentHeight", view.contentHeight, 2000, "the view scrolls through less");
        verify(view.QQC2.ScrollBar.vertical.size < 1, "no scroll bar says there is more");
        SessionModel.removeBlocks([block]);
    }

    function test_aCardOutsideTheMainWindowKeepsDrawingBesideItsTile() {
        const window = createTemporaryObject(shownCanvas, null);
        tryVerify(() => window.canvas.mainWindowShowing, 2000, "the canvas's window is not showing");
        const canvas = window.canvas;
        const block = canvas.addBlockAt("meter.waveform", 200, 200);
        canvas.openSettings(block);
        canvas.settings.detachButton.clicked();
        const panel = canvas.detachedPanelFor(block);
        const body = panel.body.item;
        verify(body !== null && body.scope !== undefined, "the card is not the Waveform's");
        SessionModel.setParam(block, "motion", "smooth");
        run();
        tryVerify(() => body.scope.showing, 2000);
        tryVerify(() => body.scope.ticking, 2000);

        panel.outsideButton.clicked();
        tryVerify(() => panel.surface !== null, 2000, "the panel got no window");
        compare(body.scope.Window.window, panel.surface);
        tryVerify(() => panel.surface.visible, 2000);
        verify(body.scope.showing, "the trace went with the card");
        tryVerify(() => body.scope.ticking, 2000);
        let pictures = body.scope.pictures;
        tryVerify(() => body.scope.pictures > pictures + 2, 2000, "it goes on being drawn in its own window");

        panel.outsideButton.clicked();
        tryVerify(() => panel.surface === null, 2000, "the panel's window stayed");
        compare(body.scope.Window.window, window);
        tryVerify(() => body.scope.ticking, 2000);
        pictures = body.scope.pictures;
        tryVerify(() => body.scope.pictures > pictures + 2, 2000, "and back in the main one");
        SessionModel.removeBlocks([block]);
        WindowState.setDetachedPanels(testCase.sessionId, []);
    }

    function test_theCardKeepsDrawingInAWindowOfItsOwn() {
        const panel = panelFor();
        SessionModel.setParam("scope", "motion", "smooth");
        run();
        tryVerify(() => panel.scope.ticking, 2000);
        const window = createTemporaryObject(otherWindow, testCase);
        verify(waitForRendering(window.contentItem));

        for (let move = 0; move < 4; ++move) {
            panel.parent = move % 2 === 0 ? window.contentItem : testCase;
            verify(panel.scope.showing, "the trace goes with the card");
            tryVerify(() => panel.scope.ticking, 2000);
            const pictures = panel.scope.pictures;
            tryVerify(() => panel.scope.pictures > pictures, 2000, "it goes on being drawn");
        }
        compare(Meters.scopes.feedCount(), 1);
    }
}
