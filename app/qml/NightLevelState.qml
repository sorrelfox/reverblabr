// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

// What a Night Level's tile and card both read: its settings, what it
// publishes, and what the program in front of it says of itself.
//
// **The words change at most four times a second**, so they can be read.
QtObject {
    id: reading

    /// The block this reads.
    property string blockId: ""
    /// Whether this one writes the range down. Only the tile does.
    property bool keepsHistory: false

    readonly property NightLevelWords words: NightLevelWords {}

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(reading.blockId) : ({})
    readonly property real cut: reading.params.cut ?? 0
    readonly property real boost: reading.params.boost ?? 0
    readonly property string pivotFrom: reading.params.pivotFrom ?? "found"
    readonly property real typedPivot: reading.params.typedPivot ?? reading.words.defaultTyped
    readonly property bool anyAmount: reading.cut > 0 || reading.boost > 0

    readonly property var levels: Meters.frame >= 0 ? Meters.levels(reading.blockId) : ({})
    readonly property var values: reading.levels.values ?? []
    readonly property string watch: Meters.frame >= 0 ? Meters.watchState(reading.blockId) : "stopped"
    readonly property bool blockEnabled: SessionModel.paramRevision >= 0
        && (SessionModel.block(reading.blockId).enabled ?? true)
    readonly property bool watching: reading.watch === "watching" && reading.values.length > reading.words.stateValue
    /// **A block switched off in a running session is not run**, and its
    /// slots go on saying what they said last.
    readonly property bool current: reading.watching && reading.blockEnabled

    readonly property real gainDb: reading.current ? reading.values[reading.words.gainValue] : 0
    readonly property real foundLufs: reading.current ? reading.values[reading.words.foundValue] : reading.words.noLevel
    readonly property real readingLufs: reading.current ? reading.values[reading.words.readingValue] : reading.words.noLevel
    readonly property real stateNumber: reading.current ? reading.values[reading.words.stateValue] : 0
    readonly property int tracking: reading.words.trackingOf(reading.stateNumber)
    readonly property int seconds: reading.words.secondsOf(reading.stateNumber)
    readonly property int detector: reading.words.detectorOf(reading.stateNumber)
    /// The level the law goes by, or noLevel.
    readonly property real pivotLufs: reading.pivotFrom === "typed" ? reading.typedPivot : reading.foundLufs
    readonly property bool hasPivot: reading.pivotLufs > reading.words.noLevel
    readonly property bool held: reading.tracking === 1 && reading.seconds >= reading.words.heldFrom
    readonly property real ceilingDb: reading.words.ceilingFor(reading.pivotLufs, reading.cut, reading.boost)

    readonly property string kindNow: reading.words.kindOf(reading.watching, reading.blockEnabled, reading.cut,
                                                           reading.boost, reading.stateNumber, reading.gainDb)

    /// What the words show, as of the last tick while it runs.
    property string heldKind: "stopped"
    property real heldGainDb: 0
    readonly property string kind: reading.current ? reading.heldKind : reading.kindNow
    readonly property real shownGainDb: reading.current ? reading.heldGainDb : 0
    readonly property bool running: reading.kind !== "stopped" && reading.kind !== "off"
    readonly property string levelLine: !reading.current || reading.kind === "notSet" ? ""
        : reading.words.levelLine(reading.tracking, reading.foundLufs, reading.typedPivot, reading.seconds)

    /// A level was found, or let go. **Only that:** a press of Found moves
    /// tracking too, and a session or a block coming back starts afresh.
    signal levelFound(lufs: real)
    signal levelLost

    /// What tracking was at the last tick while it runs, or -1.
    property int heldTracking: -1

    readonly property Timer ticker: Timer {
        interval: 250
        repeat: true
        triggeredOnStart: true
        running: reading.current
        onRunningChanged: {
            if (!running) {
                reading.heldTracking = -1;
            }
        }
        onTriggered: {
            const was = reading.heldTracking;
            reading.heldTracking = reading.tracking;
            if (was === 0 && reading.tracking === 1) {
                reading.levelFound(reading.foundLufs);
            } else if (was === 1 && reading.tracking === 0) {
                reading.levelLost();
            }
            reading.heldKind = reading.kindNow;
            reading.heldGainDb = reading.gainDb;
            if (reading.keepsHistory) {
                AutoLevelHistory.note(reading.blockId, reading.gainDb, Date.now());
            }
        }
    }

    /// A film player saying Movie is what this block is for, so only Music.
    readonly property LaneProgram program: LaneProgram {
        blockId: reading.blockId
        roles: ["Music"]
    }
    readonly property bool declaredMusic: reading.program.declared
}
