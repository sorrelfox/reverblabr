// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What the panel widget knows about the sessions, and what it costs: the
// list against an engine of the test's own (FakeEngine, tst_plasmoid.cpp)
// on a private bus.

import QtQuick
import QtTest
import io.github.reverblabr.plasmoid
import ReverblabrTest

TestCase {
    id: testCase

    name: "SessionList"

    Component {
        id: listComponent

        SessionList {}
    }

    function init() {
        FakeEngine.reset();
    }

    function makeList(): var {
        const list = createTemporaryObject(listComponent, testCase);
        verify(list, "the list is made");
        return list;
    }

    function readyList(): var {
        const list = makeList();
        tryVerify(() => list.ready, 2000, "the engine's list never came");
        return list;
    }

    function test_listsWhatTheEngineHasByName() {
        FakeEngine.addSession("b", "Zither practice", "closed");
        FakeEngine.addSession("a", "evening radio", "idle");
        FakeEngine.addSession("c", "Calls", "running");
        const list = readyList();
        compare(list.count, 3);
        compare(list.rowAt(0).name, "Calls");
        compare(list.rowAt(1).name, "evening radio");
        compare(list.rowAt(2).name, "Zither practice");
        compare(list.rowAt(list.rowOf("b")).state, "closed");
        compare(list.runningCount, 1);
        verify(list.engineRunning);
    }

    function test_hearsWhatOthersDoWithoutAsking() {
        FakeEngine.addSession("a", "Radio", "idle");
        const list = readyList();
        FakeEngine.clearCalls();
        FakeEngine.setState("a", "running");
        tryCompare(list, "runningCount", 1);
        FakeEngine.setState("a", "idle");
        tryCompare(list, "runningCount", 0);
        compare(FakeEngine.calls, [], "the list asked the engine about what it had heard");
    }

    // **An engine that is not running is not started** by the widget being
    // there: the bus here has nothing to start, so a call that asked it to
    // would say the engine is not installed.
    function test_doesNotStartAnEngineThatIsNotRunning() {
        FakeEngine.disappear();
        const list = makeList();
        wait(500);
        verify(!list.engineRunning);
        verify(!list.ready);
        verify(list.installed, "the list asked the bus to start an engine");
        verify(!list.holdingEngine);

        FakeEngine.addSession("a", "Radio", "idle");
        verify(FakeEngine.appear());
        tryVerify(() => list.ready, 2000, "an engine that came was not asked for its list");
        compare(list.count, 1);
    }

    // **It never keeps an engine alive**: the engine counts as its client
    // whoever has called it and is still on the bus, and the widget is on
    // the bus for as long as the desktop runs.
    function test_letsTheEngineGoAfterItsAnswers() {
        FakeEngine.addSession("a", "Radio", "idle");
        const list = readyList();
        verify(FakeEngine.clientCount > 0 || !list.holdingEngine);
        tryVerify(() => FakeEngine.clientCount === 0, 8000, "the widget stayed one of the engine's clients");
        verify(!list.holdingEngine);

        list.run("a");
        tryCompare(list, "runningCount", 1);
        tryVerify(() => FakeEngine.clientCount === 0, 8000, "the widget stayed a client after Run");
    }

    function test_runOpensASessionOnlyOnDiskFirst() {
        FakeEngine.addSession("a", "Radio", "closed");
        const list = readyList();
        FakeEngine.clearCalls();
        list.run("a");
        tryCompare(list, "runningCount", 1);
        compare(FakeEngine.calls, ["OpenSession a", "Start a"]);
        list.stop("a");
        tryCompare(list, "runningCount", 0);
        compare(list.rowAt(list.rowOf("a")).state, "idle");
    }

    function test_saysWhichSessionCouldNotRun() {
        FakeEngine.addSession("a", "Radio", "idle");
        FakeEngine.refuseStart("a");
        const list = readyList();
        list.run("a");
        tryVerify(() => (list.failure.session ?? "") === "a", 2000, "the refusal was not kept");
        compare(list.failure.name, "Radio");
        compare(list.failure.code, "session.cannotRun");
        list.run("a");
        compare(list.failure.session, undefined, "a new Run did not clear the old failure");
        tryCompare(list, "runningCount", 1);
    }

    // **Idle costs nothing**: the levels are read on a timer only while the
    // popup is open and a session runs.
    function test_readsLevelsOnlyWhileWatchedAndRunning() {
        FakeEngine.addSession("a", "Radio", "idle");
        const list = readyList();
        verify(!list.readingLevels);
        list.watching = true;
        verify(!list.readingLevels, "levels were read with nothing running");

        FakeEngine.setState("a", "running");
        tryVerify(() => list.readingLevels, 2000, "levels were not read while a session ran");
        FakeEngine.publishPeak("a", 0.5);
        // 0.5 is -6 dBFS.
        tryVerify(() => list.rowAt(0).level > -7 && list.rowAt(0).level < -5, 2000,
                  "the level is not the published one: " + list.rowAt(0).level);

        list.watching = false;
        verify(!list.readingLevels, "levels were read with nobody watching");
        compare(list.rowAt(0).level, -120);

        list.watching = true;
        verify(list.readingLevels);
        FakeEngine.setState("a", "idle");
        tryVerify(() => !list.readingLevels, 2000, "levels were read after the session stopped");
        compare(list.rowAt(0).level, -120);
    }

    function test_aSessionThatRecordsSaysSo() {
        FakeEngine.addSession("a", "Radio", "running");
        const list = readyList();
        verify(!list.recording);
        FakeEngine.startRecording("a", "/tmp/one.flac");
        tryVerify(() => list.recording, 2000);
        verify(list.rowAt(0).recording);
        // **A split ends one file and starts the next**: still recording.
        FakeEngine.startRecording("a", "/tmp/two.flac");
        FakeEngine.finishRecording("a", "/tmp/one.flac");
        wait(100);
        verify(list.recording, "a split read as the end of the recording");
        FakeEngine.finishRecording("a", "/tmp/two.flac");
        tryVerify(() => !list.recording, 2000);
    }

    function test_anEngineThatLeavesLeavesNothingRunning() {
        FakeEngine.addSession("a", "Radio", "running");
        const list = readyList();
        FakeEngine.startRecording("a", "/tmp/one.flac");
        tryVerify(() => list.recording, 2000);
        FakeEngine.disappear();
        tryVerify(() => !list.engineRunning, 2000);
        compare(list.runningCount, 0);
        verify(!list.recording);
        compare(list.count, 1, "the sessions are still saved");
        compare(list.rowAt(0).state, "closed");
    }

    function test_saysWhenThereIsNoEngineToStart() {
        FakeEngine.disappear();
        const list = makeList();
        list.watching = true;
        tryVerify(() => !list.installed, 5000, "a bus with no engine to start was not noticed");
        verify(FakeEngine.appear());
        tryVerify(() => list.installed, 2000, "an engine that came did not count as installed");
    }
}
