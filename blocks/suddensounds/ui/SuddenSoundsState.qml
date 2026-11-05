// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

// What a Sudden Sounds' tile and card both read: its settings and what it
// publishes. The block holds its reading for a second, so a cut too short to
// fall between two reads is still here, and nothing here needs a timer.
// Nothing announces either: a voice over every knock is the wrong answer.
QtObject {
    id: reading

    /// The block this reads.
    property string blockId: ""

    readonly property SuddenSoundsWords words: SuddenSoundsWords {}

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(reading.blockId) : ({})
    readonly property string strength: reading.params.strength ?? "off"
    readonly property string ear: reading.params.ear ?? "left"

    readonly property var levels: Meters.frame >= 0 ? Meters.levels(reading.blockId) : ({})
    readonly property var values: reading.levels.values ?? []
    readonly property string watch: Meters.frame >= 0 ? Meters.watchState(reading.blockId) : "stopped"
    readonly property bool blockEnabled: SessionModel.paramRevision >= 0
        && (SessionModel.block(reading.blockId).enabled ?? true)
    readonly property bool watching: reading.watch === "watching" && reading.values.length > reading.words.countValue
    /// **A block switched off in a running session is not run**, and its
    /// slots go on saying what they said last.
    readonly property bool current: reading.watching && reading.blockEnabled

    readonly property real softenedDb: reading.current ? reading.values[reading.words.softenedValue] : 0
    readonly property int count: reading.current ? Math.round(reading.values[reading.words.countValue]) : 0

    readonly property string kind: reading.words.kindOf(reading.watching, reading.blockEnabled, reading.strength,
                                                        reading.softenedDb)
}
