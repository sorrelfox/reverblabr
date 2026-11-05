// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Mixer's face and panel, against the mock engine: five inputs
// on the tile and five strips in the panel, each strip named for what is
// wired into it, and every timed change sent as one edit in the order the
// block needs — the ramp, the volume, the ramp put back.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "Mixer"
    when: windowShown

    property string sessionId: ""
    /// Every parameter the engine said was set, in order, as [key, value].
    property var sent: []

    Component {
        id: panelComponent

        MixerPanel {
            width: 480
        }
    }

    Component {
        id: faceComponent

        MixerFace {
            width: 220
            height: 120
        }
    }

    Component {
        id: canvasComponent

        SessionCanvas {
            width: 900
            height: 600
        }
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.sessionPatch.connect(function (id, revision, json) {
            const command = JSON.parse(json);
            if (command.op === "setParam") {
                testCase.sent.push([command.key, command.value]);
            }
        });
        TestEngine.createSession("Mixer");
        verify(testCase.sessionId !== "", "the mock opened a session");
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.sessionId = testCase.sessionId;
        testCase.sent = [];
    }

    function cleanup() {
        SessionModel.removeBlocks(SessionModel.blockIds);
    }

    function addMixer(): string {
        return SessionModel.addBlock("advanced.mixer", Qt.point(500, 200));
    }

    function test_itIsInAdvancedWithFiveNamedInputs() {
        verify(BlockCatalogue.types.indexOf("advanced.mixer") >= 0, "the engine does not offer it");
        compare(BlockPresentation.category("advanced.mixer"), "advanced");
        verify(BlockFaces.faceFor("advanced.mixer") !== null, "it has no face");
        verify(BlockPanels.panelFor("advanced.mixer") !== null, "it has no panel");
        const inputs = BlockCatalogue.describe("advanced.mixer").inputs;
        compare(inputs.laneWidth, 2);
        compare(inputs.lanes.length, 5);
        compare(BlockPresentation.laneName("input1"), "Input 1");
        compare(BlockPresentation.laneName("input5"), "Input 5");

        const canvas = createTemporaryObject(canvasComponent, null);
        const mixer = canvas.addBlockAt("advanced.mixer", 500, 200);
        compare(canvas.laneCountOf(mixer, false), 5, "the port limit is five connectors");
        compare(canvas.laneIdOf(mixer, 4, false), "input5");
        compare(canvas.laneIdOf(mixer, 5, false), "", "there is no sixth");
    }

    function test_eachStripIsNamedForWhatIsWiredIntoIt() {
        const mixer = testCase.addMixer();
        SessionModel.setAutoConnect(false);
        const voice = SessionModel.addBlock("source.device", Qt.point(100, 100));
        const music = SessionModel.addBlock("source.system", Qt.point(100, 300));
        SessionModel.renameBlock(voice, "Guest");
        verify(SessionModel.connectLanes(voice, mixer, 2, 2));
        verify(SessionModel.connectLanes(music, mixer, 2, 6));
        SessionModel.setAutoConnect(true);

        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = mixer;
        tryCompare(panel.stripRows, "count", 5);
        compare(panel.stripRows.itemAt(1).title.text, "Guest", "the nickname names the strip");
        compare(panel.stripRows.itemAt(3).title.text, BlockPresentation.name("source.system"));
        compare(panel.stripRows.itemAt(0).title.text, "No source");
        verify(panel.stripRows.itemAt(0).opacity < 1, "an empty strip is not dimmed");
        compare(panel.stripRows.itemAt(1).opacity, 1);

        const face = createTemporaryObject(faceComponent, null);
        face.blockId = mixer;
        compare(face.captionLabel.text, "2 of 5 inputs");
        compare(face.masterLabel.text, "100 %");
    }

    function test_aFadeIsTheRampTheVolumeAndTheRampPutBackInOneEdit() {
        const mixer = testCase.addMixer();
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = mixer;
        testCase.sent = [];

        panel.stripRows.itemAt(1).fadeDownButton.clicked();
        compare(testCase.sent, [["ramp2", 500], ["volume2", 0], ["ramp2", 20]]);
        compare(SessionModel.param(mixer, "volume2"), 0);

        // **One step to undo**, which puts the volume back the same way:
        // over the fade's ramp, to what it was (never set, so the default).
        testCase.sent = [];
        SessionModel.undo();
        tryVerify(() => testCase.sent.length === 3, 1000, "the undo was not one step of three");
        compare(testCase.sent[0], ["ramp2", 500]);
        compare(testCase.sent.map(entry => entry[0]), ["ramp2", "volume2", "ramp2"]);
        verify(testCase.sent[1][1] == null && testCase.sent[2][1] == null, "not put back to what it was");

        // With Shift, two seconds.
        testCase.sent = [];
        panel.fadeStrips([1], 0, true);
        compare(testCase.sent, [["ramp2", 2000], ["volume2", 0], ["ramp2", 20]]);

        // Every strip at once.
        testCase.sent = [];
        panel.fadeAllDownButton.clicked();
        compare(testCase.sent.length, 15);
        compare(testCase.sent.slice(5, 10), [["volume1", 0], ["volume2", 0], ["volume3", 0], ["volume4", 0],
                                             ["volume5", 0]]);
        compare(testCase.sent[0], ["ramp1", 500]);
        compare(testCase.sent[14], ["ramp5", 20]);
    }

    function test_soloIsAdditiveAndExclusiveWithAlt() {
        const mixer = testCase.addMixer();
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = mixer;

        panel.toggleSolo(0, false);
        panel.toggleSolo(3, false);
        compare(SessionModel.param(mixer, "solo1"), true);
        compare(SessionModel.param(mixer, "solo4"), true);
        tryVerify(() => panel.stripRows.itemAt(3).soloButton.checked, 1000);

        panel.toggleSolo(2, true);
        tryCompare(panel.stripRows.itemAt(2).soloButton, "checked", true);
        compare(SessionModel.param(mixer, "solo1"), false);
        compare(SessionModel.param(mixer, "solo3"), true);
        compare(SessionModel.param(mixer, "solo4"), false);

        panel.toggleSolo(2, false);
        compare(SessionModel.param(mixer, "solo3"), false);
    }

    function test_aSceneIsSavedAndRecalledOverASecond() {
        const mixer = testCase.addMixer();
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = mixer;
        verify(!panel.sceneButtons.itemAt(1).enabled, "a scene nobody saved can be recalled");

        SessionModel.setParam(mixer, "volume1", 0.5);
        SessionModel.setParam(mixer, "volume3", 1.5);
        panel.saveScene("B");
        compare(SessionModel.param(mixer, "sceneB1"), 0.5);
        compare(SessionModel.param(mixer, "sceneB2"), 1);
        compare(SessionModel.param(mixer, "sceneB3"), 1.5);
        compare(SessionModel.param(mixer, "sceneBSaved"), true);
        tryVerify(() => panel.sceneButtons.itemAt(1).enabled, 1000, "a saved scene cannot be recalled");
        verify(!panel.sceneButtons.itemAt(0).enabled);

        SessionModel.setParam(mixer, "volume1", 2);
        SessionModel.setParam(mixer, "volume3", 0);
        testCase.sent = [];
        panel.sceneButtons.itemAt(1).clicked();
        compare(testCase.sent[0], ["ramp1", 1000]);
        compare(testCase.sent[5], ["volume1", 0.5]);
        compare(testCase.sent[7], ["volume3", 1.5]);
        compare(testCase.sent[14], ["ramp5", 20]);
        compare(SessionModel.param(mixer, "volume1"), 0.5);
    }

    function test_theStripsControlsWriteWhatTheyName() {
        const mixer = testCase.addMixer();
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = mixer;
        const strip = panel.stripRows.itemAt(4);

        strip.volumeSlider.value = 150;
        strip.volumeSlider.moved();
        SessionModel.commitParam(mixer, "volume5");
        compare(SessionModel.param(mixer, "volume5"), 1.5, "the volume is written as an amplitude");
        compare(strip.volumeReadout.text, "150 %");

        strip.muteButton.toggle();
        strip.muteButton.toggled();
        compare(SessionModel.param(mixer, "mute5"), true);

        strip.balanceSlider.value = -0.5;
        strip.balanceSlider.moved();
        SessionModel.commitParam(mixer, "balance5");
        compare(SessionModel.param(mixer, "balance5"), -0.5);

        panel.masterSlider.value = 50;
        panel.masterSlider.moved();
        SessionModel.commitParam(mixer, "master");
        compare(SessionModel.param(mixer, "master"), 0.5);
    }
}
