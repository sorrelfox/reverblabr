// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

// What a Speech Now's tile and card both read: its settings and what it
// publishes, on the words the two share.
QtObject {
    id: reading

    /// The block this reads.
    property string blockId: ""

    /// Which of the block's published values is which (SpeechNow).
    readonly property int stateValue: 0
    readonly property int voiceValue: 1
    readonly property int detectorValue: 2

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(reading.blockId) : ({})
    readonly property var levels: Meters.frame >= 0 ? Meters.levels(reading.blockId) : ({})
    readonly property var values: reading.levels.values ?? []
    readonly property string watch: Meters.frame >= 0 ? Meters.watchState(reading.blockId) : "stopped"
    readonly property bool watching: reading.watch === "watching"

    /// Whether the block is switched on, as the session has it.
    readonly property bool blockEnabled: SessionModel.paramRevision >= 0
        && (SessionModel.block(reading.blockId).enabled ?? true)
    readonly property int state: Math.round(reading.values[reading.stateValue] ?? 0)
    readonly property real voice: reading.values[reading.voiceValue] ?? 0
    readonly property string chosenDetector: reading.params.detector ?? "model"
    /// What the block says of its detector, or what it was asked for while
    /// nothing runs.
    readonly property int detector: reading.watching
        ? Math.round(reading.values[reading.detectorValue] ?? 0)
        : (reading.chosenDetector === "model" ? reading.words.modelListening : words.levelChosen)
    readonly property string kind: reading.words.kindOf(reading.watch, reading.blockEnabled, reading.state)

    readonly property SpeechNowWords words: SpeechNowWords {}
}
