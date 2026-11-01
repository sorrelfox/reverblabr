// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// **What a global shortcut does to the front session's blocks**: the
// session whose tab the main window shows, or showed last while it is
// hidden. Every block of the kind the shortcut is about is acted on, the
// way its own face would; a session with none, or no session at all, is
// left alone.
//
// - Input Switch: A or B, as its segmented choice sets it.
// - Time Shift: pause or play, 10 seconds back or forward, back to live, as
//   its transport does (TimeShiftState, which works from where the running
//   block says its head is).
// - Recorder: a new file, only for one that is recording, as its Split
//   button is only there then.
// - Speech Now: one sentence from all of them, "Call speaking, You quiet".
//   Nothing else says this unasked, since a sentence every few seconds would
//   be spoken over the talker.
QtObject {
    id: targets

    readonly property string inputSwitchType: "advanced.inputswitch"
    readonly property string timeShiftType: "advanced.timeshift"
    readonly property string recorderType: "output.recorder"
    readonly property string speechNowType: "meter.speechnow"
    /// What speaks a sentence to a screen reader: the window, set by Main.
    property Item speaker: null
    /// How far the Time Shift shortcuts jump (GlobalShortcuts::kJumpSeconds).
    readonly property int jumpSeconds: 10

    readonly property SpeechNowWords speechWords: SpeechNowWords {}

    readonly property Component timeShiftReading: Component {
        TimeShiftState {}
    }

    /// The front session's blocks of \a type.
    function blocksOf(type: string): list<string> {
        return SessionModel.blockIds.filter(id => SessionModel.blockType(id) === type);
    }

    /// Does what \a actionId says to every block of its kind in the front
    /// session. Returns how many blocks it acted on.
    function act(actionId: string): int {
        if (SessionModel.sessionId === "") {
            return 0;
        }
        switch (actionId) {
        case "inputSwitch.a":
        case "inputSwitch.b":
            return targets.switchTo(actionId === "inputSwitch.a" ? "a" : "b");
        case "timeShift.pause":
        case "timeShift.back":
        case "timeShift.forward":
        case "timeShift.live":
            return targets.moveTimeShifts(actionId);
        case "recorder.split":
            return targets.splitRecorders();
        case "speechNow.say":
            return targets.sayWhoIsSpeaking();
        }
        return 0;
    }

    function switchTo(input: string): int {
        const blocks = targets.blocksOf(targets.inputSwitchType);
        for (const blockId of blocks) {
            SessionModel.setParam(blockId, "input", input);
        }
        return blocks.length;
    }

    function moveTimeShifts(actionId: string): int {
        let moved = 0;
        for (const blockId of targets.blocksOf(targets.timeShiftType)) {
            const reading = targets.timeShiftReading.createObject(targets, { blockId: blockId });
            if (targets.moveHead(reading, actionId)) {
                ++moved;
            }
            reading.destroy();
        }
        return moved;
    }

    /// Moves one Time Shift's head as \a actionId says, through its
    /// transport's own \a reading. **Only a running block has a head to
    /// move**, as its buttons are only enabled then.
    function moveHead(reading: var, actionId: string): bool {
        if (!reading.live) {
            return false;
        }
        if (actionId === "timeShift.pause") {
            reading.togglePause();
        } else if (actionId === "timeShift.back") {
            reading.jumpBy(-targets.jumpSeconds);
        } else if (actionId === "timeShift.forward") {
            // Forward past live is live, as the face's button is.
            if (!reading.atLive) {
                reading.jumpBy(targets.jumpSeconds);
            }
        } else {
            reading.goLive();
        }
        return true;
    }

    /// One sentence, past the Announcer's gap, since it was asked for.
    function sayWhoIsSpeaking(): int {
        const blocks = targets.blocksOf(targets.speechNowType);
        if (blocks.length === 0) {
            return 0;
        }
        const parts = blocks.map(blockId => speechWords.spokenPart(targets.nameOf(blockId), speechWords.kindOf(
            Meters.watchState(blockId), Meters.levels(blockId).enabled ?? true,
            (Meters.levels(blockId).values ?? [0])[0])));
        if (targets.speaker !== null) {
            Announcer.sayNow(targets.speaker, parts.join(KI18n.i18nc("@info between the parts of a spoken list", ", ")));
        }
        return blocks.length;
    }

    function nameOf(blockId: string): string {
        const name = SessionModel.block(blockId).nickname ?? "";
        return name !== "" ? name : BlockPresentation.name(targets.speechNowType);
    }

    function splitRecorders(): int {
        let split = 0;
        for (const blockId of targets.blocksOf(targets.recorderType)) {
            if (RecordingStatus.state(SessionModel.sessionId, blockId) === "recording") {
                RecordingStatus.split(SessionModel.sessionId, blockId);
                ++split;
            }
        }
        return split;
    }
}
