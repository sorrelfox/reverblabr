// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Time Shift block's face and panel offscreen, against the mock
// engine: every button of the transport sends the head where it says, from
// where the running block says it is; the face and the scrubber say where
// that is; and with the session stopped there is nothing to move. That the
// block's head lands on the exact sample, and without a click, is measured
// in blocks.TimeShiftTest.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "TimeShift"
    when: windowShown

    property string sessionId: ""

    function panelFor() {
        const panel = createTemporaryObject(BlockPanels.panelFor("advanced.timeshift"), testCase, { width: 340 });
        panel.blockId = "ts";
        return panel;
    }

    function faceFor() {
        const face = createTemporaryObject(BlockFaces.faceFor("advanced.timeshift"), testCase, { width: 180, height: 90 });
        face.blockId = "ts";
        return face;
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Time Shift");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "ts", type: "advanced.timeshift" }, index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParams("ts", [["paused", false], ["target", 0], ["jump", 0], ["minutes", 5]]);
    }

    function cleanup() {
        TestEngine.stop(testCase.sessionId);
        Meters.watch("");
    }

    function run() {
        Meters.watch(testCase.sessionId);
        TestEngine.start(testCase.sessionId);
        tryVerify(() => Meters.live, 1000, "the session never went live");
    }

    function test_theBlockHasAFaceAndAPanelOfItsOwn() {
        verify(BlockPanels.panelFor("advanced.timeshift") !== null, "Time Shift is in the panel register");
        verify(BlockFaces.faceFor("advanced.timeshift") !== null, "Time Shift is in the face register");
    }

    function test_theRangeIsTheEnginesOwn() {
        const panel = testCase.panelFor();
        const minutes = BlockCatalogue.describeParam("advanced.timeshift", "minutes");
        compare(panel.longestMinutes, minutes.maximum);
        compare(panel.defaultMinutes, minutes.default);
        compare(panel.minutesBox.to, minutes.maximum);
    }

    function test_stoppedThereIsNothingToMove() {
        const face = testCase.faceFor();
        const panel = testCase.panelFor();
        compare(face.headlineLabel.text, "Live");
        verify(!face.pauseButton.enabled, "the face's pause works with nothing kept");
        verify(!face.backButton.enabled, "the face's jump back works with nothing kept");
        verify(!panel.positionSlider.enabled, "the scrubber works with nothing kept");
        for (const button of panel.jumpButtons) {
            verify(!button.enabled, button.text + " works with nothing kept");
        }
        verify(!panel.liveButton.enabled);
        verify(panel.minutesBox.enabled, "how much to keep cannot be set while stopped");
    }

    function test_theFaceJumpsTenSecondsEitherWay() {
        const face = testCase.faceFor();
        testCase.run();
        tryVerify(() => face.backButton.enabled, 1000, "the face's transport never woke up");
        verify(!face.forwardButton.enabled, "forward past live can be pressed");
        face.backButton.clicked();
        compare(SessionModel.param("ts", "target"), 10);
        compare(SessionModel.param("ts", "jump"), 1);
        tryCompare(face.headlineLabel, "text", "−10 s");
        face.backButton.clicked();
        compare(SessionModel.param("ts", "target"), 20);
        compare(SessionModel.param("ts", "jump"), 2, "a second press is a second jump");
        tryCompare(face.headlineLabel, "text", "−20 s");
        face.forwardButton.clicked();
        compare(SessionModel.param("ts", "target"), 10);
        tryCompare(face.headlineLabel, "text", "−10 s");
    }

    function test_thePanelsTransportGoesWhereEachButtonSays() {
        const panel = testCase.panelFor();
        testCase.run();
        tryVerify(() => panel.positionSlider.enabled, 1000, "the panel's transport never woke up");
        // Back 30, 10 and 3, then forward 3 and 10: 30, 40, 43, 40, 30.
        const expected = [30, 40, 43];
        for (let i = 0; i < 3; ++i) {
            panel.jumpButtons[i].clicked();
            compare(SessionModel.param("ts", "target"), expected[i], panel.jumpButtons[i].Accessible.name);
            tryCompare(panel.reading, "offset", expected[i]);
        }
        panel.jumpButtons[3].clicked();
        compare(SessionModel.param("ts", "target"), 40);
        tryCompare(panel.reading, "offset", 40);
        panel.jumpButtons[4].clicked();
        compare(SessionModel.param("ts", "target"), 30);
        tryCompare(panel.positionLabel, "text", "−30 s");
        compare(panel.positionSlider.value, -30, "the scrubber is not where the head is");
        // Forward 30 from 30 is live, and forward past it is still live.
        panel.jumpButtons[5].clicked();
        compare(SessionModel.param("ts", "target"), 0);
        tryCompare(panel.positionLabel, "text", "Live");
        verify(!panel.jumpButtons[5].enabled, "forward can be pressed at live");
        verify(!panel.liveButton.enabled, "Live can be pressed at live");
    }

    function test_aJumpBackGoesNoFurtherThanIsKept() {
        SessionModel.setParam("ts", "minutes", 1);
        const panel = testCase.panelFor();
        testCase.run();
        tryCompare(panel.reading, "available", 60);
        panel.positionSlider.value = -45;
        panel.positionSlider.moved();
        compare(SessionModel.param("ts", "target"), 45);
        tryCompare(panel.reading, "offset", 45);
        panel.jumpButtons[0].clicked();
        compare(SessionModel.param("ts", "target"), 60, "back 30 from 45 went past the minute kept");
    }

    function test_pauseAndLive() {
        const face = testCase.faceFor();
        const panel = testCase.panelFor();
        testCase.run();
        tryVerify(() => panel.pauseButton.enabled, 1000);
        compare(panel.pauseButton.text, "Pause");
        panel.pauseButton.clicked();
        compare(SessionModel.param("ts", "paused"), true);
        tryCompare(face.headlineLabel, "text", "Paused");
        compare(face.pauseButton.text, "Play");
        verify(panel.liveButton.enabled, "Live cannot be pressed while paused");
        face.pauseButton.clicked();
        compare(SessionModel.param("ts", "paused"), false);
        panel.jumpButtons[1].clicked();
        panel.pauseButton.clicked();
        tryCompare(face.headlineLabel, "text", "Paused");
        panel.liveButton.clicked();
        compare(SessionModel.param("ts", "paused"), false, "Live left it paused");
        compare(SessionModel.param("ts", "target"), 0);
        tryCompare(face.headlineLabel, "text", "Live");
    }

    function test_theScrubberJumpsWhenLetGo() {
        const panel = testCase.panelFor();
        testCase.run();
        tryVerify(() => panel.positionSlider.enabled, 1000);
        compare(panel.positionSlider.from, -300, "five minutes kept is not the scrubber's length");
        const jumps = SessionModel.param("ts", "jump");
        panel.positionSlider.value = -125;
        panel.positionSlider.moved();
        compare(SessionModel.param("ts", "target"), 125);
        compare(SessionModel.param("ts", "jump"), jumps + 1);
        tryCompare(panel.positionLabel, "text", "−2:05");
    }

    function test_howFarBehindIsSaid() {
        const face = testCase.faceFor();
        compare(face.reading.behind(0), "Live");
        compare(face.reading.behind(3.46), "−3.5 s");
        compare(face.reading.behind(42.4), "−42 s");
        compare(face.reading.behind(725), "−12:05");
        compare(face.reading.behind(3600), "−60:00");
    }

    function test_theMinutesKeptAreSet() {
        const panel = testCase.panelFor();
        compare(panel.minutesBox.value, 5);
        panel.minutesBox.value = 30;
        panel.minutesBox.valueModified();
        compare(SessionModel.param("ts", "minutes"), 30);
    }
}
