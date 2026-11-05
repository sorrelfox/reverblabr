// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Parametric EQ's face and panel: the response drawn on both, band
// handles dragged across and up the curve, chips to pick a band by, bands
// added and deleted, and the picked band's settings.
//
// **The drags are driven as a pointer drives them** — press, moves,
// release, on the window — with the panel inside a Flickable, as the
// settings card puts it in a scroll view: a drag the flickable took away
// after a few pixels was why the Dynamics curve's handles did not follow
// the pointer, and only a real press and real moves can show it.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "ParametricEq"
    when: windowShown

    property string sessionId: ""
    property int made: 0

    Component {
        id: faceComponent

        ParametricEqFace {
            width: AppMetrics.tileOuterWidth - 2
            height: AppMetrics.tileOuterHeight - AppMetrics.tileBandHeight - 2
        }
    }

    // The panel in a window, inside a Flickable taller than the window, as
    // the settings card holds it.
    Component {
        id: shownPanel

        Window {
            property alias panel: inner
            property alias flickable: scroller

            width: 460
            height: 360
            visible: true

            Flickable {
                id: scroller

                anchors.fill: parent
                contentWidth: width
                contentHeight: inner.implicitHeight + 200

                ParametricEqPanel {
                    id: inner

                    width: 420
                }
            }
        }
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Parametric EQ");
        verify(testCase.sessionId !== "", "the mock opened a session");
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function cleanup() {
        Meters.watch("");
    }

    /// A fresh block, so each test starts from the block's own defaults.
    function newBlock(): string {
        testCase.made += 1;
        const id = "peq" + testCase.made;
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: id, type: "advanced.parametriceq" }, index: -1 }));
        tryVerify(() => SessionModel.blockType(id) === "advanced.parametriceq", 1000, "the block was not added");
        return id;
    }

    function openPanel(blockId: string): var {
        // The window watches its session's meters from the start, as Main
        // does: a panel's ask for a spectrum goes to that session.
        Meters.watch(testCase.sessionId);
        const window = createTemporaryObject(shownPanel, null);
        window.panel.blockId = blockId;
        tryVerify(() => window.panel.plotArea.width > 0 && window.panel.plot.curve.length > 0, 2000,
                  "the plot was never laid out");
        return window;
    }

    /// Where a band's handle is, in the window's coordinates.
    function handleInWindow(window: var, band: int): point {
        const panel = window.panel;
        const point = panel.handlePoint(band);
        return panel.plotArea.mapToItem(window.contentItem, point.x, point.y);
    }

    function test_itIsAnAdvancedBlockWithAFaceAndAPanelOfItsOwn() {
        verify(BlockCatalogue.types.indexOf("advanced.parametriceq") >= 0, "the engine does not offer it");
        compare(BlockPresentation.category("advanced.parametriceq"), "advanced");
        compare(BlockPresentation.name("advanced.parametriceq"), "Parametric EQ");
        verify(BlockFaces.faceFor("advanced.parametriceq") !== null, "it has no face");
        verify(BlockPanels.panelFor("advanced.parametriceq") !== null, "it has no panel");
    }

    function test_theBandTypesAreTheOnesTheEngineOffers() {
        const face = createTemporaryObject(faceComponent, null);
        compare(face.bands.plot.paramId(0, "Type"), "band1Type");
        const words = Qt.createQmlObject("import io.github.reverblabr; FilterWords {}", testCase);
        compare(words.bandTypes, BlockCatalogue.describeParam("advanced.parametriceq", "band1Type").choices);
        for (const type of words.bandTypes) {
            verify(words.bandTypeName(type) !== type, type + " has no word");
        }
        words.destroy();
    }

    function test_theFaceDrawsTheResponseAndNamesTheBands() {
        const id = testCase.newBlock();
        const face = createTemporaryObject(faceComponent, null);
        face.blockId = id;
        // A new block's three bells, from the engine's defaults.
        tryVerify(() => face.detail === "Bell 100 Hz 0 dB, Bell 1.00 kHz 0 dB, Bell 8.00 kHz 0 dB", 1000,
                  "the face said: " + face.detail);
        const flat = face.plot.curve;
        verify(flat.length > 20, "the face drew no curve");
        verify(flat.every(point => Math.abs(point.y - flat[0].y) < 1e-6), "a flat equaliser is not a flat line");

        SessionModel.setParam(id, "band2Gain", 12);
        tryVerify(() => face.detail.indexOf("Bell 1.00 kHz +12.0 dB") >= 0, 1000);
        const lifted = face.plot.curve;
        const top = lifted.reduce((best, point) => point.y < best.y ? point : best, lifted[0]);
        verify(Math.abs(face.plot.hzAtX(top.x) - 1000) < 100, "the lift is not at 1 kHz on the face");
    }

    function test_theChipsAreTheBandsLowToHigh() {
        const id = testCase.newBlock();
        SessionModel.setParam(id, "band1Frequency", 5000);
        const window = testCase.openPanel(id);
        const panel = window.panel;
        tryVerify(() => panel.chips.count === 3, 1000);
        compare([0, 1, 2].map(index => panel.chips.itemAt(index).text), ["1.00 kHz", "5.00 kHz", "8.00 kHz"]);
        // The lowest is picked to start with, and a chip picks its band.
        compare(panel.selected, 1);
        panel.chips.itemAt(2).clicked();
        compare(panel.selected, 2);
        verify(panel.chips.itemAt(2).checked && !panel.chips.itemAt(0).checked, "the picked chip is not the checked one");
        tryVerify(() => Math.abs(panel.frequencySlider.value
                                 - panel.words.positionOf(8000, panel.plot.lowestHz, panel.plot.highestHz)) < 1e-6,
                  1000, "the frequency slider does not show the picked band");
    }

    function test_draggingAHandleMovesItsBandAcrossAndUp() {
        const id = testCase.newBlock();
        const window = testCase.openPanel(id);
        const panel = window.panel;
        const from = testCase.handleInWindow(window, 1);

        // Straight up first — the direction a scroll view scrolls, far
        // enough that it would have taken the gesture if it could — then
        // right, in small steps as a hand moves, **with the button held**:
        // a move with no button down is a hover, which nothing takes.
        mousePress(window.contentItem, from.x, from.y);
        for (let step = 1; step <= 5; ++step) {
            mouseMove(window.contentItem, from.x, from.y - step * 8, 10, Qt.LeftButton);
        }
        for (let step = 1; step <= 6; ++step) {
            mouseMove(window.contentItem, from.x + step * 10, from.y - 40, 10, Qt.LeftButton);
        }
        mouseRelease(window.contentItem, from.x + 60, from.y - 40);

        compare(window.flickable.contentY, 0, "the scroll view took the drag");
        const to = panel.plotArea.mapFromItem(window.contentItem, from.x + 60, from.y - 40);
        const hz = SessionModel.param(id, "band2Frequency");
        const db = SessionModel.param(id, "band2Gain");
        verify(Math.abs(Math.log(hz / panel.plot.hzAtX(to.x))) < 0.01,
               "the band is at " + hz + " Hz where the pointer let go at " + panel.plot.hzAtX(to.x));
        verify(Math.abs(db - panel.plot.dbAtY(to.y)) < 0.1,
               "the band's gain is " + db + " where the pointer let go at " + panel.plot.dbAtY(to.y));
        verify(db > 5, "the drag did not lift the band: " + db);
        compare(panel.selected, 1, "the dragged band is not the picked one");
        // The other bands did not move.
        compare(SessionModel.param(id, "band1Frequency") ?? 100, 100);
        compare(SessionModel.param(id, "band3Gain") ?? 0, 0);
    }

    function test_aDragMovesTheHandleByHowFarThePointerMoved() {
        // Found in the real window: a drag straight up moved a band at
        // 1.00 kHz to 998 Hz, because the frequency was read from where the
        // pointer was — a few pixels off the handle's centre — rather than
        // from where the handle was. Pressed off-centre here on purpose.
        const id = testCase.newBlock();
        const window = testCase.openPanel(id);
        const panel = window.panel;
        const centre = testCase.handleInWindow(window, 1);
        const from = Qt.point(centre.x + 4, centre.y + 3);
        mousePress(window.contentItem, from.x, from.y);
        for (let step = 1; step <= 5; ++step) {
            mouseMove(window.contentItem, from.x, from.y - step * 8, 10, Qt.LeftButton);
        }
        mouseRelease(window.contentItem, from.x, from.y - 40);
        compare(SessionModel.param(id, "band2Frequency") ?? 1000, 1000, "a drag straight up moved the band across");
        const lifted = SessionModel.param(id, "band2Gain");
        const expected = panel.plot.dbAtY(panel.plot.yOfDb(0) - 40);
        verify(Math.abs(lifted - expected) < 0.1, "the band rose to " + lifted + " dB, not " + expected);

        // And across only, from off-centre: the band moves by the pointer's
        // distance, not to it.
        const again = testCase.handleInWindow(window, 1);
        const start = Qt.point(again.x - 5, again.y + 2);
        mousePress(window.contentItem, start.x, start.y);
        for (let step = 1; step <= 5; ++step) {
            mouseMove(window.contentItem, start.x + step * 6, start.y, 10, Qt.LeftButton);
        }
        mouseRelease(window.contentItem, start.x + 30, start.y);
        const moved = SessionModel.param(id, "band2Frequency");
        const wanted = panel.plot.hzAtX(panel.plot.xOfHz(1000) + 30);
        verify(Math.abs(Math.log(moved / wanted)) < 0.002, "the band went to " + moved + " Hz, not " + wanted);
        compare(SessionModel.param(id, "band2Gain"), lifted, "a drag straight across changed the gain");
    }

    function test_aPressAwayFromEveryBandTakesNone() {
        const id = testCase.newBlock();
        const window = testCase.openPanel(id);
        const panel = window.panel;
        // Low in the plot, where no handle is.
        const empty = panel.plotArea.mapToItem(window.contentItem, panel.plot.xOfHz(3000), panel.plotArea.height - 4);
        compare(panel.bandAt(panel.plot.xOfHz(3000), panel.plotArea.height - 4), -1);
        mousePress(window.contentItem, empty.x, empty.y);
        mouseMove(window.contentItem, empty.x + 20, empty.y - 20, -1, Qt.LeftButton);
        mouseRelease(window.contentItem, empty.x + 20, empty.y - 20);
        compare(SessionModel.param(id, "band2Frequency") ?? 1000, 1000);
        compare(SessionModel.param(id, "band3Frequency") ?? 8000, 8000);
    }

    function test_theWheelOverABandIsItsWidth() {
        const id = testCase.newBlock();
        const window = testCase.openPanel(id);
        const at = testCase.handleInWindow(window, 2);
        mouseWheel(window.contentItem, at.x, at.y, 0, 120);
        tryVerify(() => (SessionModel.param(id, "band3Q") ?? 1) > 1.05, 1000, "the wheel did not narrow the band");
        verify(Math.abs(SessionModel.param(id, "band3Q") - Math.pow(2, 1 / 6)) < 0.01);
        compare(window.panel.selected, 2);
    }

    function test_aBandIsAddedInTheWidestGapAsOneStep() {
        const id = testCase.newBlock();
        const window = testCase.openPanel(id);
        const panel = window.panel;
        compare(panel.bands.free, 3);
        panel.addButton.clicked();
        tryVerify(() => SessionModel.param(id, "band4Used") === true, 1000, "the band was not added");
        // 100 Hz to 1 kHz is the widest stretch with no band: its middle,
        // in octaves, is 316 Hz.
        compare(SessionModel.param(id, "band4Frequency"), 316);
        compare(panel.selected, 3);
        tryVerify(() => panel.chips.count === 4, 1000);

        // **One step back** takes the whole band away, not one setting of it.
        SessionModel.undo();
        tryVerify(() => SessionModel.param(id, "band4Used") !== true, 1000, "one undo did not take the band away");
        compare(SessionModel.param(id, "band4Frequency") ?? 250, 250, "one undo left the new band's frequency behind");
        tryVerify(() => panel.chips.count === 3, 1000);
    }

    function test_aDeletedBandHandsThePickToTheNearest() {
        const id = testCase.newBlock();
        const window = testCase.openPanel(id);
        const panel = window.panel;
        panel.chips.itemAt(2).clicked();
        compare(panel.selected, 2);
        panel.deleteButton.clicked();
        tryVerify(() => SessionModel.param(id, "band3Used") === false, 1000);
        tryVerify(() => panel.selected === 1, 1000, "the pick went to " + panel.selected + ", not the 1 kHz band");
        compare(panel.chips.count, 2);
    }

    function test_tenBandsAreAllThereIsRoomFor() {
        const id = testCase.newBlock();
        const window = testCase.openPanel(id);
        const panel = window.panel;
        for (let added = 3; added < 10; ++added) {
            verify(panel.addButton.enabled, "Add Band is off with " + added + " bands");
            panel.addButton.clicked();
            tryVerify(() => panel.chips.count === added + 1, 1000);
        }
        verify(!panel.addButton.enabled, "Add Band is still offered with ten bands");
        compare(panel.bands.free, -1);
        // And no two landed on one frequency.
        const frequencies = panel.bands.used.map(band => panel.bands.value(band, "Frequency"));
        compare(new Set(frequencies).size, 10);
    }

    function test_aBandWithNoGainHasNoGainToSetAndSitsOnTheLine() {
        const id = testCase.newBlock();
        SessionModel.setParam(id, "band2Gain", 9);
        const window = testCase.openPanel(id);
        const panel = window.panel;
        panel.chips.itemAt(1).clicked();
        verify(panel.gainSlider.parent.enabled, "a bell has no gain to set");
        panel.typeBox.currentIndex = panel.words.bandTypes.indexOf("notch");
        panel.typeBox.activated(panel.typeBox.currentIndex);
        compare(SessionModel.param(id, "band2Type"), "notch");
        tryVerify(() => !panel.gainSlider.parent.enabled, 1000, "a notch offers a gain");
        compare(panel.handlePoint(1).y, panel.plot.yOfDb(0));
        // Switched off, and back.
        panel.onSwitch.toggle();
        panel.onSwitch.toggled();
        compare(SessionModel.param(id, "band2On"), false);
    }

    // --- the live spectrum behind the curve ---

    /// Bands at the floor, and one at \a loud dB at band \a at.
    function spectrumWithPeak(at: int, loud: real): var {
        const bands = [];
        for (let band = 0; band < 96; ++band) {
            bands.push(band === at ? loud : -90);
        }
        return bands;
    }

    function test_thePanelAsksForItsBlocksSpectrumOnlyWhileItIsShown() {
        const id = testCase.newBlock();
        compare(TestEngine.analysed(testCase.sessionId), "", "somebody's spectrum was asked for already");
        const window = testCase.openPanel(id);
        tryVerify(() => TestEngine.analysed(testCase.sessionId) === id, 1000, "the panel did not ask for its spectrum");
        window.panel.visible = false;
        tryVerify(() => TestEngine.analysed(testCase.sessionId) === "", 1000, "a hidden panel still asked for it");
        window.panel.visible = true;
        tryVerify(() => TestEngine.analysed(testCase.sessionId) === id, 1000);
        window.destroy();
        tryVerify(() => TestEngine.analysed(testCase.sessionId) === "", 1000, "a closed panel kept the copy going");
    }

    function test_theSpectrumIsDrawnBehindTheCurveWhileTheSessionRuns() {
        const id = testCase.newBlock();
        const window = testCase.openPanel(id);
        const panel = window.panel;
        TestEngine.setFakeSpectrum(testCase.sessionId, id, testCase.spectrumWithPeak(60, -12));

        // Stopped: nothing to draw, whatever the mock was given.
        wait(50);
        verify(!panel.spectrumShape.visible, "a spectrum was drawn for a session that is not running");

        TestEngine.start(testCase.sessionId);
        tryVerify(() => panel.spectrumShape.visible, 1000, "the spectrum was never drawn");
        const area = panel.plot.spectrumArea;
        compare(area.length, 98);
        const peak = area[61];
        verify(Math.abs(peak.x - panel.plot.xOfHz(Meters.spectrumBandsHz()[60])) < 1e-6, "the peak is not at its band");
        verify(Math.abs(peak.y - panel.plot.yOfSpectrumDb(-12)) < 1e-6, "the peak is not at its level");
        verify(area[20].y > peak.y + 50, "the floor is not below the peak");

        // Another block's spectrum is not this panel's.
        TestEngine.analyse(testCase.sessionId, "someoneElse", -1);
        tryVerify(() => !panel.spectrumShape.visible, 1000, "another block's spectrum was drawn here");

        TestEngine.stop(testCase.sessionId);
    }
}
