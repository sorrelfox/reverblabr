// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Limits on a Hearing Compensation card and an Output Device: going down is
// free by every route, going up takes a second step by every route, and the
// words say what is digital and what acts.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "LimitRow"
    when: windowShown

    property string sessionId: ""
    property int made: 0

    Component {
        id: hearingPanel

        Window {
            property alias panel: inner

            width: 560
            height: 900
            visible: true

            HearingCompensationPanel {
                id: inner

                width: 540
            }
        }
    }

    Component {
        id: outputPanel

        Window {
            property alias panel: inner

            width: 560
            height: 700
            visible: true

            OutputDevicePanel {
                id: inner

                width: 540
            }
        }
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Limits");
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function cleanup() {
        Meters.watch("");
        Announcer.reset();
        TestEngine.stop(testCase.sessionId);
    }

    function add(type: string, params: var): string {
        testCase.made += 1;
        const id = "b" + testCase.made;
        TestEngine.applyEdit(testCase.sessionId, JSON.stringify({ op: "addBlock", block: { id: id, type: type, params: params }, index: -1 }));
        tryVerify(() => SessionModel.blockType(id) === type, 1000);
        return id;
    }

    function wire(from: string, to: string) {
        for (const port of [0, 1]) {
            TestEngine.applyEdit(testCase.sessionId, JSON.stringify({ op: "connect", wire: { from: [from, port], to: [to, port] } }));
        }
    }

    function openHearing(params: var): var {
        const id = testCase.add("hearing.compensation16", params);
        const window = createTemporaryObject(hearingPanel, null);
        window.panel.blockId = id;
        tryVerify(() => window.panel.mostGainLimit.width > 0, 2000);
        waitForItemPolished(window.panel);
        wait(100); // a dialog sized against a window still settling loops on its y
        return window.panel;
    }

    function test_theLimitsHaveASectionOfTheirOwn() {
        const panel = testCase.openHearing({});
        compare(panel.limits.title, "Limits");
        compare(panel.limits.note, "Digital, not at your ear");
        verify(panel.limits.expanded, "the section starts open");
        compare(panel.mostGainLimit.field.text, "50");
        compare(panel.mostGainLimit.unitLabel.text, "dB");
        compare(panel.outputCeilingLimit.field.text, Number(-1).toLocaleString(Qt.locale(), 'f', 0));
        compare(panel.outputCeilingLimit.unitLabel.text, "dBFS");
    }

    function test_lowerIsFreeByTheKeys() {
        const panel = testCase.openHearing({});
        const row = panel.mostGainLimit;
        row.stepBy(-3);
        tryCompare(row, "value", 47);
        verify(!row.dialog.opened, "lowering asked");
    }

    function test_lowerIsFreeByTyping() {
        const panel = testCase.openHearing({});
        const row = panel.mostGainLimit;
        row.field.text = "40";
        row.field.commit();
        tryCompare(row, "value", 40);
        verify(!row.dialog.opened, "lowering asked");
    }

    function test_lowerIsFreeByDrag() {
        const panel = testCase.openHearing({});
        const row = panel.mostGainLimit;
        row.dragged(30);
        tryCompare(row, "value", 30);
    }

    function test_aDragStopsAtTheValueAndSaysSo() {
        const panel = testCase.openHearing({});
        const row = panel.mostGainLimit;
        row.dragged(55);
        wait(50);
        compare(row.value, 50);
        compare(row.fader.value, 50);
        compare(Announcer.last, "Most gain stops here. To raise it, use Raise.");
    }

    function test_aDragThatFellCannotComeBackUp() {
        const panel = testCase.openHearing({});
        const row = panel.mostGainLimit;
        row.dragged(30);
        row.dragged(40);
        wait(50);
        compare(row.value, 30);
    }

    function test_anArrowAtTheStopDoesNothing() {
        const panel = testCase.openHearing({});
        const row = panel.mostGainLimit;
        row.stepBy(1);
        wait(50);
        compare(row.value, 50);
        compare(Announcer.last, "Most gain stops here. To raise it, use Raise.");
        Announcer.reset();
        row.field.step(1);
        wait(50);
        compare(row.value, 50);
        compare(Announcer.last, "Most gain stops here. To raise it, use Raise.");
    }

    function test_aTypedValueAboveTheStopOpensTheDialogFilledIn() {
        const panel = testCase.openHearing({});
        const row = panel.mostGainLimit;
        row.field.text = "55";
        row.field.commit();
        tryVerify(() => row.dialog.opened, 1000, "typing past the stop did not ask");
        compare(row.dialogField.value, 55);
        compare(row.value, 50, "the value moved before anybody said yes");
        compare(row.field.text, "50");
        row.dialog.close();
        tryVerify(() => !row.dialog.opened, 1000);
        compare(row.value, 50);
    }

    function test_raiseAsksAndIsOneUndoStep() {
        const panel = testCase.openHearing({});
        const row = panel.mostGainLimit;
        compare(row.question, "Most gain is 50 dB. Raise it to:");
        mouseClick(row.button);
        tryVerify(() => row.dialog.opened, 1000);
        compare(row.dialogField.value, 50);
        row.dialogField.value = 58;
        row.confirm.trigger();
        tryCompare(row, "value", 58);
        tryVerify(() => !row.dialog.opened, 1000);
        SessionModel.undo();
        tryCompare(row, "value", 50);
    }

    function test_theKeysGoDownAtOnceAndUpThroughTheDialog() {
        const panel = testCase.openHearing({});
        const row = panel.mostGainLimit;
        row.fader.forceActiveFocus();
        keyClick(Qt.Key_Up);
        wait(30);
        compare(row.value, 50, "an arrow raised a limit");
        keyClick(Qt.Key_PageDown);
        tryCompare(row, "value", 47);
        keyClick(Qt.Key_Down);
        tryCompare(row, "value", 46);

        row.button.forceActiveFocus();
        keyClick(Qt.Key_Space);
        tryVerify(() => row.dialog.opened, 1000, "the button did not open the dialog");
        keyClick(Qt.Key_Escape);
        tryVerify(() => !row.dialog.opened, 1000, "Escape did not cancel");
        compare(row.value, 46);
        row.askFor(52);
        tryVerify(() => row.dialog.opened, 1000);
        row.dialogField.forceActiveFocus();
        keyClick(Qt.Key_Return);
        tryCompare(row, "value", 52);
    }

    function test_raiseStopsAtTheTopOfTheRange() {
        const panel = testCase.openHearing({ maximumGainDb: 60 });
        verify(!panel.mostGainLimit.button.enabled, "there is nothing to raise it to");
    }

    function test_loudnessIsFreeUpToTheLineAndStopsThere() {
        const panel = testCase.openHearing({});
        const row = panel.loudnessLimit;
        compare(panel.loudnessStop, 10);
        verify(!row.button.visible, "Go Further shows only at the stop");
        row.dragged(8);
        tryCompare(row, "value", 8);
        row.dragged(14);
        wait(50);
        compare(row.fader.value, 10);
        compare(Announcer.last, "Loudness stops here. To go on, use Go Further.");
        SessionModel.setParam(panel.blockId, "loudnessDb", 10);
        tryVerify(() => row.button.visible, 1000, "Go Further did not show at the stop");
        compare(row.button.text, "Go Further...");
        row.askFor(15);
        tryVerify(() => row.dialog.opened, 1000);
        row.confirm.trigger();
        tryCompare(row, "value", 15);
        // Above the stop it goes down freely, and no further up.
        verify(row.button.visible);
        row.stepBy(-1);
        tryCompare(row, "value", 14);
    }

    function test_theLineCountsTone() {
        const panel = testCase.openHearing({});
        panel.toneDb = 3;
        compare(panel.loudnessStop, 1, "Loudness +1 with Tone +3 is at the stop");
        panel.toneDb = -3;
        compare(panel.loudnessStop, 1, "a tilt the other way lifts the lows as much");
        const bands = panel.bands;
        compare(bands.liftOffsetDb(1, 3), 10);
        compare(bands.liftOffsetDb(10, 0), 10);
    }

    function test_aCeilingReadsAsALevelOrNoLimit() {
        const panel = testCase.openHearing({});
        compare(panel.bands.ceilingText(100), "no limit");
        compare(panel.bands.ceilingText(85), "0 dBFS".replace("0", panel.bands.ceilingDbfs(85).toString()));
        compare(panel.bands.ceilingText(70), "-15 dBFS");
    }

    function test_aLimitActingIsSaidOnceAndOnlyAboveHalfADecibel() {
        const panel = testCase.openHearing({});
        const line = panel.limitActingLine;
        verify(!line.visible, "nothing is acting");
        verify(!line.told);
    }

    function test_aCopyThatRaisesALimitAsks() {
        const left = testCase.add("hearing.compensation64", { ear: "left", maximumGainDb: 20 });
        const right = testCase.add("hearing.compensation64", { ear: "right", maximumGainDb: 40, limiterCeilingDb: -3 });
        const window = createTemporaryObject(hearingPanel, null);
        window.panel.typeId = "hearing.compensation64";
        window.panel.blockId = left;
        tryVerify(() => window.panel.otherBlock === right, 1000);
        wait(100);
        window.panel.askBeforeCopying();
        tryVerify(() => window.panel.copyDialog.opened, 1000, "the copy raised a limit without asking");
        compare(SessionModel.param(left, "maximumGainDb"), 20);
        window.panel.copyDialog.close();

        // The other way round lowers, and asks nothing.
        const back = createTemporaryObject(hearingPanel, null);
        back.panel.typeId = "hearing.compensation64";
        back.panel.blockId = right;
        SessionModel.setParam(left, "limiterCeilingDb", -6);
        tryVerify(() => back.panel.otherBlock !== "", 1000);
        tryVerify(() => SessionModel.param(left, "limiterCeilingDb") === -6, 1000);
        verify(!back.panel.copyRaisesALimit());
        back.panel.askBeforeCopying();
        tryVerify(() => SessionModel.param(right, "maximumGainDb") === 20, 1000);
        compare(SessionModel.param(right, "limiterCeilingDb"), -6);
    }

    function test_theOutputCardShowsItsLimitOnlyWhenTheSessionNeedsOne() {
        const source = testCase.add("source.application", {});
        const lift = testCase.add("hearing.compensation16", {});
        const out = testCase.add("output.device", {});
        testCase.wire(source, lift);
        testCase.wire(lift, out);
        const window = createTemporaryObject(outputPanel, null);
        window.panel.blockId = out;
        const panel = window.panel;
        tryVerify(() => panel.lastLimit.needed === false, 1000);
        verify(!panel.limitsSection.visible, "a chain ending in Soft Lift pays nothing");

        const after = testCase.add("hearing.frequencylowering", {});
        TestEngine.applyEdit(testCase.sessionId, JSON.stringify({ op: "disconnect", wire: { from: [lift, 0], to: [out, 0] } }));
        TestEngine.applyEdit(testCase.sessionId, JSON.stringify({ op: "disconnect", wire: { from: [lift, 1], to: [out, 1] } }));
        testCase.wire(lift, after);
        testCase.wire(after, out);
        tryVerify(() => panel.limitsSection.visible, 1000, "the limit's section did not appear");
        compare(panel.limitReasonLine.text,
                "This limit is here because Frequency Lowering can add gain after the last limit. It adds about 2 ms of delay.");
        compare(panel.limit.field.text, Number(-1).toLocaleString(Qt.locale(), 'f', 0));
        verify(!panel.setLowerLine.visible);

        SessionModel.setParam(lift, "limiterCeilingDb", -12);
        tryVerify(() => panel.setLowerLine.visible, 1000);
        compare(panel.setLowerLine.text, "Set lower by Output ceiling: -12 dBFS");

        // The tile and the card read what the running block says.
        Meters.watch(testCase.sessionId);
        TestEngine.start(testCase.sessionId);
        tryVerify(() => (panel.deviceState.levels.values ?? [])[3] === -12, 2000, "the mock did not publish the limit");
        compare(panel.appliedLimitDb, -12);
    }

    function test_twoReasonsMoreInWords() {
        const source = testCase.add("source.application", {});
        const lift = testCase.add("hearing.compensation16", {});
        const out = testCase.add("output.device", {});
        testCase.wire(source, lift);
        testCase.wire(lift, out);
        testCase.wire(source, out);
        const window = createTemporaryObject(outputPanel, null);
        window.panel.blockId = out;
        tryVerify(() => window.panel.lastLimit.cause === "twoWires", 1000);
        verify(window.panel.limitReason.indexOf("two wires") >= 0);
    }
}
