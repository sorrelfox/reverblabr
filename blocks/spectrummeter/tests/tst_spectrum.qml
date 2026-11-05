// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Spectrum block's tile and card offscreen, against the mock engine: the
// mock writes its made-up sound through the same pool a real block does, so
// bars here have come the whole way. What the block and the drawing do is in
// blocks.SpectrumMeter, dsp.BarSpectrum, app.SpectrumView and app.SpectrumItem.

import QtQuick
import QtQuick.Window
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "Spectrum"
    when: windowShown
    visible: true
    width: 640
    height: 480

    property string sessionId: ""

    readonly property var words: SpectrumWords {}

    Component {
        id: otherWindow

        Window {
            width: 640
            height: 480
            visible: true
        }
    }

    function panelFor() {
        const panel = createTemporaryObject(BlockPanels.panelFor("meter.spectrum"), testCase, { width: 520 });
        panel.blockId = "bars";
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
        const face = createTemporaryObject(BlockFaces.faceFor("meter.spectrum"), testCase, { width: 200, height: 90 });
        face.blockId = "bars";
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

    /// Bars there are, which takes a frame's worth of sound to arrive.
    function barsArrive(surface) {
        tryVerify(() => {
            surface.scope.work();
            return surface.scope.levels().length > 0;
        }, 3000, "no bars arrived");
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Spectrum");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "bars", type: "meter.spectrum" }, index: -1 }));
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "other", type: "meter.spectrum" }, index: -1 }));
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
        for (const [paramId, value] of [["hold", false], ["motion", "realTime"], ["refresh", "hz30"], ["bars", "bars64"],
                                        ["scale", "logarithmic"], ["fftSize", "fft4096"], ["floor", "db80"],
                                        ["tilt", "tilt0"], ["peaks", true], ["channels", "mix"], ["grid", true],
                                        ["style", "solid"]]) {
            SessionModel.setParam("bars", paramId, value);
        }
    }

    function test_theWordsMatchTheCatalogue() {
        const lists = { refresh: words.refreshRates, motion: words.motions, bars: words.barCounts, scale: words.scales,
                        fftSize: words.fftSizes, floor: words.floors, tilt: words.tilts,
                        channels: words.channelChoices, style: words.styles };
        let found = 0;
        for (const param of BlockCatalogue.params("meter.spectrum")) {
            if (lists[param.id] !== undefined) {
                ++found;
                compare(param.choices, lists[param.id], param.id);
                for (const choice of param.choices) {
                    verify(ChoiceWords.hasWords("meter.spectrum", param.id, choice), param.id + "/" + choice);
                }
            }
        }
        compare(found, Object.keys(lists).length);
    }

    function test_whatItIsSetToIsANumberTheSumCanUse() {
        compare(words.barCountOf("bars128"), 128);
        compare(words.fftSizeOf("fft8192"), 8192);
        compare(words.floorDbOf("db120"), -120);
        compare(words.tiltDbOf("tilt0"), 0);
        compare(words.tiltDbOf("tilt3"), 3);
        compare(words.tiltDbOf("tilt4p5"), 4.5);
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
        compare(BlockCatalogue.describe("meter.spectrum").category, "meters");
        verify(BlockHelp.pageFor("meter.spectrum") !== null);
    }

    function test_barsArriveOnBothSurfacesFromOneFeed() {
        const panel = panelFor();
        const face = faceFor();
        run();
        tryVerify(() => panel.scope.showing, 2000, "the card has bars");
        tryVerify(() => face.scope.showing, 2000, "the tile has bars");
        compare(Meters.scopes.feedCount(), 1);
        verify(TestEngine.scopesGiven(testCase.sessionId).indexOf("bars") >= 0);
        compare(panel.statusLabel.text, words.status("live", false));
        compare(face.dot, "live");
        barsArrive(panel);
        tryVerify(() => panel.scope.pictures > 3, 2000, "the bars go on being worked out");
    }

    function test_aToneReachesItsBarAtItsLevel() {
        const panel = panelFor();
        SessionModel.setParam("bars", "channels", "left");
        TestEngine.setFakeLevel(testCase.sessionId, "bars", 1);
        run();
        barsArrive(panel);
        tryVerify(() => {
            panel.scope.work();
            return Math.abs(panel.scope.levels()[panel.scope.barOf(1000)]) < 0.5;
        }, 3000, "the mock's full-scale tone reads 0 dBFS at 1 kHz on the left");
        // The right has half of it, and a mix three quarters.
        SessionModel.setParam("bars", "channels", "right");
        tryVerify(() => {
            panel.scope.work();
            return Math.abs(panel.scope.levels()[panel.scope.barOf(1000)] + 6) < 0.5;
        }, 3000);
        SessionModel.setParam("bars", "channels", "mix");
        tryVerify(() => {
            panel.scope.work();
            return Math.abs(panel.scope.levels()[panel.scope.barOf(1000)] + 2.5) < 0.5;
        }, 3000);
    }

    function test_severalBlocksEachGetTheirOwnSlot() {
        const one = faceFor();
        const two = createTemporaryObject(BlockFaces.faceFor("meter.spectrum"), testCase, { width: 200, height: 90 });
        two.blockId = "other";
        run();
        tryVerify(() => one.scope.showing && two.scope.showing, 2000);
        const given = TestEngine.scopesGiven(testCase.sessionId);
        verify(given.indexOf("bars") >= 0 && given.indexOf("other") >= 0);
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

    function test_noBarsAreLeftStandingAndAWordSaysWhy() {
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
        verify(!panel.scope.showing, "an engine that died leaves its last bars");
        TestEngine.setFakeFrozen(testCase.sessionId, false);
        tryVerify(() => panel.scope.showing, 2000);

        SessionModel.setBlockEnabled("bars", false);
        tryCompare(panel, "feedState", "off", 2000);
        verify(!panel.scope.showing);
        SessionModel.setBlockEnabled("bars", true);
        tryVerify(() => panel.scope.showing, 2000);

        halt();
        tryCompare(panel, "feedState", "stopped", 2000);
        verify(!panel.scope.showing);
    }

    function test_holdKeepsTheBarsAndSaysSo() {
        const panel = panelFor();
        run();
        barsArrive(panel);
        mouseClick(panel.holdButton);
        verify(panel.reader.hold);
        compare(panel.statusLabel.text, words.status("", true));
        wait(100);
        const pictures = panel.scope.pictures;
        const levels = panel.scope.levels();
        wait(150);
        compare(panel.scope.pictures, pictures, "held bars take no new sound");
        compare(panel.scope.levels(), levels);
        halt();
        verify(panel.scope.showing, "they stay, as they were asked to");

        // Undo lets go of them, as of any other setting.
        SessionModel.undo();
        tryVerify(() => !panel.reader.hold, 2000);
        verify(!panel.holdButton.checked);
        tryVerify(() => !panel.scope.showing, 2000, "with no sound there is then nothing to show");
    }

    function test_everyControlWritesTheSessionAndAnUndoComesBack() {
        const panel = openPanelFor();
        clickIn(panel.barsChoices, "bars128");
        compare(panel.reader.bars, "bars128");
        compare(panel.scope.bars, 128);
        clickIn(panel.motionChoices, "smooth");
        compare(panel.scope.motion, "smooth");
        clickIn(panel.refreshChoices, "hz60");
        compare(panel.scope.refreshHz, 60);
        clickIn(panel.scaleChoices, "linear");
        compare(panel.scope.frequencyScale, "linear");
        clickIn(panel.fftSizeChoices, "fft8192");
        compare(panel.scope.frameSize, 8192);
        clickIn(panel.floorChoices, "db120");
        compare(panel.scope.floorDb, -120);
        clickIn(panel.tiltChoices, "tilt4p5");
        compare(panel.scope.tiltDb, 4.5);
        clickIn(panel.channelChoices, "left");
        compare(panel.scope.channels, "left");
        clickIn(panel.styleChoices, "segmented");
        compare(panel.scope.barStyle, "segmented");
        mouseClick(panel.peaksCheck);
        verify(!panel.scope.peakMarks);
        mouseClick(panel.gridCheck);
        verify(!panel.scope.grid);

        for (let step = 0; step < 11; ++step) {
            SessionModel.undo();
        }
        tryCompare(panel.scope, "bars", 64, 2000);
        compare(checkedIn(panel.barsChoices), "bars64", "the card follows an undo");
        compare(checkedIn(panel.motionChoices), "realTime");
        compare(checkedIn(panel.refreshChoices), "hz30");
        compare(checkedIn(panel.scaleChoices), "logarithmic");
        compare(checkedIn(panel.fftSizeChoices), "fft4096");
        compare(checkedIn(panel.floorChoices), "db80");
        compare(checkedIn(panel.tiltChoices), "tilt0");
        compare(checkedIn(panel.channelChoices), "mix");
        compare(checkedIn(panel.styleChoices), "solid");
        verify(panel.peaksCheck.checked);
        verify(panel.gridCheck.checked);
    }

    function test_theColoursComeFromTheApplicationInEveryAppearance() {
        const panel = panelFor();
        run();
        tryVerify(() => panel.scope.showing, 2000);
        for (const appearance of [Appearance.ReverblabrDark, Appearance.ReverblabrLight]) {
            Appearance.mode = appearance;
            compare(panel.scope.barColor, AppColors.accent);
            compare(panel.scope.peakColor, AppColors.live);
            compare(panel.scope.gridColor, AppColors.scopeGrid);
            verify(panel.scope.barColor !== panel.scope.peakColor);
            for (const style of words.styles) {
                SessionModel.setParam("bars", "style", style);
                compare(panel.scope.barStyle, style);
            }
        }
        Appearance.mode = Appearance.ReverblabrDark;
    }

    function test_theGridsNumbersAreOnTheCardWhileItIsOn() {
        const panel = openPanelFor();
        const face = faceFor();
        const well = panel.scopeWell;
        verify(well.showsLabels);
        verify(!face.scopeWell.showsLabels, "a tile has no room for them");
        compare(well.frequencyLabels.count, 3);
        compare(well.frequencyLabels.itemAt(1).text, words.frequencyText(1000));
        compare(well.levelLabels.count, 3);
        compare(well.levelLabels.itemAt(0).text, words.levelText(-20));
        verify(panel.scope.width < well.width && panel.scope.height < well.height, "the numbers have a strip of their own");

        SessionModel.setParam("bars", "floor", "db120");
        compare(well.levelLabels.count, 5);
        SessionModel.setParam("bars", "scale", "linear");
        compare(well.frequencyLabels.count, 3);
        compare(well.frequencyLabels.itemAt(1).text, words.frequencyText(10000));

        mouseClick(panel.gridCheck);
        compare(well.frequencyLabels.count, 0);
        compare(well.levelLabels.count, 0);
        compare(panel.scope.width, well.width);
    }

    function test_theCardSaysWhatTheFftSizeComesTo() {
        const panel = openPanelFor();
        verify(!panel.fftSizeNote.visible, "nothing is known until sound is here");
        run();
        barsArrive(panel);
        tryVerify(() => panel.fftSizeNote.visible, 2000);
        fuzzyCompare(panel.scope.frameMs, 85.3, 0.1);
        fuzzyCompare(panel.scope.resolutionHz, 23.4, 0.1);
        clickIn(panel.fftSizeChoices, "fft1024");
        fuzzyCompare(panel.scope.frameMs, 21.3, 0.1);
        fuzzyCompare(panel.scope.resolutionHz, 93.75, 0.1);
    }

    function test_smoothAsksForEveryFrameAndRealTimeDoesNot() {
        const panel = panelFor();
        run();
        tryVerify(() => panel.scope.showing, 2000);
        verify(!panel.scope.ticking);
        SessionModel.setParam("bars", "motion", "smooth");
        tryVerify(() => panel.scope.ticking, 2000);
        halt();
        tryVerify(() => !panel.scope.ticking, 2000, "with no bars there is nothing to move");
    }

    function test_aRefreshFasterThanTheCyclesIsSaidToBe() {
        const panel = openPanelFor();
        run();
        tryVerify(() => panel.arrivals > 0, 2000);
        verify(!panel.refreshNote.visible, "30 a second is under what a cycle of 1024 brings");
        clickIn(panel.refreshChoices, "hz120");
        verify(panel.refreshNote.visible);
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

    function test_aCardOutsideTheMainWindowKeepsDrawingBesideItsTile() {
        const window = createTemporaryObject(shownCanvas, null);
        tryVerify(() => window.canvas.mainWindowShowing, 2000, "the canvas's window is not showing");
        const canvas = window.canvas;
        const block = canvas.addBlockAt("meter.spectrum", 200, 200);
        canvas.openSettings(block);
        canvas.settings.detachButton.clicked();
        const panel = canvas.detachedPanelFor(block);
        const body = panel.body.item;
        verify(body !== null && body.scope !== undefined, "the card is not the Spectrum's");
        SessionModel.setParam(block, "motion", "smooth");
        run();
        tryVerify(() => body.scope.showing, 2000);
        tryVerify(() => body.scope.ticking, 2000);

        panel.outsideButton.clicked();
        tryVerify(() => panel.surface !== null, 2000, "the panel got no window");
        compare(body.scope.Window.window, panel.surface);
        tryVerify(() => panel.surface.visible, 2000);
        verify(body.scope.showing, "the bars went with the card");
        tryVerify(() => body.scope.ticking, 2000);
        let pictures = body.scope.pictures;
        tryVerify(() => body.scope.pictures > pictures + 2, 2000, "they go on being drawn in its own window");

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
        SessionModel.setParam("bars", "motion", "smooth");
        run();
        tryVerify(() => panel.scope.ticking, 2000);
        const window = createTemporaryObject(otherWindow, testCase);
        verify(waitForRendering(window.contentItem));

        for (let move = 0; move < 4; ++move) {
            panel.parent = move % 2 === 0 ? window.contentItem : testCase;
            verify(panel.scope.showing, "the bars go with the card");
            tryVerify(() => panel.scope.ticking, 2000);
            const pictures = panel.scope.pictures;
            tryVerify(() => panel.scope.pictures > pictures, 2000, "they go on being drawn");
        }
        compare(Meters.scopes.feedCount(), 1);
    }
}
