// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Reverb's face and panel offscreen, against the mock engine: the
// ranges are the engine's, the decay is logarithmic, every slider writes a
// whole number of its setting, and the face says the decay and the mix.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "Reverb"
    when: windowShown

    property string sessionId: ""

    function panelFor() {
        const panel = createTemporaryObject(BlockPanels.panelFor("effect.reverb"), testCase, { width: 320 });
        panel.blockId = "reverb";
        return panel;
    }

    function faceFor() {
        const face = createTemporaryObject(BlockFaces.faceFor("effect.reverb"), testCase, { width: 220, height: 120 });
        face.blockId = "reverb";
        return face;
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Reverb");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "reverb", type: "effect.reverb" }, index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParam("reverb", "preDelay", 20);
        SessionModel.setParam("reverb", "size", 50);
        SessionModel.setParam("reverb", "decay", 2000);
        SessionModel.setParam("reverb", "damping", 40);
        SessionModel.setParam("reverb", "width", 100);
        SessionModel.setParam("reverb", "mix", 25);
    }

    function test_theBlockHasAFaceAndAPanelOfItsOwn() {
        verify(BlockPanels.panelFor("effect.reverb") !== null, "the Reverb is in the panel register");
        verify(BlockFaces.faceFor("effect.reverb") !== null, "the Reverb is in the face register");
    }

    function test_theRangesAreTheEnginesOwn_data() {
        return [
            { tag: "preDelay", id: "preDelay", lowest: "", highest: "longestPreDelay", fallback: "defaultPreDelay", taper: "linear" },
            { tag: "size", id: "size", lowest: "", highest: "", fallback: "defaultSize", taper: "linear" },
            { tag: "decay", id: "decay", lowest: "shortestDecay", highest: "longestDecay", fallback: "defaultDecay", taper: "log" },
            { tag: "damping", id: "damping", lowest: "", highest: "", fallback: "defaultDamping", taper: "linear" },
            { tag: "width", id: "width", lowest: "", highest: "", fallback: "defaultWidth", taper: "linear" },
            { tag: "mix", id: "mix", lowest: "", highest: "", fallback: "defaultMix", taper: "linear" },
        ];
    }

    function test_theRangesAreTheEnginesOwn(data) {
        const words = testCase.panelFor().words;
        const descriptor = BlockCatalogue.describeParam("effect.reverb", data.id);
        compare(descriptor.minimum, data.lowest === "" ? 0 : words[data.lowest]);
        compare(descriptor.maximum, data.highest === "" ? (data.id === "preDelay" ? 250 : 100) : words[data.highest]);
        compare(descriptor.default, words[data.fallback]);
        compare(descriptor.taper, data.taper);
    }

    function test_theDecayIsLogarithmic() {
        const panel = testCase.panelFor();
        // The middle of 100 ms to 20 s is their geometric mean, 1414 ms.
        panel.decayRow.slider.value = 0.5;
        panel.decayRow.slider.moved();
        SessionModel.commitParam("reverb", "decay");
        compare(SessionModel.param("reverb", "decay"), 1414);
        compare(panel.decayRow.readout.text, "1.41 s");
    }

    function test_theOthersAreWholeNumbers() {
        const panel = testCase.panelFor();
        panel.preDelayRow.slider.value = 120;
        panel.preDelayRow.slider.moved();
        SessionModel.commitParam("reverb", "preDelay");
        compare(SessionModel.param("reverb", "preDelay"), 120);
        compare(panel.preDelayRow.readout.text, "120 ms");
        compare(panel.preDelayRow.slider.to, 250);

        panel.sizeRow.slider.value = 80;
        panel.sizeRow.slider.moved();
        SessionModel.commitParam("reverb", "size");
        compare(SessionModel.param("reverb", "size"), 80);
        compare(panel.sizeRow.readout.text, "80 %");

        panel.mixRow.slider.value = 60;
        panel.mixRow.slider.moved();
        SessionModel.commitParam("reverb", "mix");
        compare(SessionModel.param("reverb", "mix"), 60);
    }

    function test_thePanelFollowsTheSession() {
        const panel = testCase.panelFor();
        SessionModel.setParam("reverb", "decay", 3200);
        SessionModel.setParam("reverb", "damping", 15);
        SessionModel.setParam("reverb", "width", 70);
        tryCompare(panel.decayRow.readout, "text", "3.20 s");
        compare(panel.dampingRow.readout.text, "15 %");
        compare(panel.widthRow.readout.text, "70 %");
    }

    function test_theFaceSaysTheDecayAndTheMixAndMovesTheDecay() {
        const face = testCase.faceFor();
        compare(face.headlineLabel.text, "2.00 s");
        compare(face.captionLabel.text, "25 % mix");
        verify(face.detail.indexOf("20 ms") >= 0 && face.detail.indexOf("40 %") >= 0, face.detail);
        SessionModel.setParam("reverb", "mix", 40);
        tryCompare(face.captionLabel, "text", "40 % mix");
        face.slider.value = 0;
        face.slider.moved();
        SessionModel.commitParam("reverb", "decay");
        compare(SessionModel.param("reverb", "decay"), 100);
        compare(face.headlineLabel.text, "100 ms");
    }
}
