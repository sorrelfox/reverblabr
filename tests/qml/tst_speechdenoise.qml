// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Speech Denoise block's face and panel offscreen, against the mock
// engine: the model on the tile, speech heard or not, and the two settings.
// **Skipped in a build without RNNoise** (ADR-0016): the engine's catalogue
// has no such block there, and nothing offers one.

import QtQuick
import QtQuick.Window
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "SpeechDenoise"
    when: windowShown

    property string sessionId: ""
    readonly property string typeId: "advanced.speechdenoise"

    Component {
        id: panelComponent

        Window {
            property alias panel: inner

            width: 420
            height: 600
            visible: true

            SpeechDenoisePanel {
                id: inner

                width: 380
            }
        }
    }

    function panelFor() {
        const panel = createTemporaryObject(panelComponent, null).panel;
        panel.blockId = "sd";
        return panel;
    }

    function faceFor() {
        const face = createTemporaryObject(BlockFaces.faceFor(testCase.typeId), testCase, { width: 180, height: 90 });
        face.blockId = "sd";
        return face;
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Speech Denoise");
        verify(testCase.sessionId !== "", "the mock opened a session");
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
        if (BlockCatalogue.types.indexOf(testCase.typeId) < 0) {
            return;
        }
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "sd", type: testCase.typeId }, index: -1 }));
    }

    function init() {
        if (BlockCatalogue.types.indexOf(testCase.typeId) < 0) {
            skip("built without RNNoise");
        }
        SessionModel.setParam("sd", "reduction", 40);
        SessionModel.setParam("sd", "mono", false);
    }

    function cleanup() {
        TestEngine.clearFakeLevels(testCase.sessionId);
        TestEngine.stop(testCase.sessionId);
        Meters.watch("");
    }

    function run() {
        Meters.watch(testCase.sessionId);
        TestEngine.start(testCase.sessionId);
        tryVerify(() => Meters.live, 1000, "the session never went live");
    }

    function test_theBlockHasAFaceAndAPanelOfItsOwn() {
        verify(BlockPanels.panelFor(testCase.typeId) !== null, "Speech Denoise is in the panel register");
        verify(BlockFaces.faceFor(testCase.typeId) !== null, "Speech Denoise is in the face register");
    }

    function test_theRangesAreTheEnginesOwn() {
        const panel = testCase.panelFor();
        const reduction = BlockCatalogue.describeParam(testCase.typeId, "reduction");
        compare(panel.highestReduction, reduction.maximum);
        compare(panel.defaultReduction, reduction.default);
        compare(panel.reductionSlider.to, reduction.maximum);
        compare(panel.defaultMono, BlockCatalogue.describeParam(testCase.typeId, "mono").default);
    }

    function test_theTileNamesTheModel() {
        const face = testCase.faceFor();
        compare(face.lineLabel.text, "RNNoise · 48 kHz");
    }

    function test_itSaysWhetherItHearsSpeech() {
        const panel = testCase.panelFor();
        compare(panel.statusLabel.text, "No speech detected");
        testCase.run();
        TestEngine.setFakeLevel(testCase.sessionId, "sd", 0.5);
        tryCompare(panel.statusLabel, "text", "Speech detected");
        TestEngine.clearFakeLevels(testCase.sessionId);
        tryCompare(panel.statusLabel, "text", "No speech detected");
    }

    function test_theSettingsWriteWhatTheyShow() {
        const panel = testCase.panelFor();
        compare(panel.reductionLabel.text, "40.0 dB");
        verify(!panel.monoBox.checked, "mono is on by default");
        panel.reductionSlider.value = 25;
        panel.reductionSlider.moved();
        SessionModel.commitParam("sd", "reduction");
        compare(SessionModel.param("sd", "reduction"), 25);
        compare(panel.reductionLabel.text, "25.0 dB");
        panel.monoBox.toggle();
        panel.monoBox.toggled();
        compare(SessionModel.param("sd", "mono"), true);
    }
}
