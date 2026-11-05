// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Ducking block's face and panel: the threshold and the ducked
// volume, each written as the setting it names, the ducked volume shown as
// a percentage and stored as the amplitude it is, and a status that follows
// what the running block says it is doing.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "Ducking"

    property string sessionId: ""

    Component {
        id: faceComponent

        DuckingFace {
            width: 220
            height: 120
        }
    }

    Component {
        id: panelComponent

        DuckingPanel {
            width: 340
        }
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Ducking");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "duck", type: "advanced.ducking" },
                                              index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParam("duck", "threshold", -30);
        SessionModel.setParam("duck", "duckedVolume", 0.4);
        SessionModel.setParam("duck", "attack", 50);
        SessionModel.setParam("duck", "hold", 300);
        SessionModel.setParam("duck", "release", 500);
    }

    function test_itIsInAdvancedWithAFaceAPanelAndTwoNamedInputs() {
        verify(BlockCatalogue.types.indexOf("advanced.ducking") >= 0, "the engine does not offer it");
        compare(BlockPresentation.category("advanced.ducking"), "advanced");
        compare(BlockPresentation.name("advanced.ducking"), "Ducking");
        verify(BlockFaces.faceFor("advanced.ducking") !== null, "it has no face");
        verify(BlockPanels.panelFor("advanced.ducking") !== null, "it has no panel");
        const inputs = BlockCatalogue.describe("advanced.ducking").inputs;
        compare(inputs.laneWidth, 2);
        compare(inputs.lanes, ["main", "background"]);
        compare(BlockPresentation.laneName("main"), "Main");
        compare(BlockPresentation.laneName("background"), "Background");
    }

    function test_theFaceSaysWhereItStartsAndHowFarItGoes() {
        const face = createTemporaryObject(faceComponent, null);
        face.blockId = "duck";
        wait(50);
        compare(face.headlineLabel.text, "-30.0 dB");
        compare(face.captionLabel.text, "duck to 40 %");
        compare(face.slider.value, 40);

        SessionModel.setParam("duck", "duckedVolume", 0.15);
        tryCompare(face.captionLabel, "text", "duck to 15 %");

        // While it is ducking, it says by how much, in the live colour.
        face.published = [-16.5, 1];
        compare(face.captionLabel.text, "ducking -16.5 dB");
        compare(face.captionLabel.color, AppColors.liveText);
        face.published = [0, 0];
        compare(face.captionLabel.text, "duck to 15 %");
    }

    function test_theFacesSliderWritesAnAmplitude() {
        const face = createTemporaryObject(faceComponent, null);
        face.blockId = "duck";
        face.slider.value = 25;
        face.slider.moved();
        SessionModel.commitParam("duck", "duckedVolume");
        compare(SessionModel.param("duck", "duckedVolume"), 0.25);
    }

    function test_thePanelWritesWhatItsControlsSay() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "duck";
        wait(50);
        compare(panel.thresholdReadout.text, "-30.0 dB");
        compare(panel.volumeReadout.text, "40 % · -8.0 dB");

        panel.thresholdSlider.value = -42;
        panel.thresholdSlider.moved();
        SessionModel.commitParam("duck", "threshold");
        compare(SessionModel.param("duck", "threshold"), -42);

        panel.volumeSlider.value = 10;
        panel.volumeSlider.moved();
        SessionModel.commitParam("duck", "duckedVolume");
        compare(SessionModel.param("duck", "duckedVolume"), 0.1);
        compare(panel.volumeReadout.text, "10 % · -20.0 dB");

        panel.volumeSlider.value = 0;
        panel.volumeSlider.moved();
        SessionModel.commitParam("duck", "duckedVolume");
        compare(panel.volumeReadout.text, "0 % · silent");

        // The timing, folded away, and logarithmic where the range is wide:
        // the middle of the attack's slider is the geometric middle.
        verify(!panel.timingSection.expanded, "the timing is open before anybody asked for it");
        panel.timingSection.expanded = true;
        tryVerify(() => Math.abs(panel.attackSlider.value - Math.log(10) / Math.log(200)) < 1e-6, 1000,
                  "the attack's slider is not where 50 ms is");
        panel.attackSlider.value = 0.5;
        panel.attackSlider.moved();
        SessionModel.commitParam("duck", "attack");
        verify(Math.abs(SessionModel.param("duck", "attack") - Math.sqrt(5 * 1000)) < 0.01,
               "the middle of the attack slider is " + SessionModel.param("duck", "attack"));

        panel.holdSlider.value = 0.25;
        panel.holdSlider.moved();
        SessionModel.commitParam("duck", "hold");
        compare(SessionModel.param("duck", "hold"), 500);

        panel.releaseSlider.value = 1;
        panel.releaseSlider.moved();
        SessionModel.commitParam("duck", "release");
        verify(Math.abs(SessionModel.param("duck", "release") - 5000) < 0.01);
    }

    function test_theStatusFollowsWhatTheBlockSays() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "duck";
        compare(panel.statusLabel.text, "Not ducking");
        panel.published = [-8, 1];
        compare(panel.statusLabel.text, "Ducking");
        compare(panel.statusLabel.color, AppColors.liveText);
        panel.published = [0, 0];
        compare(panel.statusLabel.text, "Not ducking");
    }
}
