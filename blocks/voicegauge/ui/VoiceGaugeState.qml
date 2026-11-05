// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

// What a Voice Gauge's tile and card both read: its settings and what it
// publishes, on the words the two share.
QtObject {
    id: reading

    /// The block this reads.
    property string blockId: ""

    /// Which of the block's published values is which (VoiceGauge).
    readonly property int stateValue: 0
    readonly property int readingValue: 1
    readonly property int wholeValue: 2
    readonly property int marksValue: 3

    readonly property VoiceGaugeWords words: VoiceGaugeWords {}

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(reading.blockId) : ({})
    readonly property var levels: Meters.frame >= 0 ? Meters.levels(reading.blockId) : ({})
    readonly property var values: reading.levels.values ?? []
    readonly property string watch: Meters.frame >= 0 ? Meters.watchState(reading.blockId) : "stopped"
    readonly property bool watching: reading.watch === "watching"

    /// Whether the block is switched on, as the session has it.
    readonly property bool blockEnabled: SessionModel.paramRevision >= 0
        && (SessionModel.block(reading.blockId).enabled ?? true)
    /// Whether what it published is its word now. **A block switched off in a
    /// running session is not run**, and its slots go on saying what they said
    /// last while the frame moves, so `watching` alone does not tell.
    readonly property bool current: reading.watching && reading.blockEnabled
    readonly property real state: reading.values[reading.stateValue] ?? 0
    readonly property real level: reading.values[reading.readingValue] ?? reading.words.noReading
    /// The level over 20 s of voice, once there is one.
    readonly property real wholeLevel: reading.values[reading.wholeValue] ?? reading.words.noReading
    readonly property bool wholeLevelReady: reading.current && reading.wholeLevel > reading.words.noReading
    readonly property int secondsCounted: reading.current ? reading.words.secondsOf(reading.state) : 0

    /// The marks the block goes by while it runs, and the ones it was given
    /// while it does not.
    readonly property var storedMarks: reading.words.moved(reading.params.upperMark ?? reading.words.notSet,
                                                           reading.params.lowerMark ?? reading.words.notSet, false)
    readonly property var marks: reading.current && reading.values.length > reading.marksValue
        ? reading.words.marksOf(reading.values[reading.marksValue]) : reading.storedMarks
    readonly property bool anyMark: reading.words.isSet(reading.marks.upper) || reading.words.isSet(reading.marks.lower)

    readonly property string chosenDetector: reading.params.detector ?? "model"
    /// What the block says of its detector (SpeechNowWords), or what it was
    /// asked for while nothing runs.
    readonly property int detector: reading.watching ? reading.words.detectorOf(reading.state)
                                                     : (reading.chosenDetector === "model" ? 0 : 1)

    readonly property string kind: reading.words.kindOf(reading.watch, reading.blockEnabled, reading.state, reading.level)
    readonly property string headline: reading.words.headline(reading.kind, reading.level)
    readonly property string line: reading.words.line(reading.kind, reading.state, reading.watch)
}
