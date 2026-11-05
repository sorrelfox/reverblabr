// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Echo's face and panel offscreen, against the mock engine: the
// ranges are the engine's, the time and the tone are logarithmic, and every
// slider writes a whole number of its setting.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "Echo"
    when: windowShown

    property string sessionId: ""

    function panelFor() {
        const panel = createTemporaryObject(BlockPanels.panelFor("effect.echo"), testCase, { width: 320 });
        panel.blockId = "echo";
        return panel;
    }

    function faceFor() {
        const face = createTemporaryObject(BlockFaces.faceFor("effect.echo"), testCase, { width: 220, height: 120 });
        face.blockId = "echo";
        return face;
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Echo");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "echo", type: "effect.echo" }, index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParam("echo", "time", 350);
        SessionModel.setParam("echo", "feedback", 40);
        SessionModel.setParam("echo", "tone", 5000);
        SessionModel.setParam("echo", "mix", 35);
    }

    function test_theBlockHasAFaceAndAPanelOfItsOwn() {
        verify(BlockPanels.panelFor("effect.echo") !== null, "the Echo is in the panel register");
        verify(BlockFaces.faceFor("effect.echo") !== null, "the Echo is in the face register");
    }

    function test_theRangesAreTheEnginesOwn_data() {
        return [
            { tag: "time", id: "time", lowest: "shortestTime", highest: "longestTime", fallback: "defaultTime", taper: "log" },
            { tag: "feedback", id: "feedback", lowest: "", highest: "highestFeedback", fallback: "defaultFeedback", taper: "linear" },
            { tag: "tone", id: "tone", lowest: "lowestTone", highest: "highestTone", fallback: "defaultTone", taper: "log" },
            { tag: "mix", id: "mix", lowest: "", highest: "", fallback: "defaultMix", taper: "linear" },
        ];
    }

    function test_theRangesAreTheEnginesOwn(data) {
        const words = testCase.panelFor().words;
        const descriptor = BlockCatalogue.describeParam("effect.echo", data.id);
        compare(descriptor.minimum, data.lowest === "" ? 0 : words[data.lowest]);
        compare(descriptor.maximum, data.highest === "" ? 100 : words[data.highest]);
        compare(descriptor.default, words[data.fallback]);
        compare(descriptor.taper, data.taper);
    }

    function test_theTimeAndTheToneAreLogarithmic() {
        const panel = testCase.panelFor();
        // The middle of 1 ms to 2 s is their geometric mean, 45 ms.
        panel.timeRow.slider.value = 0.5;
        panel.timeRow.slider.moved();
        SessionModel.commitParam("echo", "time");
        compare(SessionModel.param("echo", "time"), 45);
        compare(panel.timeRow.readout.text, "45 ms");

        // And of 500 Hz to 20 kHz, 3162 Hz.
        panel.toneRow.slider.value = 0.5;
        panel.toneRow.slider.moved();
        SessionModel.commitParam("echo", "tone");
        compare(SessionModel.param("echo", "tone"), 3162);
        compare(panel.toneRow.readout.text, "3.16 kHz");
    }

    function test_theFeedbackAndTheMixAreWholePerCents() {
        const panel = testCase.panelFor();
        panel.feedbackRow.slider.value = 95;
        panel.feedbackRow.slider.moved();
        SessionModel.commitParam("echo", "feedback");
        compare(SessionModel.param("echo", "feedback"), 95);
        compare(panel.feedbackRow.readout.text, "95 %");
        compare(panel.feedbackRow.slider.to, 95);

        panel.mixRow.slider.value = 60;
        panel.mixRow.slider.moved();
        SessionModel.commitParam("echo", "mix");
        compare(SessionModel.param("echo", "mix"), 60);
    }

    function test_thePanelFollowsTheSession() {
        const panel = testCase.panelFor();
        SessionModel.setParam("echo", "time", 1250);
        SessionModel.setParam("echo", "feedback", 72);
        tryCompare(panel.timeRow.readout, "text", "1.25 s");
        compare(panel.feedbackRow.readout.text, "72 %");
    }

    function test_theFaceSaysTheTimeAndMovesIt() {
        const face = testCase.faceFor();
        compare(face.headlineLabel.text, "350 ms");
        compare(face.captionLabel.text, "echo");
        verify(face.detail.indexOf("40 %") >= 0 && face.detail.indexOf("5.00 kHz") >= 0, face.detail);
        face.slider.value = 1;
        face.slider.moved();
        SessionModel.commitParam("echo", "time");
        compare(SessionModel.param("echo", "time"), 2000);
        compare(face.headlineLabel.text, "2.00 s");
    }
}
