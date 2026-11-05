// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Multiband Compressor's face and panel.

import QtQuick
import QtQuick.Controls
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "MultibandCompressor"
    when: windowShown

    property string sessionId: ""

    readonly property string typeId: "effect.multibandcompressor"

    // A panel in a window, as the settings panel puts it: a drag is a
    // gesture the window system delivers.
    Component {
        id: panelWindow

        Window {
            property alias panel: inner

            width: 380
            height: 900
            visible: true

            ScrollView {
                anchors.fill: parent
                contentWidth: availableWidth

                MultibandCompressorPanel {
                    id: inner

                    width: 360
                }
            }
        }
    }

    function faceFor() {
        const face = createTemporaryObject(BlockFaces.faceFor(testCase.typeId), testCase, { width: 180, height: 110 });
        face.blockId = "mb";
        return face;
    }

    /// Every item under \a item called \a name, depth first.
    function named(item: Item, name: string): var {
        const found = [];
        const walk = function (node) {
            for (let i = 0; i < node.children.length; ++i) {
                const child = node.children[i];
                if (child.objectName === name) {
                    found.push(child);
                }
                walk(child);
            }
        };
        walk(item);
        return found;
    }

    function openPanel() {
        // Watched first, as the window watches its front session: a panel
        // asks for its block's bands of the session the meters watch.
        Meters.watch(testCase.sessionId);
        const window = createTemporaryObject(panelWindow, null);
        window.panel.blockId = "mb";
        waitForRendering(window.panel);
        return window;
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Multiband");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "mb", type: testCase.typeId }, index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParam("mb", "lowCrossover", 200);
        SessionModel.setParam("mb", "highCrossover", 3000);
        for (const band of ["low", "mid", "high"]) {
            SessionModel.setParam("mb", band + "Threshold", -24);
            SessionModel.setParam("mb", band + "Ratio", 3);
        }
    }

    function cleanup() {
        TestEngine.stop(testCase.sessionId);
        TestEngine.setFakeLevel(testCase.sessionId, "mb", 0);
        Meters.watch("");
    }

    function test_theBlockHasAFaceAndAPanelOfItsOwn() {
        verify(BlockPanels.panelFor(testCase.typeId) !== null, "the panel register has it");
        verify(BlockFaces.faceFor(testCase.typeId) !== null, "the face register has it");
        compare(BlockPresentation.name(testCase.typeId), "Multiband Compressor");
    }

    function test_theWordsNameTheEnginesParameters() {
        // Every id the panel and the face build from a band's name is one
        // the engine has.
        const words = createTemporaryQmlObject("import io.github.reverblabr; MultibandWords {}", testCase);
        for (const band of words.bandIds) {
            for (const field of ["Threshold", "Ratio", "Attack", "Release", "Makeup"]) {
                compare(BlockCatalogue.describeParam(testCase.typeId, band + field).id, band + field);
            }
        }
        compare(BlockCatalogue.describeParam(testCase.typeId, "lowCrossover").default, 200);
        compare(BlockCatalogue.describeParam(testCase.typeId, "highCrossover").default, 3000);
        compare(words.rangeText(0, 200, 3000), "Below 200 Hz");
        compare(words.rangeText(1, 200, 3000), "200 Hz to 3.00 kHz");
        compare(words.rangeText(2, 200, 3000), "Above 3.00 kHz");
        compare(words.reductionText(-4.46), "-4.5 dB");
        compare(words.reductionText(0.2), "0 dB");
    }

    function test_theFaceSaysWhereTheBandsMeet() {
        const face = testCase.faceFor();
        compare(face.captionLabel.text, "200 Hz · 3.00 kHz");
        SessionModel.setParam("mb", "lowCrossover", 150);
        tryCompare(face.captionLabel, "text", "150 Hz · 3.00 kHz");
        verify(face.detail.includes("Mid, 150 Hz to 3.00 kHz: from -24 dB at 3:1"), face.detail);
        SessionModel.setParam("mb", "highRatio", 1);
        tryVerify(() => face.detail.includes("High, Above 3.00 kHz: left alone"), 1000, face.detail);
    }

    function test_theFaceShowsEachBandHoldingBackWhileItRuns() {
        const face = testCase.faceFor();
        compare(face.reductions, [0, 0, 0]);
        // Only the middle band set to work: a made-up level at -6 dBFS
        // through -24 dB at 3:1 is 12 dB held back.
        SessionModel.setParam("mb", "lowRatio", 1);
        SessionModel.setParam("mb", "highRatio", 1);
        Meters.watch(testCase.sessionId);
        TestEngine.setFakeLevel(testCase.sessionId, "mb", 0.5);
        TestEngine.start(testCase.sessionId);
        tryVerify(() => face.active, 1000, "the face never went live");
        tryVerify(() => Math.abs(face.reductions[1] + 12) < 0.3, 1000, "the middle band read " + face.reductions[1]);
        compare(face.reductions[0], 0);
        compare(face.reductions[2], 0);
        const bars = testCase.named(face.bars, "reductionBar");
        compare(bars.length, 3);
        verify(bars[1].height > 0, "the middle band's bar is empty");
        compare(bars[0].height, 0);
        compare(bars[2].height, 0);
    }

    function test_pressingABandPicksItAndItsControlsFollow() {
        const window = testCase.openPanel();
        const panel = window.panel;
        compare(panel.band, 1);
        // Well inside the low band, nowhere near a split.
        const low = panel.strip.mapToItem(window.contentItem, panel.xOfHz(40), panel.strip.height / 2);
        mouseClick(window.contentItem, low.x, low.y);
        compare(panel.band, 0);
        compare(panel.curveParams.threshold, -24);
        SessionModel.setParam("mb", "lowThreshold", -40);
        tryCompare(panel.curveParams, "threshold", -40);
        // The rows are the low band's now, then the shared ones.
        const labels = [];
        for (let i = 0; i < panel.bandRows.children.length; ++i) {
            const row = panel.bandRows.children[i];
            if (row.paramId !== undefined) {
                labels.push(row.paramId);
            }
        }
        compare(labels, ["lowThreshold", "lowRatio", "lowAttack", "lowRelease", "lowMakeup", "knee", "lowCrossover",
                         "highCrossover"]);
        // The curve is the band's: a steady level where dsp::outputDb says.
        compare(panel.curve.outputDb(-10), -40 + 30 / 3);
    }

    function test_aSplitFollowsThePointerAndStopsShortOfTheOther() {
        const window = testCase.openPanel();
        const panel = window.panel;
        const from = panel.strip.mapToItem(window.contentItem, panel.xOfHz(3000), panel.strip.height / 2);
        mousePress(window.contentItem, from.x, from.y);
        compare(panel.dragging, "highCrossover");
        const to = panel.strip.mapToItem(window.contentItem, panel.xOfHz(6000), panel.strip.height / 2);
        mouseMove(window.contentItem, (from.x + to.x) / 2, from.y);
        mouseMove(window.contentItem, to.x, to.y);
        mouseRelease(window.contentItem, to.x, to.y);
        compare(panel.dragging, "");
        verify(Math.abs(SessionModel.param("mb", "highCrossover") - 6000) < 150,
               "the split went to " + SessionModel.param("mb", "highCrossover"));

        // Dragged past the other split: it stops 1.5 times above it — with
        // the low split at 400 Hz, where that is above the high split's own
        // floor of 500 Hz.
        SessionModel.setParam("mb", "lowCrossover", 400);
        tryCompare(panel, "lowSplit", 400);
        const high = panel.strip.mapToItem(window.contentItem, panel.xOfHz(6000), panel.strip.height / 2);
        const under = panel.strip.mapToItem(window.contentItem, panel.xOfHz(100), panel.strip.height / 2);
        mousePress(window.contentItem, high.x, high.y);
        mouseMove(window.contentItem, (high.x + under.x) / 2, high.y);
        mouseMove(window.contentItem, under.x, under.y);
        mouseRelease(window.contentItem, under.x, under.y);
        compare(SessionModel.param("mb", "highCrossover"), 600);
    }

    function test_thePanelAsksForTheBandsWhileShownAndDrawsWhereTheSoundIs() {
        const window = testCase.openPanel();
        const panel = window.panel;
        tryVerify(() => TestEngine.analysed(testCase.sessionId) === "mb", 1000, "the panel did not ask");
        const marker = testCase.named(panel, "liveMarker")[0];
        verify(!marker.visible, "a marker with nothing running");
        // Three values a band: the level on the curve, what comes out, what
        // is taken off.
        TestEngine.setFakeBandValues(testCase.sessionId, "mb", [-30, -30, 0, -12, -16, -4, -50, -50, 0]);
        Meters.watch(testCase.sessionId);
        TestEngine.setFakeLevel(testCase.sessionId, "mb", 0.25);
        TestEngine.start(testCase.sessionId);
        tryVerify(() => panel.liveCurve, 1000, "the panel never had the bands' levels");
        compare(panel.bandInputDb, -12);
        verify(marker.visible, "no marker while the band's level is known");
        panel.band = 2;
        compare(panel.bandInputDb, -50);
        window.destroy();
        tryVerify(() => TestEngine.analysed(testCase.sessionId) === "", 1000, "a closed panel kept asking");
    }
}
