// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Hearing Compensation 16's face and panel: the curves drawn from the
// block's settings, the "now" line following the gain the block applies in
// each band, a band's handle dragged up its curve, Fit from audiogram and its
// undo, the other ear's curve and Copy from the Other Ear, and before/after;
// Speed and Measured in in words, each audiogram list named by its
// frequency and ear, and a fit that keeps under Most gain.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "HearingCompensation"
    when: windowShown

    property string sessionId: ""
    property int made: 0

    Component {
        id: faceComponent

        HearingCompensationFace {
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

            width: 500
            height: 420
            visible: true

            Flickable {
                id: scroller

                anchors.fill: parent
                contentWidth: width
                contentHeight: inner.implicitHeight + 200

                HearingCompensationPanel {
                    id: inner

                    width: 460
                }
            }
        }
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Hearing Compensation");
        verify(testCase.sessionId !== "", "the mock opened a session");
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function cleanup() {
        Meters.watch("");
    }

    function newBlock(ear: string): string {
        testCase.made += 1;
        const id = "hc" + testCase.made;
        const block = { id: id, type: "hearing.compensation16" };
        if (ear !== "") {
            block.params = { ear: ear };
        }
        TestEngine.applyEdit(testCase.sessionId, JSON.stringify({ op: "addBlock", block: block, index: -1 }));
        tryVerify(() => SessionModel.blockType(id) === "hearing.compensation16", 1000, "the block was not added");
        return id;
    }

    function openPanel(blockId: string): var {
        Meters.watch(testCase.sessionId);
        const window = createTemporaryObject(shownPanel, null);
        window.panel.blockId = blockId;
        tryVerify(() => window.panel.plotArea.width > 0 && window.panel.averageLine.path.length === 16, 2000,
                  "the plot was never laid out");
        return window;
    }

    /// Band values as the block publishes them: sixteen gains, then sixteen
    /// levels, with \a gainDb in \a band and none elsewhere.
    function valuesWithGainIn(band: int, gainDb: real): var {
        const values = [];
        for (let index = 0; index < 16; ++index) {
            values.push(index === band ? gainDb : 0);
        }
        for (let index = 0; index < 16; ++index) {
            values.push(-40);
        }
        return values;
    }

    function test_itIsAHearingBlockWithAFaceAndAPanelOfItsOwn() {
        verify(BlockCatalogue.types.indexOf("hearing.compensation16") >= 0, "the engine does not offer it");
        compare(BlockPresentation.category("hearing.compensation16"), "hearing");
        compare(BlockPresentation.name("hearing.compensation16"), "Hearing Compensation 16");
        verify(BlockFaces.faceFor("hearing.compensation16") !== null, "it has no face");
        verify(BlockPanels.panelFor("hearing.compensation16") !== null, "it has no panel");
        // The panel's numbers are the block's: its ranges and defaults.
        const soft = BlockCatalogue.describeParam("hearing.compensation16", "band0SoftGainDb");
        compare(soft.minimum, -30);
        compare(soft.maximum, 60);
        compare(BlockCatalogue.describeParam("hearing.compensation16", "band15CeilingDb").default, 100);
    }

    function test_theFaceDrawsTheLiftForAverageSound() {
        const id = testCase.newBlock("");
        const face = createTemporaryObject(faceComponent, testCase, { blockId: id });
        tryVerify(() => face.averageLine.path.length === 16, 1000);
        SessionModel.setParams(id, [["band5AverageGainDb", 20], ["band5SoftGainDb", 30]]);
        tryVerify(() => face.averageLine.path[5].y < face.averageLine.path[0].y - 5, 1000,
                  "a band lifted 20 dB is not drawn higher");
        verify(face.softLine.path[5].y < face.averageLine.path[5].y, "soft is lifted more than average");
    }

    function test_thePanelAsksForItsOwnEar() {
        const id = testCase.newBlock("right");
        const window = testCase.openPanel(id);
        tryVerify(() => TestEngine.analysed(testCase.sessionId) === id, 1000, "the panel did not ask");
        compare(TestEngine.analysedChannel(testCase.sessionId), 1, "a right-ear block asked for the wrong channel");
        window.destroy();
        tryVerify(() => TestEngine.analysed(testCase.sessionId) === "", 1000, "a closed panel kept asking");
    }

    function test_theNowLineFollowsAToneSweptAcrossTheBands() {
        const id = testCase.newBlock("");
        const window = testCase.openPanel(id);
        const panel = window.panel;
        TestEngine.start(testCase.sessionId);
        for (const band of [0, 3, 7, 11, 15]) {
            TestEngine.setFakeBandValues(testCase.sessionId, id, testCase.valuesWithGainIn(band, 20));
            tryVerify(() => panel.nowLine.path.length === 16, 1000, "no now line while the session runs");
            tryVerify(() => {
                const path = panel.nowLine.path;
                let highest = 0;
                for (let index = 1; index < path.length; ++index) {
                    if (path[index].y < path[highest].y) {
                        highest = index;
                    }
                }
                return highest === band;
            }, 1000, "the now line does not peak at band " + band);
            // At the band's own frequency, at the band's own gain.
            const peak = panel.nowLine.path[band];
            verify(Math.abs(peak.x - panel.xOfHz(panel.bands.centresHz[band])) < 1e-6);
            verify(Math.abs(peak.y - panel.yOfGain(20)) < 1e-6);
        }
        TestEngine.stop(testCase.sessionId);
        tryVerify(() => panel.nowLine.path.length === 0, 1000, "the now line stayed after the session stopped");
    }

    function test_draggingAHandleUpLiftsItsBandOnTheCurveBeingEdited() {
        const id = testCase.newBlock("");
        const window = testCase.openPanel(id);
        const panel = window.panel;
        panel.editing = "Soft";
        const point = panel.handlePoint(6);
        const from = panel.plotArea.mapToItem(window.contentItem, point.x, point.y);
        mousePress(window.contentItem, from.x, from.y);
        for (let step = 1; step <= 5; ++step) {
            mouseMove(window.contentItem, from.x, from.y - step * 8, 10, Qt.LeftButton);
        }
        mouseRelease(window.contentItem, from.x, from.y - 40);
        compare(window.flickable.contentY, 0, "the scroll view took the drag");
        const expected = Math.round(panel.gainAtY(panel.yOfGain(0) - 40) * 10) / 10;
        tryVerify(() => Math.abs((SessionModel.param(id, "band6SoftGainDb") ?? 0) - expected) < 0.11, 1000,
                  "band 6's soft gain is " + SessionModel.param(id, "band6SoftGainDb") + ", not " + expected);
        compare(SessionModel.param(id, "band6AverageGainDb"), undefined, "the drag moved another curve");
        compare(panel.selected, 6, "the band dragged is the one inspected");
    }

    function test_theInspectorDrawsTheBandsCurveWithADotWhereItIsNow() {
        const id = testCase.newBlock("");
        SessionModel.setParams(id, [["band5SoftGainDb", 30], ["band5AverageGainDb", 20], ["band5LoudGainDb", 10]]);
        tryVerify(() => SessionModel.param(id, "band5LoudGainDb") === 10, 1000);
        const window = testCase.openPanel(id);
        const panel = window.panel;
        panel.selected = 5;
        tryVerify(() => panel.bandCurve.path.length > 80, 1000, "the band's curve was not drawn");
        // The curve is the block's law: at -40 dBFS, 20 dB of gain.
        const area = panel.bandCurveArea;
        const at = panel.bandCurve.path.find(point => Math.abs(point.x - area.xOf(-40)) < 1e-6);
        verify(at !== undefined, "no point at -40 dBFS");
        verify(Math.abs(at.y - area.yOf(-20)) < 1e-6, "the curve at -40 dBFS is not 20 dB up");

        // Live: the dot where the band is now, on the curve.
        TestEngine.start(testCase.sessionId);
        const values = [];
        for (let band = 0; band < 16; ++band) {
            values.push(band === 5 ? 20 : 0);
        }
        for (let band = 0; band < 16; ++band) {
            values.push(band === 5 ? -40 : -120);
        }
        TestEngine.setFakeBandValues(testCase.sessionId, id, values);
        tryVerify(() => panel.bandCurveDot.visible, 1000, "no dot while the session runs");
        verify(Math.abs(panel.bandCurveDot.x + panel.bandCurveDot.width / 2 - at.x) < 1e-6);
        verify(Math.abs(panel.bandCurveDot.y + panel.bandCurveDot.height / 2 - at.y) < 1e-6);
        TestEngine.stop(testCase.sessionId);
        tryVerify(() => !panel.bandCurveDot.visible, 1000, "the dot stayed after the session stopped");
    }

    function test_fitFromAudiogramSetsEveryBandAndOneUndoTakesItBack() {
        const id = testCase.newBlock("");
        const window = testCase.openPanel(id);
        const panel = window.panel;
        SessionModel.setParams(id, [
            ["audiogram500", "heard"], ["audiogram500Db", 30],
            ["audiogram2000", "heard"], ["audiogram2000Db", 50],
            ["audiogram8000", "noResponse"], ["audiogram8000Db", 90]
        ]);
        tryVerify(() => SessionModel.param(id, "audiogram8000") === "noResponse", 1000);
        const before = SessionModel.params(id);

        panel.audiogramSection.expanded = true;
        tryVerify(() => panel.fitButton.visible && panel.fitButton.width > 0, 1000, "the audiogram did not open");
        mouseClick(panel.fitButton);
        tryVerify(() => SessionModel.param(id, "band15AverageGainDb") !== undefined, 1000, "the fit set nothing");
        let set = 0;
        for (let band = 0; band < 16; ++band) {
            for (const which of panel.bands.settings) {
                set += SessionModel.param(id, panel.bands.paramId(band, which)) !== undefined ? 1 : 0;
            }
        }
        compare(set, 64, "every band setting was fitted");
        verify(panel.unhelped.length > 0, "the band with no response was not marked");
        verify(panel.fitMessage.visible, "nothing points to Frequency Lowering");

        SessionModel.undo();
        tryVerify(() => SessionModel.param(id, "band15AverageGainDb") === undefined, 1000, "one undo left the fit");
        compare(JSON.stringify(SessionModel.params(id)), JSON.stringify(before));
    }

    /// The ParamRow for \a paramId in \a item, or null.
    function rowFor(item: Item, paramId: string): var {
        for (let index = 0; index < item.children.length; ++index) {
            const child = item.children[index];
            if (child.descriptor !== undefined && child.paramId === paramId) {
                return child;
            }
            const inner = testCase.rowFor(child, paramId);
            if (inner !== null) {
                return inner;
            }
        }
        return null;
    }

    function test_speedAndUnitAreInWordsNotIds() {
        const id = testCase.newBlock("");
        const window = testCase.openPanel(id);
        const panel = window.panel;
        panel.audiogramSection.expanded = true;
        const speed = testCase.rowFor(panel, "speed");
        verify(speed !== null, "no Speed in the panel");
        compare(speed.activeControl.model, ["Syllabic", "Slow", "Adaptive"]);
        compare(speed.activeControl.currentText, "Adaptive");
        const unit = testCase.rowFor(panel, "audiogramUnit");
        verify(unit !== null, "no Measured in in the panel");
        compare(unit.activeControl.model, ["dB HL", "dBFS"]);
        compare(unit.activeControl.currentText, "dB HL");
    }

    /// The audiogram's lists, one a frequency.
    function markLists(panel: var): var {
        const lists = [];
        const walk = item => {
            for (let index = 0; index < item.children.length; ++index) {
                const child = item.children[index];
                if (child.paramId !== undefined && /^audiogram[0-9]+$/.test(child.paramId)) {
                    lists.push(child);
                } else {
                    walk(child);
                }
            }
        };
        walk(panel.audiogramSection);
        return lists;
    }

    function test_eachAudiogramListIsNamedByItsFrequencyAndEar() {
        const id = testCase.newBlock("");
        const window = testCase.openPanel(id);
        const panel = window.panel;
        panel.audiogramSection.expanded = true;
        const lists = testCase.markLists(panel);
        compare(lists.length, 11);
        compare(lists[0].model, ["—", "Heard", "No response"]);
        compare(lists[0].Accessible.name, "Audiogram at 125 Hz, left ear");
        compare(lists[4].Accessible.name, "Audiogram at 1.0 kHz, left ear");
        compare(lists[10].Accessible.name, "Audiogram at 8.0 kHz, left ear");
        const names = lists.map(list => list.Accessible.name);
        compare(new Set(names).size, 11, "two lists share a name");

        SessionModel.setParam(id, "ear", "right");
        tryCompare(lists[4].Accessible, "name", "Audiogram at 1.0 kHz, right ear");
    }

    function test_theFitGivesNoBandMoreThanMostGain() {
        const id = testCase.newBlock("");
        const window = testCase.openPanel(id);
        const panel = window.panel;
        // A deep loss everywhere asks for more than 30 dB in every band.
        const changes = [["maximumGainDb", 30]];
        for (const hz of panel.audiogramHz) {
            changes.push(["audiogram" + hz, "heard"], ["audiogram" + hz + "Db", 90]);
        }
        SessionModel.setParams(id, changes);
        tryVerify(() => SessionModel.param(id, "audiogram8000Db") === 90, 1000);

        panel.audiogramSection.expanded = true;
        tryVerify(() => panel.fitButton.visible && panel.fitButton.width > 0, 1000, "the audiogram did not open");
        mouseClick(panel.fitButton);
        tryVerify(() => SessionModel.param(id, "band8SoftGainDb") !== undefined, 1000, "the fit set nothing");
        let most = 0;
        for (let band = 0; band < 16; ++band) {
            for (const which of ["Soft", "Average", "Loud"]) {
                most = Math.max(most, SessionModel.param(id, panel.bands.paramId(band, which)));
            }
        }
        compare(most, 30, "the fit went past Most gain");
        // The handles are drawn where the settings are: none above 30 dB.
        panel.editing = "Soft";
        for (let band = 0; band < 16; ++band) {
            verify(panel.handlePoint(band).y >= panel.yOfGain(30) - 0.5, "band " + band + " is drawn above Most gain");
        }
        compare(panel.unhelped.length, 16, "the bands that asked for more are not marked");
    }

    /// Takes out the blocks earlier tests left, so "the other ear" is the
    /// one a test made.
    function clearSession() {
        const ids = SessionModel.blockIds.slice();
        if (ids.length > 0) {
            SessionModel.removeBlocks(ids);
            tryVerify(() => SessionModel.blockIds.length === 0, 1000, "the session was not cleared");
        }
    }

    function test_theOtherEarIsDrawnFaintlyAndCanBeCopied() {
        testCase.clearSession();
        const left = testCase.newBlock("left");
        const right = testCase.newBlock("right");
        SessionModel.setParams(left, [["band4AverageGainDb", 18], ["band9LoudGainDb", -6], ["band12CeilingDb", 80]]);
        tryVerify(() => SessionModel.param(left, "band12CeilingDb") === 80, 1000);
        const window = testCase.openPanel(right);
        const panel = window.panel;
        compare(panel.otherBlock, left, "the left ear's block was not found");
        tryVerify(() => panel.otherLine.path.length === 16, 1000, "the other ear's curve is not drawn");
        verify(panel.otherLine.path[4].y < panel.averageLine.path[4].y, "the other ear's lift is not drawn");
        verify(panel.copyButton.enabled);

        mouseClick(panel.copyButton);
        tryVerify(() => SessionModel.param(right, "band4AverageGainDb") === 18, 1000, "nothing was copied");
        compare(SessionModel.param(right, "band9LoudGainDb"), -6);
        compare(SessionModel.param(right, "band12CeilingDb"), 80);
        compare(SessionModel.param(right, "ear"), "right", "copying moved the block to the other ear");
        // One step.
        SessionModel.undo();
        tryVerify(() => SessionModel.param(right, "band4AverageGainDb") === undefined, 1000);
    }

    function test_holdingBeforeHoldsTheBlockOffAndIsNoEdit() {
        const id = testCase.newBlock("");
        const window = testCase.openPanel(id);
        const panel = window.panel;
        SessionModel.setParam(id, "loudnessDb", 3);
        const undoText = SessionModel.undoText;
        const redoText = SessionModel.redoText;
        TestEngine.start(testCase.sessionId);
        tryVerify(() => Meters.live, 1000, "the session did not start");

        mousePress(panel.beforeButton);
        tryVerify(() => TestEngine.heldOff(testCase.sessionId, id), 1000, "holding did not take the block out");
        verify(SessionModel.block(id).enabled ?? true, "the session heard of the hold");
        compare(SessionModel.undoText, undoText);
        compare(SessionModel.redoText, redoText);
        mouseRelease(panel.beforeButton);
        tryVerify(() => !TestEngine.heldOff(testCase.sessionId, id), 1000, "letting go did not put it back");
        compare(SessionModel.undoText, undoText);
        compare(SessionModel.redoText, redoText);
        TestEngine.stop(testCase.sessionId);
    }

    // --- the numbers round the plots ---

    /// Each number on \a axes sits where these formulas, written apart from
    /// the panel, put its value.
    function checkTicks(axes: var, x: var, y: var) {
        verify(axes.xNumbers.count > 0, "no numbers across");
        verify(axes.yNumbers.count > 0, "no numbers up");
        for (let tick = 0; tick < axes.xNumbers.count; ++tick) {
            const number = axes.xNumbers.itemAt(tick);
            const at = x(number.modelData);
            verify(Math.abs(number.x - axes.plot.x - at) <= 0.51, number.modelData + " is at " + (number.x - axes.plot.x) + ", not " + at);
        }
        for (let tick = 0; tick < axes.yNumbers.count; ++tick) {
            const number = axes.yNumbers.itemAt(tick);
            const at = y(number.modelData);
            verify(Math.abs(number.y - axes.plot.y - at) <= 0.51, number.modelData + " is at " + (number.y - axes.plot.y) + ", not " + at);
        }
    }

    function checkPlot(panel: var) {
        const plot = panel.plotArea;
        const xOfHz = hz => plot.width * Math.log(hz / 80) / Math.log(18000 / 80);
        if (panel.view === "gain") {
            testCase.checkTicks(panel.plotAxes, xOfHz, db => plot.height * (60 - db) / 80);
        } else {
            testCase.checkTicks(panel.plotAxes, xOfHz, dbfs => plot.height * -dbfs / 100);
        }
    }

    function test_everyAxisHasNumbersAndAUnit() {
        const window = testCase.openPanel(testCase.newBlock(""));
        const panel = window.panel;
        const axes = panel.plotAxes;
        compare(axes.xTitleLabel.text, "Frequency, Hz");
        compare(axes.yTitleLabel.text, "Gain, dB");
        compare(axes.shownX, [125, 250, 500, 1000, 2000, 4000, 8000, 16000]);
        compare(axes.xNumbers.itemAt(3).children[1].text, "1k");
        compare(axes.xNumbers.itemAt(7).children[1].text, "16k");
        compare(axes.shownY, [60, 40, 20, 0, -20]);
        compare(axes.yNumbers.itemAt(0).children[1].text, "+60");
        compare(axes.yNumbers.itemAt(4).children[1].text, "−20");
        compare(axes.heavyY, 0);
        testCase.checkPlot(panel);

        panel.view = "levels";
        compare(axes.yTitleLabel.text, "Level, dBFS");
        compare(axes.shownY, [0, -20, -40, -60, -80, -100]);
        compare(axes.heavyY, null);
        testCase.checkPlot(panel);

        panel.selected = 3;
        const curve = panel.bandCurveAxes;
        tryVerify(() => curve.plot.visible && curve.xNumbers.count === 4, 1000, "the band's curve has no numbers");
        compare(curve.xTitleLabel.text, "In, dBFS");
        compare(curve.yTitleLabel.text, "Out, dBFS");
        const side = curve.plot.width;
        testCase.checkTicks(curve, dbfs => side * (dbfs + 90) / 90, dbfs => side * -dbfs / 90);
    }

    function test_theGutterDoesNotMoveThePlotBetweenViews() {
        const panel = testCase.openPanel(testCase.newBlock("")).panel;
        const x = panel.plotArea.x;
        const width = panel.plotArea.width;
        panel.view = "levels";
        compare(panel.plotArea.x, x);
        compare(panel.plotArea.width, width);
    }

    function test_ticksAndHandlesStayTogetherAsThePlotIsResized() {
        const window = testCase.openPanel(testCase.newBlock(""));
        const panel = window.panel;
        const widths = [460, 300, 620];
        for (const width of widths) {
            panel.width = width;
            tryVerify(() => Math.abs(panel.plotArea.width - (width - panel.plotAxes.leftGutter - panel.plotAxes.rightGutter)) < 1, 1000);
            testCase.checkPlot(panel);
            // A handle at a band's centre, against the number nearest it.
            const handle = panel.handles.itemAt(7);
            fuzzyCompare(handle.x + handle.width / 2, panel.xOfHz(panel.bands.centresHz[7]), 0.01);
            verify(panel.plotAxes.width <= panel.width, "the axes run past the card");
            verify(panel.plotArea.x + panel.plotArea.width + panel.plotAxes.rightGutter <= panel.width + 0.5, "past the card's edge");
        }
    }

    // Alone, at a width no card comes down to: a card's rows stop shrinking
    // first, and how far depends on the style.
    Component {
        id: narrowAxes

        Item {
            property alias axes: axes

            width: 130
            height: 90

            PlotAxes {
                id: axes

                anchors.fill: parent
                plot: narrowPlot
                xOf: hz => narrowPlot.width * Math.log(hz / 80) / Math.log(18000 / 80)
                yOf: db => narrowPlot.height * (60 - db) / 80
                xText: hz => String(hz)
                yText: db => String(db)
                xTicks: [125, 250, 500, 1000, 2000, 4000, 8000, 16000]
                yTicks: [-20, 0, 20, 40, 60]
                widestYText: "-100"
            }

            Item {
                id: narrowPlot

                x: axes.leftGutter
                y: axes.topGutter
                width: parent.width - axes.leftGutter - axes.rightGutter
                height: parent.height - axes.topGutter - axes.bottomGutter
            }
        }
    }

    function test_noLabelsOverlapAtLargeText() {
        const axes = createTemporaryObject(narrowAxes, testCase).axes;
        tryVerify(() => axes.xNumbers.count > 1, 1000, "too few numbers kept to read a scale");
        verify(axes.xNumbers.count < 8, "every number kept at a width they cannot fit");
        for (let tick = 1; tick < axes.xNumbers.count; ++tick) {
            const before = axes.xNumbers.itemAt(tick - 1).children[1];
            const after = axes.xNumbers.itemAt(tick).children[1];
            verify(after.mapToItem(axes, 0, 0).x >= before.mapToItem(axes, before.width, 0).x, "two numbers touch");
        }
        verify(axes.yNumbers.count < 5, "every number kept at a height they cannot fit");
        for (let tick = 1; tick < axes.yNumbers.count; ++tick) {
            const before = axes.yNumbers.itemAt(tick - 1).children[1];
            const after = axes.yNumbers.itemAt(tick).children[1];
            verify(after.mapToItem(axes, 0, 0).y >= before.mapToItem(axes, 0, before.height).y - 1, "two numbers touch");
        }
    }

    // --- the levels view ---

    function test_aCeilingAboveFullScaleIsSaidNotPinnedToTheTop() {
        const window = testCase.openPanel(testCase.newBlock(""));
        const panel = window.panel;
        panel.view = "levels";
        // A new block's ceiling, 100 on the listening scale, is +15 dBFS.
        compare(panel.ceilingsOffScale, 16);
        compare(panel.ceilingPath, "", "a line was drawn for a level this scale does not have");
        const note = panel.ceilingNote;
        tryVerify(() => note.visible, 1000, "the card says nothing about the ceilings");
        compare(note.words, "Ceiling above 0 dBFS: no limit");
        verify(note.y > 0, "the words sit on the plot's edge");
        verify(note.x >= 0 && note.x + note.width <= panel.plotArea.width, "the words run off the plot");
        verify(note.y + note.height <= panel.plotArea.height, "the words run off the plot");
        verify(note.width > 0 && note.height > 0, "the words have no size");
    }

    function test_aCeilingOnTheScaleIsDrawnAndLabelled() {
        const id = testCase.newBlock("");
        const window = testCase.openPanel(id);
        const panel = window.panel;
        panel.view = "levels";
        // Two neighbours, one band alone, one exactly at full scale; the
        // rest over the top.
        SessionModel.setParams(id, [["band3CeilingDb", 65], ["band4CeilingDb", 60], ["band9CeilingDb", 70],
                                    ["band14CeilingDb", 85]]);
        tryVerify(() => panel.ceilingsOffScale === 12, 1000);
        compare(panel.ceilingRuns.length, 3);
        compare(panel.ceilingRuns[0].length, 2);
        fuzzyCompare(panel.ceilingRuns[0][0].y, panel.yOfLevel(-20), 1e-6);
        fuzzyCompare(panel.ceilingRuns[0][1].y, panel.yOfLevel(-25), 1e-6);
        compare(panel.ceilingRuns[2][0].y, 1, "a line at 0 dBFS is half cut off by the plot's edge");
        verify(panel.ceilingPath.startsWith("M "), panel.ceilingPath);
        compare(panel.ceilingPath.split("M").length - 1, 3, "not one stroke a run");
        compare(panel.ceilingNote.words, "Ceiling: no limit in 12 bands");

        const all = [];
        for (let band = 0; band < 16; ++band) {
            all.push(["band" + band + "CeilingDb", 40]);
        }
        SessionModel.setParams(id, all);
        tryVerify(() => panel.ceilingsOffScale === 0, 1000);
        compare(panel.ceilingNote.words, "Ceiling");
        verify(panel.ceilingNote.y + panel.ceilingNote.height <= panel.plotArea.height,
               "the label left the plot under a line at the bottom");
    }

    function test_theLevelsViewSaysWhenItHasNoLiveSound() {
        const panel = testCase.openPanel(testCase.newBlock("")).panel;
        verify(!panel.levelsNote.visible, "the gain view has the levels view's words");
        panel.view = "levels";
        verify(panel.levelsNote.visible);
        verify(panel.levelsNote.text.indexOf("drawn while the session runs") >= 0, panel.levelsNote.text);
        verify(panel.levelsNote.text.indexOf("a tenth of an octave") >= 0, panel.levelsNote.text);
        TestEngine.start(testCase.sessionId);
        tryVerify(() => panel.levelsNote.text.indexOf("drawn while the session runs") < 0, 1000, "still says stopped");
        Meters.analyse("another");
        tryVerify(() => panel.levelsNote.text.indexOf("another card has it") >= 0, 1000, "does not say another card has it");
        Meters.analyse(panel.blockId, panel.earChannel);
        TestEngine.stop(testCase.sessionId);
    }

    function test_theLevelsViewDrawsTheAudiogramAndSaysWhatItIs() {
        const id = testCase.newBlock("");
        const panel = testCase.openPanel(id).panel;
        panel.view = "levels";
        compare(panel.thresholdLine.path.length, 0);
        verify(panel.levelsNote.text.indexOf("Dark line") < 0, "words for a line that is not there");

        SessionModel.setParams(id, [["audiogram1000", "heard"], ["audiogram1000Db", 40],
                                    ["audiogram4000", "heard"], ["audiogram4000Db", 60]]);
        tryVerify(() => panel.thresholdLine.path.length === 2, 1000, "the audiogram's line is not drawn");
        const line = panel.thresholdLine.path;
        fuzzyCompare(line[0].x, panel.xOfHz(1000), 1e-6);
        fuzzyCompare(line[0].y, panel.plotArea.height * 45 / 100, 1e-6);
        fuzzyCompare(line[1].y, panel.plotArea.height * 25 / 100, 1e-6);
        verify(panel.levelsNote.text.indexOf("85 dB below what was typed") >= 0, panel.levelsNote.text);

        SessionModel.setParam(id, "audiogramUnit", "dbFs");
        SessionModel.setParam(id, "audiogram1000Db", -40);
        tryVerify(() => panel.audiogramInDbfs && panel.thresholdLine.path[0].y === panel.yOfLevel(-40), 1000,
                  "a level typed in dBFS is not drawn where it reads");
        verify(panel.levelsNote.text.indexOf("Dark line") >= 0, panel.levelsNote.text);
        verify(panel.levelsNote.text.indexOf("85 dB") < 0, "an offset named that was not applied");
    }

    function test_theSoundBehindTheGainsSaysItsOwnScale() {
        const id = testCase.newBlock("");
        const panel = testCase.openPanel(id).panel;
        verify(!panel.soundBehindShape.visible);
        verify(!panel.gainNote.visible, "words for a sound that is not drawn");
        const levels = [];
        for (let band = 0; band < 96; ++band) {
            levels.push(-50);
        }
        TestEngine.setFakeSpectrum(testCase.sessionId, id, levels);
        TestEngine.start(testCase.sessionId);
        tryVerify(() => panel.soundBehindShape.visible, 1000, "the sound is not drawn behind the gains");
        verify(panel.gainNote.visible, "the sound has no scale and nothing says so");
        verify(panel.gainNote.text.indexOf("0 dBFS at the top") >= 0, panel.gainNote.text);
        panel.view = "levels";
        verify(!panel.soundBehindShape.visible);
        verify(!panel.gainNote.visible, "the levels view has an axis for it");
        TestEngine.stop(testCase.sessionId);
    }

    function test_sixteenBandsHaveNoPointsToAdd() {
        const panel = testCase.openPanel(testCase.newBlock("")).panel;
        verify(!panel.addButton.visible);
        verify(panel.addButton.Accessible.ignored, "a screen reader would find it");
        verify(!panel.removeButton.visible);
        verify(!panel.pointsFullNote.visible);
    }
}
