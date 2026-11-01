// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What the global shortcuts do to the front session's blocks, and
// that the main window answers them. **No kglobalaccel here**: the registry
// is made only by the application's main(), so a test run never binds a key
// on a desktop. A press is made through GlobalShortcuts.trigger(), which is
// what the registry calls.
//
// **One mock engine serves the whole suite**, so the session this file makes
// is found by its id.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "GlobalShortcuts"
    when: windowShown

    property string sessionId: ""
    readonly property Item speaker: Item {}

    Component {
        id: targetsComponent

        ShortcutTargets {}
    }

    SignalSpy {
        id: finished

        target: TestEngine
        signalName: "recordingFileFinished"
    }

    Component {
        id: readingComponent

        TimeShiftState {}
    }

    Component {
        id: mainWindow

        Main {}
    }

    function add(id: string, type: string) {
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: id, type: type }, index: -1 }));
    }

    function initTestCase() {
        const opened = function (id) {
            testCase.sessionId = id;
        };
        TestEngine.sessionOpened.connect(opened);
        TestEngine.createSession("Shortcuts");
        TestEngine.sessionOpened.disconnect(opened);
        verify(testCase.sessionId !== "", "the mock opened a session");
        // Two switches, to see that each is acted on; a Time Shift and a
        // Recorder.
        testCase.add("sw1", "advanced.inputswitch");
        testCase.add("sw2", "advanced.inputswitch");
        testCase.add("ts", "advanced.timeshift");
        testCase.add("rec", "output.recorder");
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => SessionModel.blockIds.length === 4, 1000, "the front session's blocks");
        SessionModel.setParams("ts", [["paused", false], ["target", 0], ["jump", 0]]);
        SessionModel.setParam("sw1", "input", "a");
        SessionModel.setParam("sw2", "input", "a");
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

    function test_sayVoiceLevelIsOneSentenceAndNothingWithNoGauge() {
        const targets = createTemporaryObject(targetsComponent, testCase, { speaker: testCase.speaker });
        Announcer.reset();
        compare(targets.act("voiceGauge.say"), 0);
        compare(Announcer.last, "");

        testCase.add("level", "meter.voicegauge");
        tryVerify(() => SessionModel.blockIds.indexOf("level") >= 0, 1000, "the block did not arrive");
        compare(targets.act("voiceGauge.say"), 1);
        compare(Announcer.last, "Not watching");

        testCase.run();
        // A voice, between marks.
        TestEngine.setFakeBlockValues(testCase.sessionId, "level", [112, -24, -120, 0]);
        tryVerify(() => Meters.watchState("level") === "watching" && (Meters.levels("level").values ?? [0])[0] === 112, 1000);
        compare(targets.act("voiceGauge.say"), 1);
        compare(Announcer.last, "Minus 24 LUFS, between marks");
        // Nothing arrives.
        TestEngine.setFakeBlockValues(testCase.sessionId, "level", [100, -24, -120, 0]);
        tryVerify(() => (Meters.levels("level").values ?? [0])[0] === 100, 1000);
        compare(targets.act("voiceGauge.say"), 1);
        compare(Announcer.last, "No sound here");

        testCase.add("other", "meter.voicegauge");
        tryVerify(() => SessionModel.blockIds.indexOf("other") >= 0, 1000, "the block did not arrive");
        SessionModel.renameBlock("level", "You");
        SessionModel.renameBlock("other", "Second");
        compare(targets.act("voiceGauge.say"), 2);
        verify(Announcer.last.startsWith("You: "), Announcer.last);
        verify(Announcer.last.indexOf(", Second: ") > 0, Announcer.last);

        TestEngine.setFakeBlockValues(testCase.sessionId, "level", []);
        TestEngine.stop(testCase.sessionId);
        Meters.watch("");
        for (const id of ["level", "other"]) {
            TestEngine.applyEdit(testCase.sessionId, JSON.stringify({ op: "removeBlock", blockId: id }));
        }
        tryVerify(() => SessionModel.blockIds.indexOf("other") < 0, 1000);
    }

    function test_sayWhoIsSpeakingIsOneSentenceAnsweringTheGap() {
        testCase.add("call", "meter.speechnow");
        testCase.add("you", "meter.speechnow");
        SessionModel.renameBlock("call", "Call");
        SessionModel.renameBlock("you", "You");
        tryVerify(() => SessionModel.blockIds.length === 6, 1000, "the blocks did not arrive");
        const targets = createTemporaryObject(targetsComponent, testCase, { speaker: testCase.speaker });
        Announcer.reset();
        compare(targets.act("speechNow.say"), 2);
        compare(Announcer.last, "Call not watching, You not watching");

        testCase.run();
        TestEngine.setFakeBlockValues(testCase.sessionId, "call", [1, 0.9, 0]);
        TestEngine.setFakeBlockValues(testCase.sessionId, "you", [0, 0, 0]);
        tryVerify(() => Meters.watchState("call") === "watching"
                  && (Meters.levels("call").values ?? [0])[0] === 1, 1000);
        // Right after the last one: it was asked for, so no gap holds it back.
        compare(targets.act("speechNow.say"), 2);
        compare(Announcer.last, "Call speaking, You quiet");
        TestEngine.setFakeBlockValues(testCase.sessionId, "call", []);
        TestEngine.setFakeBlockValues(testCase.sessionId, "you", []);
        for (const id of ["call", "you"]) {
            TestEngine.applyEdit(testCase.sessionId, JSON.stringify({ op: "removeBlock", blockId: id }));
        }
        tryVerify(() => SessionModel.blockIds.length === 4, 1000);
    }

    function test_theInputSwitchShortcutsSetEverySwitchInTheFrontSession() {
        const targets = createTemporaryObject(targetsComponent, testCase);
        compare(targets.act("inputSwitch.b"), 2);
        compare(SessionModel.param("sw1", "input"), "b");
        compare(SessionModel.param("sw2", "input"), "b");
        compare(targets.act("inputSwitch.a"), 2);
        compare(SessionModel.param("sw1", "input"), "a");
        compare(SessionModel.param("sw2", "input"), "a");
    }

    function test_theTimeShiftShortcutsMoveTheHeadAsItsTransportDoes() {
        const targets = createTemporaryObject(targetsComponent, testCase);
        // Stopped, there is no head to move, as its buttons say.
        compare(targets.act("timeShift.back"), 0);
        compare(SessionModel.param("ts", "jump"), 0);

        testCase.run();
        tryVerify(() => targets.act("timeShift.forward") === 1, 1000, "the running block never said where its head is");
        compare(SessionModel.param("ts", "jump"), 0, "forward at live moved the head");

        compare(targets.act("timeShift.back"), 1);
        compare(SessionModel.param("ts", "target"), 10);
        compare(SessionModel.param("ts", "jump"), 1);
        // The running block says where its head has got to before the next.
        const reading = createTemporaryObject(readingComponent, testCase, { blockId: "ts" });
        tryCompare(reading, "offset", 10);
        compare(targets.act("timeShift.back"), 1);
        compare(SessionModel.param("ts", "target"), 20);
        tryCompare(reading, "offset", 20);
        compare(targets.act("timeShift.forward"), 1);
        compare(SessionModel.param("ts", "target"), 10);

        compare(targets.act("timeShift.pause"), 1);
        compare(SessionModel.param("ts", "paused"), true);
        compare(targets.act("timeShift.pause"), 1);
        compare(SessionModel.param("ts", "paused"), false);

        compare(targets.act("timeShift.pause"), 1);
        compare(targets.act("timeShift.live"), 1);
        compare(SessionModel.param("ts", "paused"), false);
        compare(SessionModel.param("ts", "target"), 0);
    }

    function test_theRecorderShortcutStartsANewFileOnlyWhileItRecords() {
        const targets = createTemporaryObject(targetsComponent, testCase);
        compare(targets.act("recorder.split"), 0, "a Recorder that is not recording split");

        TestEngine.start(testCase.sessionId);
        tryVerify(() => RecordingStatus.state(testCase.sessionId, "rec") === "recording", 2000, "the Recorder never recorded");
        finished.clear();
        compare(targets.act("recorder.split"), 1);
        // **A split finishes one file and starts the next.**
        tryCompare(finished, "count", 1, 2000, "no file was finished");
        compare(finished.signalArguments[0][1], "rec");
        compare(RecordingStatus.state(testCase.sessionId, "rec"), "recording", "a split stopped the recording");
    }

    function test_withNoSessionInFrontNothingHappens() {
        const targets = createTemporaryObject(targetsComponent, testCase);
        SessionModel.sessionId = "";
        compare(targets.act("inputSwitch.b"), 0);
        compare(targets.act("recorder.split"), 0);
        compare(targets.act("no.suchAction"), 0);
    }

    function test_theMainWindowAnswersAPress() {
        const window = createTemporaryObject(mainWindow, testCase);
        verify(window, "the main window is created");
        tryVerify(() => AppShell.hasSession, 1000, "the window came up with a session");
        Sessions.currentId = testCase.sessionId;
        tryCompare(SessionModel, "sessionId", testCase.sessionId);
        tryVerify(() => SessionModel.blockIds.length === 4, 1000);

        GlobalShortcuts.trigger("inputSwitch.b");
        compare(SessionModel.param("sw1", "input"), "b", "the window did not act on a block shortcut");

        // **Show Reverblabr brings a hidden window back**, as with the tray.
        TrayState.available = true;
        window.close();
        tryVerify(() => !window.visible, 1000);
        GlobalShortcuts.trigger("window.show");
        tryVerify(() => window.visible, 1000, "Show Reverblabr left the window hidden");
        TrayState.available = false;
    }
}
