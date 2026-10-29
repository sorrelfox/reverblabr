// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A meter, as a screen reader has it: a progress bar called after its
// block, whose value is the loudest channel in whole decibels, brought up to
// date twice a second rather than sixty times; and a clip said out loud once,
// not on every hit, and not by every meter at the same time.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "MeterAccessibility"
    when: windowShown

    property string sessionId: ""

    // **In a window of its own**: a meter nobody can see is not read out,
    // and an item under the test case is not shown.
    Component {
        id: meterWindow

        Window {
            property alias meter: inner
            property alias blockId: inner.blockId

            width: 80
            height: 200
            visible: true

            LevelMeter {
                id: inner

                blockId: ""
                width: 40
                height: 160
            }
        }
    }

    function meterFor(block) {
        return createTemporaryObject(meterWindow, null, {blockId: block}).meter;
    }

    function initTestCase() {
        TestAccessibility.setActive(true);
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Meters for a screen reader");
        verify(testCase.sessionId !== "", "the mock opened a session");
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function cleanupTestCase() {
        TestAccessibility.setActive(false);
    }

    function init() {
        SessionModel.sessionId = testCase.sessionId;
        Announcer.reset();
        Meters.watch(testCase.sessionId);
        TestEngine.start(testCase.sessionId);
        tryVerify(() => Meters.live, 1000, "a running session published nothing");
    }

    function cleanup() {
        TestEngine.clearFakeLevels(testCase.sessionId);
        TestEngine.stop(testCase.sessionId);
        Meters.watch("");
        SessionModel.removeBlocks(SessionModel.blockIds);
    }

    function test_aMeterIsAProgressBarWithTheLoudestLevelInWholeDecibels() {
        const block = SessionModel.addBlock("effect.volume", Qt.point(0, 0));
        const meter = testCase.meterFor(block);
        const said = TestAccessibility.describe(meter);
        compare(said.role, Accessible.ProgressBar);
        compare(said.name, "Volume level");

        TestEngine.setFakeLevel(testCase.sessionId, block, 0.5);
        // Half of full scale is -6.02 dBFS, read out as -6.
        tryCompare(meter, "value", -6, 1500, "the meter's value is not the loudest channel in whole decibels");
        compare(meter.from, MeterScale.floorDb);
        compare(meter.to, MeterScale.topDb);
        compare(TestAccessibility.describe(meter).description, "-6 dB");
    }

    function test_theValueIsBroughtUpToDateTwiceASecondNotEveryFrame() {
        const block = SessionModel.addBlock("effect.volume", Qt.point(0, 0));
        const meter = testCase.meterFor(block);
        TestEngine.setFakeLevel(testCase.sessionId, block, 0.5);
        tryCompare(meter, "value", -6, 1500);
        let changes = 0;
        meter.valueChanged.connect(() => ++changes);
        // A level that moves every frame for two seconds, never the same
        // whole decibel twice in a row, so each reading that is taken
        // differs from the one before.
        const started = Date.now();
        let step = 0;
        while (Date.now() - started < 2000) {
            TestEngine.setFakeLevel(testCase.sessionId, block, Math.pow(10, -(1 + (step++ % 40)) / 20));
            wait(20);
        }
        verify(changes <= 5, "the value changed " + changes + " times in two seconds");
        verify(changes >= 2, "the value did not follow the level at all (" + changes + " changes)");
    }

    function test_aClipIsSaidOnceAndNotOnEveryHit() {
        const block = SessionModel.addBlock("effect.volume", Qt.point(0, 0));
        const meter = testCase.meterFor(block);
        TestEngine.setFakeLevel(testCase.sessionId, block, 1.0);
        tryCompare(Announcer, "last", "Volume clipped", 1000, "the clip was not said");
        const first = Announcer.lastAt;

        // It clips again straight away: nothing more is said. (A clip light
        // holds until somebody clicks it, which is what puts it out.)
        TestEngine.setFakeLevel(testCase.sessionId, block, 0.5);
        Meters.resetClip(block);
        tryVerify(() => !meter.clipped, 1000, "the clip light did not go out");
        TestEngine.setFakeLevel(testCase.sessionId, block, 1.0);
        tryVerify(() => meter.clipped, 1000);
        wait(100);
        compare(Announcer.lastAt, first, "a second clip within five seconds was said again");
    }

    function test_twentyMetersClippingAtOnceSayOneThing() {
        let meters = [];
        let blocks = [];
        for (let index = 0; index < 20; ++index) {
            const block = SessionModel.addBlock("effect.volume", Qt.point(index * 300, 0));
            blocks.push(block);
            meters.push(testCase.meterFor(block));
        }
        let said = 0;
        const counter = () => ++said;
        Announcer.lastAtChanged.connect(counter);
        for (const block of blocks) {
            TestEngine.setFakeLevel(testCase.sessionId, block, 1.0);
        }
        tryVerify(() => meters.every(meter => meter.clipped), 1000);
        wait(100);
        Announcer.lastAtChanged.disconnect(counter);
        compare(said, 1, "twenty meters clipping at once said " + said + " things");
    }
}
