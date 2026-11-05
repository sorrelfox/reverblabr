// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Voice Compressor's tile and card offscreen, against the mock engine:
// every state and every word, the curve against the block's table, the voice
// types, and the two controls that move together. What the block does is in
// blocks.VoiceCompressor.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "VoiceCompressor"
    when: windowShown
    visible: true

    property string sessionId: ""

    readonly property var words: VoiceCompressorWords {}

    function panelFor() {
        const panel = createTemporaryObject(BlockPanels.panelFor("effect.voicecompressor"), testCase, { width: 380 });
        panel.blockId = "voice";
        return panel;
    }

    function faceFor() {
        const face = createTemporaryObject(BlockFaces.faceFor("effect.voicecompressor"), testCase, { width: 200, height: 90 });
        face.blockId = "voice";
        return face;
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Voice");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "voice", type: "effect.voicecompressor" }, index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParams("voice", [["compression", -12], ["gate", -80], ["ceiling", -6]]);
        SessionModel.setBlockEnabled("voice", true);
    }

    function cleanup() {
        TestEngine.setFakeBlockValues(testCase.sessionId, "voice", []);
        TestEngine.stop(testCase.sessionId);
        Meters.watch("");
    }

    function play(values) {
        Meters.watch(testCase.sessionId);
        TestEngine.start(testCase.sessionId);
        TestEngine.setFakeBlockValues(testCase.sessionId, "voice", values);
        tryVerify(() => Meters.live, 1000, "the session never went live");
    }

    // --- the numbers written down twice ---

    function test_theNumbersAreTheEngines() {
        const compression = BlockCatalogue.describeParam("effect.voicecompressor", "compression");
        compare(compression.minimum, testCase.words.lowestCompression);
        compare(compression.default, testCase.words.defaultCompression);
        compare(compression.maximum, 0);
        const ceiling = BlockCatalogue.describeParam("effect.voicecompressor", "ceiling");
        compare(ceiling.minimum, testCase.words.lowestCeiling);
        compare(ceiling.default, testCase.words.defaultCeiling);
        const gate = BlockCatalogue.describeParam("effect.voicecompressor", "gate");
        compare(gate.minimum, testCase.words.gateOff);
        compare(gate.default, testCase.words.gateOff);
    }

    function test_theCurveIsTheTable_data() {
        return [
            { tag: "nothing", compression: 0, ceiling: 0, gate: -80, ratio: 2, makeup: 0.9375,
              out: [0, -5.062, -11.062, -17.062, -23.062, -35.062, -47.062, -59.062] },
            { tag: "onAdd", compression: -12, ceiling: -6, gate: -80, ratio: 3.333, makeup: 9.45,
              out: [-6, -6.75, -9.6, -14.55, -20.55, -32.55, -44.55, -56.55] },
            { tag: "pushed", compression: -12, ceiling: 0, gate: -80, ratio: 3.333, makeup: 10.5,
              out: [0, 0, -2.55, -7.5, -13.5, -25.5, -37.5, -49.5] },
            { tag: "firm", compression: -36, ceiling: -12, gate: -80, ratio: 6, makeup: 30,
              out: [-12, -13, -14, -15, -16, -19.25, -30, -42] },
            { tag: "gated", compression: -18, ceiling: -6, gate: -50, ratio: 4, makeup: 15.1875,
              out: [-6, -6, -7.312, -9.938, -14.812, -26.812, -39.479, -60.812] }
        ];
    }

    function test_theCurveIsTheTable(row) {
        fuzzyCompare(testCase.words.ratioAt(row.compression), row.ratio, 0.001);
        fuzzyCompare(testCase.words.makeupDb(row.compression, row.ceiling), row.makeup, 0.001);
        [0, -6, -12, -18, -24, -36, -48, -60].forEach((level, at) => {
            fuzzyCompare(testCase.words.outputDb(level, row.compression, row.ceiling, row.gate), row.out[at], 0.001);
        });
    }

    function test_thePresetsAreTheShippedOnes() {
        compare(testCase.words.presets.map(preset => preset.id), ["soft-voice", "speech", "strong-voice", "singing"]);
        for (const preset of testCase.words.presets) {
            verify(testCase.words.presetName(preset.id) !== "", preset.id);
        }
    }

    // --- the tile: every state ---

    function test_stopped() {
        const face = testCase.faceFor();
        compare(face.caption, "Not playing");
        compare(face.headline, "0 dB");
        compare(face.settingsLine, "Compression -12 dB, ceiling -6 dBFS");
        compare(face.dot, "off");
        compare(face.detail, "Compression -12 dB, ceiling -6 dBFS, Gate off");
        compare(face.tileLine, "-12 dB, ceiling -6");
    }

    function test_switchedOff() {
        SessionModel.setBlockEnabled("voice", false);
        const face = testCase.faceFor();
        testCase.play([6, 0, 0, -18]);
        tryCompare(face, "caption", "Switched off");
        compare(face.headline, "0 dB");
        compare(face.dot, "off");
    }

    function test_noSound() {
        const face = testCase.faceFor();
        testCase.play([0, 0, 0, -120]);
        tryCompare(face, "caption", "no sound");
        compare(face.dot, "ok");
    }

    function test_below() {
        const face = testCase.faceFor();
        testCase.play([0.1, 0, 0, -30]);
        tryCompare(face, "caption", "below compression");
        compare(face.dot, "ok");
    }

    function test_compressing() {
        const face = testCase.faceFor();
        testCase.play([6.2, 0, 0, -6]);
        tryCompare(face, "caption", "compressing");
        compare(face.headline, "-6.2 dB");
        compare(face.dot, "live");
    }

    function test_gating() {
        const face = testCase.faceFor();
        testCase.play([0, 12, 0, -70]);
        tryCompare(face, "caption", "gate lowering");
        compare(face.headline, "-12.0 dB");
        compare(face.dot, "live");
    }

    function test_atTheCeiling() {
        const face = testCase.faceFor();
        testCase.play([4, 1, 2, -6]);
        tryCompare(face, "caption", "at the ceiling");
        compare(face.headline, "-5.0 dB");
        compare(face.dot, "live");
    }

    function test_theTileSaysItsSettingsToAScreenReader() {
        const face = testCase.faceFor();
        compare(face.Accessible.name, "0 dB, Not playing, Compression -12 dB, ceiling -6 dBFS");
    }

    // --- the card ---

    function test_everyStatusLine_data() {
        return [
            { tag: "stopped", kind: "stopped", total: 0, words: "Not playing" },
            { tag: "off", kind: "off", total: 0, words: "Switched off" },
            { tag: "silent", kind: "silent", total: 0, words: "No sound coming in" },
            { tag: "under", kind: "under", total: 0, words: "Below compression, nothing taken off" },
            { tag: "compressing", kind: "compressing", total: 6.2, words: "Taking off 6.2 dB" },
            { tag: "gating", kind: "gating", total: 12, words: "Gate lowering, 12.0 dB off" },
            { tag: "limiting", kind: "limiting", total: 6.2, words: "At the ceiling, compression and gate took off 6.2 dB" }
        ];
    }

    function test_everyStatusLine(row) {
        compare(testCase.words.statusOf(row.kind, row.total), row.words);
    }

    function test_theCardReadsLive() {
        const panel = testCase.panelFor();
        testCase.play([5, 2, 0, -9]);
        tryCompare(panel.statusLabel, "text", "Taking off 7.0 dB");
        compare(panel.totalBar.db, -7);
        compare(panel.inputBar.db, -9);
        verify(panel.graphItem.live);
        tryCompare(panel.mostLabel, "text", "Most taken off 7.0 dB");
        // Down first: a frame still saying 7 would be noted again after Reset.
        testCase.play([1, 0, 0, -9]);
        tryCompare(panel.reading, "totalDb", 1);
        panel.resetButton.clicked();
        tryCompare(panel.mostLabel, "text", "Most taken off 1.0 dB");
    }

    function test_theMostIsCaughtBetweenTheWordsTicks() {
        const panel = testCase.panelFor();
        testCase.play([1, 0, 0, -9]);
        tryCompare(panel.reading, "totalDb", 1);
        // No tick from here on, so only the reading itself can note the peak.
        panel.reading.ticker.running = false;
        panel.reading.resetMost();
        TestEngine.setFakeBlockValues(testCase.sessionId, "voice", [12, 0, 0, -9]);
        tryCompare(panel.reading, "totalDb", 12);
        TestEngine.setFakeBlockValues(testCase.sessionId, "voice", [1, 0, 0, -9]);
        tryCompare(panel.reading, "totalDb", 1);
        compare(panel.reading.mostDb, 12);
    }

    function test_theHelpSaysWhatTheLawDoes() {
        const page = createTemporaryObject(BlockHelp.pageFor("effect.voicecompressor"), null);
        const lift = testCase.words.outputDb(-18, -12, -6, -80) + 18;
        fuzzyCompare(lift, 3.45, 0.001);
        verify(Array.from(page.sections).some(section => Array.from(section.paragraphs).some(
            paragraph => paragraph.indexOf("comes out about 3.5 dB higher") >= 0)));
    }

    function test_theCurveGoesOnFallingUnderThePlot() {
        SessionModel.setParams("voice", [["compression", 0], ["ceiling", -36]]);
        const graph = testCase.panelFor().graphItem;
        tryCompare(graph, "ceiling", -36);
        const points = graph.drawn;
        // The law at -60 in is far under the plot's -60, not along its floor.
        verify(points[0].y > graph.plot.height * 1.4, "the curve lies on the floor at " + points[0].y);
        verify(points[1].y < points[0].y);
        fuzzyCompare(points[points.length - 1].y, graph.yOf(-36), 0.01);
    }

    function test_thePresetIsChosenAndForgotten() {
        const panel = testCase.panelFor();
        compare(panel.chosenPreset, "");
        panel.choosePreset("speech");
        tryCompare(panel, "chosenPreset", "speech");
        compare(panel.reading.gate, -55);
        compare(panel.reading.compression, -14);
        SessionModel.setParam("voice", "ceiling", -7);
        tryCompare(panel, "chosenPreset", "");
    }

    function test_theGateSaysOffAtTheBottom() {
        const panel = testCase.panelFor();
        compare(panel.gateRow.lowestNote.text, "Nothing is lowered");
        compare(panel.gateRow.spoken, "Gate off");
        verify(!panel.gateMarkItem.visible);
        SessionModel.setParam("voice", "gate", -50);
        tryCompare(panel.gateRow, "spoken", "Gate -50 dB");
        verify(panel.gateMarkItem.visible);
        fuzzyCompare(panel.gateMarkItem.x, panel.inputBar.width * 30 / 80, 1);
    }

    function test_apartByDefault() {
        const panel = testCase.panelFor();
        panel.moveCompression(-20, false);
        tryCompare(panel.reading, "compression", -20);
        compare(panel.reading.ceiling, -6);
    }

    function test_theyMoveTogether() {
        const panel = testCase.panelFor();
        panel.linkBox.checked = true;
        panel.moveCompression(-20, false);
        tryCompare(panel.reading, "compression", -20);
        tryCompare(panel.reading, "ceiling", -14);
        panel.moveCeiling(-10, false);
        tryCompare(panel.reading, "ceiling", -10);
        tryCompare(panel.reading, "compression", -16);
    }

    function test_togetherStopsAtTheEndsOfTheRange() {
        const panel = testCase.panelFor();
        panel.linkBox.checked = true;
        panel.moveCeiling(0, false);
        tryCompare(panel.reading, "ceiling", 0);
        compare(panel.reading.compression, -6);
        panel.moveCompression(0, false);
        tryCompare(panel.reading, "compression", 0);
        compare(panel.reading.ceiling, 0);
    }

    function test_theGraphDrawsTheLawWithAxes() {
        const panel = testCase.panelFor();
        const graph = panel.graphItem;
        compare(graph.axes.xTitleLabel.text, "In, dBFS");
        compare(graph.axes.yTitleLabel.text, "Out, dBFS");
        verify(graph.axes.xNumbers.count >= 3);
        compare(graph.curve[0].x, -60);
        fuzzyCompare(graph.curve[graph.curve.length - 1].y, -6, 0.001);
        compare(graph.yOf(0), 0);
        compare(graph.xOf(0), graph.plot.width);
    }

    function test_everyControlHasAName() {
        const panel = testCase.panelFor();
        verify(panel.compressionRow.label !== "");
        verify(panel.gateRow.label !== "");
        verify(panel.ceilingRow.label !== "");
        compare(panel.linkBox.Accessible.name, "Link Compression and Ceiling");
        compare(panel.resetButton.Accessible.name, "Reset the most taken off");
        compare(panel.presetChoices.count, 4);
    }
}
