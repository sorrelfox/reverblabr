// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick

// What a Voice Compressor's tile and card both read: its settings, and what
// it took off. The words move four times a second, which a person can read;
// the bars on the card use the live numbers.
QtObject {
    id: reading

    /// The block this reads.
    property string blockId: ""

    readonly property VoiceCompressorWords words: VoiceCompressorWords {}

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(reading.blockId) : ({})
    readonly property real compression: reading.params.compression ?? reading.words.defaultCompression
    readonly property real gate: reading.params.gate ?? reading.words.gateOff
    readonly property real ceiling: reading.params.ceiling ?? reading.words.defaultCeiling

    readonly property var levels: Meters.frame >= 0 ? Meters.levels(reading.blockId) : ({})
    readonly property var values: reading.levels.values ?? []
    readonly property string watch: Meters.frame >= 0 ? Meters.watchState(reading.blockId) : "stopped"
    readonly property bool blockEnabled: SessionModel.paramRevision >= 0
        && (SessionModel.block(reading.blockId).enabled ?? true)
    readonly property bool watching: reading.watch === "watching"
        && reading.values.length > reading.words.inputValue
    /// **A block switched off in a running session is not run**, and its
    /// slots go on saying what they said last.
    readonly property bool current: reading.watching && reading.blockEnabled

    readonly property real compressedDb: reading.current ? reading.values[reading.words.compressedValue] : 0
    readonly property real gatedDb: reading.current ? reading.values[reading.words.gatedValue] : 0
    readonly property real limitedDb: reading.current ? reading.values[reading.words.limitedValue] : 0
    readonly property real inputDb: reading.current ? reading.values[reading.words.inputValue] : -120
    /// What the compressor and the gate take off together.
    readonly property real totalDb: reading.compressedDb + reading.gatedDb

    readonly property string kindNow: reading.words.kindOf(reading.watching, reading.blockEnabled, reading.compressedDb,
                                                           reading.gatedDb, reading.limitedDb, reading.inputDb)

    /// What the words show, as of the last tick while it runs.
    property string heldKind: "stopped"
    property real heldTotalDb: 0
    readonly property string kind: reading.current ? reading.heldKind : reading.kindNow
    readonly property real shownTotalDb: reading.current ? reading.heldTotalDb : 0
    /// The most taken off since the last press of Reset.
    property real mostDb: 0

    /// **Noted as the reading moves**, not on the tick: the quick release is gone
    /// well inside a quarter of a second. It sees what the meter frames show,
    /// so a peak that lies wholly between two frames can still be missed.
    onTotalDbChanged: reading.mostDb = Math.max(reading.mostDb, reading.totalDb)

    function resetMost() {
        reading.mostDb = 0;
    }

    readonly property Timer ticker: Timer {
        interval: 250
        repeat: true
        triggeredOnStart: true
        running: reading.current
        onTriggered: {
            reading.heldKind = reading.kindNow;
            reading.heldTotalDb = reading.totalDb;
            reading.mostDb = Math.max(reading.mostDb, reading.totalDb);
        }
    }
}
