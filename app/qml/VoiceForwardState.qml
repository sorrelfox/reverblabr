// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

// What a Voice Forward's tile and card both read: its settings and what it
// publishes.
//
// **The words change at most four times a second**, so they can be read, and
// nothing here announces: a voice from a screen reader over a film is the
// wrong moment.
QtObject {
    id: reading

    /// The block this reads.
    property string blockId: ""

    readonly property VoiceForwardWords words: VoiceForwardWords {}

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(reading.blockId) : ({})
    readonly property real background: reading.params.background ?? 0
    readonly property real between: reading.params.betweenVoices ?? reading.words.defaultBetween
    readonly property bool holdLevel: reading.params.holdLevel ?? true
    /// The cut in use while the centre is quiet.
    readonly property real betweenActing: reading.words.betweenActing(reading.background, reading.between)

    readonly property var levels: Meters.frame >= 0 ? Meters.levels(reading.blockId) : ({})
    readonly property var values: reading.levels.values ?? []
    readonly property string watch: Meters.frame >= 0 ? Meters.watchState(reading.blockId) : "stopped"
    readonly property bool blockEnabled: SessionModel.paramRevision >= 0
        && (SessionModel.block(reading.blockId).enabled ?? true)
    readonly property bool watching: reading.watch === "watching" && reading.values.length > reading.words.limitValue
    /// **A block switched off in a running session is not run**, and its
    /// slots go on saying what they said last.
    readonly property bool current: reading.watching && reading.blockEnabled
    readonly property real latencyMs: reading.levels.latencyMs ?? 0

    readonly property real takenOutNow: reading.current ? reading.values[reading.words.takenOutValue] : 0
    readonly property int foundNow: reading.current ? Math.round(reading.values[reading.words.foundValue]) : reading.words.quiet
    readonly property real liftNow: reading.current ? reading.values[reading.words.liftValue] : 0
    readonly property real limitNow: reading.current ? reading.values[reading.words.limitValue] : 0

    /// What the words show, as of the last tick while it runs.
    property int heldFound: reading.words.quiet
    property real heldTakenOut: 0
    property real heldLift: 0
    property real heldLimit: 0

    readonly property int found: reading.current ? reading.heldFound : reading.words.quiet
    readonly property real takenOutDb: reading.current ? reading.heldTakenOut : 0
    readonly property real liftDb: reading.current ? reading.heldLift : 0
    readonly property real limitDb: reading.current ? reading.heldLimit : 0

    readonly property string kind: reading.words.kindOf(reading.watching, reading.blockEnabled, reading.background,
                                                        reading.found, reading.liftDb, reading.takenOutDb)

    readonly property Timer ticker: Timer {
        interval: 250
        repeat: true
        triggeredOnStart: true
        running: reading.current
        onTriggered: {
            reading.heldFound = reading.foundNow;
            reading.heldTakenOut = reading.takenOutNow;
            reading.heldLift = reading.liftNow;
            reading.heldLimit = reading.limitNow;
        }
    }
}
