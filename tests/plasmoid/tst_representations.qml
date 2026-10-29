// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The panel widget's two faces, from its package: the icon in the panel
// and the list in its popup, against an engine of the test's own
// (FakeEngine, tst_plasmoid.cpp) on a private bus. main.qml needs a
// running Plasma to load, so the representations are made here the way it
// makes them.

import QtQuick
import QtTest
import io.github.reverblabr.plasmoid
import ReverblabrTest
import "../../plasmoid/package/contents/ui"

TestCase {
    id: testCase

    name: "Representations"
    when: windowShown
    width: 400
    height: 600
    // **Shown, unlike a TestCase's default**: what the representations
    // show is read from their parts' visibility, and a click needs an item
    // on the screen.
    visible: true

    Component {
        id: listComponent

        SessionList {}
    }

    Component {
        id: compactComponent

        CompactRepresentation {
            width: 32
            height: 32
        }
    }

    Component {
        id: fullComponent

        FullRepresentation {
            width: 360
            height: 480
        }
    }

    function init() {
        FakeEngine.reset();
    }

    function readyList(): var {
        const list = createTemporaryObject(listComponent, testCase);
        tryVerify(() => list.ready, 2000, "the engine's list never came");
        return list;
    }

    function makeFull(list: var): var {
        list.watching = true;
        const full = createTemporaryObject(fullComponent, testCase, {sessions: list});
        verify(full, "the popup is made");
        waitForRendering(full);
        return full;
    }

    function rowOf(full: var, list: var, sessionId: string): var {
        const index = list.rowOf(sessionId);
        full.sessionList.positionViewAtIndex(index, ListView.Contain);
        tryVerify(() => full.sessionList.itemAtIndex(index) !== null, 1000, "the session has no row");
        return full.sessionList.itemAtIndex(index);
    }

    // --- in the panel ---

    function test_theIconShowsWhetherAnythingRuns() {
        FakeEngine.addSession("a", "Radio", "idle");
        const list = readyList();
        const compact = createTemporaryObject(compactComponent, testCase, {sessions: list});
        verify(!compact.runningMark.visible);
        verify(!compact.recordingMark.visible);
        compare(compact.Accessible.description, "No session is running");

        FakeEngine.setState("a", "running");
        tryVerify(() => compact.runningMark.visible, 2000, "the icon did not say a session runs");
        compare(compact.Accessible.description, "1 session running");

        FakeEngine.startRecording("a", "/tmp/one.flac");
        tryVerify(() => compact.recordingMark.visible, 2000, "the icon did not say a session records");
        compare(compact.Accessible.description, "1 session running, recording");

        FakeEngine.setState("a", "idle");
        tryVerify(() => !compact.runningMark.visible && !compact.recordingMark.visible, 2000);
    }

    function test_aClickOnTheIconAsksForThePopup() {
        const list = readyList();
        const compact = createTemporaryObject(compactComponent, testCase, {sessions: list});
        const toggled = createTemporaryObject(spyComponent, testCase, {target: compact, signalName: "toggled"});
        mouseClick(compact);
        compare(toggled.count, 1);
    }

    Component {
        id: spyComponent

        SignalSpy {}
    }

    // --- in the popup ---

    function test_thePopupListsEverySessionWithRunAndStop() {
        FakeEngine.addSession("a", "Radio", "idle");
        FakeEngine.addSession("b", "Calls", "closed");
        const list = readyList();
        const full = makeFull(list);
        tryCompare(full.sessionList, "count", 2);
        verify(!full.placeholder.visible);

        const row = rowOf(full, list, "b");
        compare(row.name, "Calls");
        compare(row.runButton.text, "Run");
        compare(row.runButton.Accessible.name, "Run Calls");
        FakeEngine.clearCalls();
        mouseClick(row.runButton);
        tryCompare(row.runButton, "text", "Stop");
        // The popup is open, so the running session's levels are asked for
        // as well, whenever the answer comes.
        compare(FakeEngine.calls.filter(call => !call.startsWith("Share ")), ["OpenSession b", "Start b"]);

        mouseClick(row.runButton);
        tryCompare(row.runButton, "text", "Run");
        compare(FakeEngine.calls[FakeEngine.calls.length - 1], "Stop b");
    }

    function test_aRunningSessionShowsHowLoudItIs() {
        FakeEngine.addSession("a", "Radio", "running");
        const list = readyList();
        const full = makeFull(list);
        const row = rowOf(full, list, "a");
        FakeEngine.publishPeak("a", 0.5);
        tryVerify(() => row.levelBar.fill > 0.8, 2000, "the bar did not show -6 dB: " + row.levelBar.fill);
        compare(row.levelBar.Accessible.name, "Level of Radio");
        tryCompare(row.levelBar.Accessible, "description", "-6 dB");

        FakeEngine.setState("a", "idle");
        tryCompare(row.levelBar, "fill", 0);
        tryCompare(row.levelBar.Accessible, "description", "Idle");
    }

    function test_aRowSaysItRecords() {
        FakeEngine.addSession("a", "Radio", "running");
        const list = readyList();
        const full = makeFull(list);
        const row = rowOf(full, list, "a");
        verify(!row.recordingMark.visible);
        FakeEngine.startRecording("a", "/tmp/one.flac");
        tryVerify(() => row.recordingMark.visible, 2000);
        compare(row.recordingMark.Accessible.name, "Recording");
    }

    function test_aSessionThatCouldNotRunIsNamed() {
        FakeEngine.addSession("a", "Radio", "idle");
        FakeEngine.refuseStart("a");
        const list = readyList();
        const full = makeFull(list);
        verify(!full.failureMessage.visible);
        mouseClick(rowOf(full, list, "a").runButton);
        tryVerify(() => full.failureMessage.visible, 2000, "nobody was told the session did not start");
        compare(full.failureMessage.text, "\"Radio\" could not be started. Open Reverblabr to see why.");
        list.watching = false;
        verify(!full.failureMessage.visible, "the failure outlived the popup");
    }

    function test_withNoSessionsThePopupSaysSo() {
        const list = readyList();
        const full = makeFull(list);
        tryVerify(() => full.placeholder.visible, 2000);
        compare(full.placeholder.text, "No saved sessions yet");
    }

    function test_withNoEngineThePopupSaysItIsNotInstalled() {
        FakeEngine.disappear();
        const list = createTemporaryObject(listComponent, testCase);
        const full = makeFull(list);
        tryVerify(() => full.placeholder.visible, 5000, "the popup did not say the engine is missing");
        compare(full.placeholder.text, "Reverblabr's background engine is not installed");
    }
}
