// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Hearing Compensation 16's face and panel: the curves drawn from the
// block's settings, the "now" line following the gain the block applies in
// each band, a band's handle dragged up its curve, Fit from audiogram and its
// undo, the other ear's curve and Copy from the Other Ear, and before/after.

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

    function test_holdingBeforeTurnsTheBlockOffUntilLetGo() {
        const id = testCase.newBlock("");
        const window = testCase.openPanel(id);
        const panel = window.panel;
        verify(SessionModel.block(id).enabled ?? true);
        mousePress(panel.beforeButton);
        tryVerify(() => SessionModel.block(id).enabled === false, 1000, "holding did not take the block out");
        mouseRelease(panel.beforeButton);
        tryVerify(() => SessionModel.block(id).enabled === true, 1000, "letting go did not put it back");
    }
}
