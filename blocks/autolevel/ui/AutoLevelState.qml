// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

// Where an Auto Level stands, worked out once for its face and its panel:
// its settings, what the running block says about itself, and whether the
// program in front of it says it is playing music.
//
// **A stopped session publishes nothing**, so `live` is whether values arrive.
//
// **The words change at most four times a second**, so they can be read: the
// numbers arrive thirty times. The ticker takes what they say now.
QtObject {
    id: reading

    /// The block this is about.
    required property string blockId
    /// Whether this one writes the range down. Only the tile does: it is
    /// there for as long as the block is.
    property bool keepsHistory: false

    readonly property AutoLevelWords words: AutoLevelWords {}

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(reading.blockId) : ({})
    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(reading.blockId).enabled ?? true)
    /// The settings, as AutoLevelWords' rule takes them.
    readonly property var settings: ({
        blockEnabled: reading.blockEnabled,
        boostOn: reading.params.boost ?? true,
        cutOn: reading.params.cut ?? false,
        target: reading.params.target ?? reading.words.defaultTarget,
        maximumBoost: reading.params.maximumBoost ?? reading.words.defaultMaximum,
        maximumCut: reading.params.maximumCut ?? reading.words.defaultMaximumCut,
        band: reading.params.band ?? 0
    })
    readonly property bool anyOn: reading.settings.boostOn || reading.settings.cutOn

    readonly property var levels: Meters.frame >= 0 ? Meters.levels(reading.blockId) : ({})
    readonly property var values: reading.levels.values ?? []
    readonly property bool live: Meters.live && reading.blockEnabled && reading.values.length > reading.words.pullValue
    readonly property real gainDb: reading.live ? reading.values[reading.words.boostValue] : 0
    readonly property real readingLufs: reading.live ? reading.values[reading.words.loudnessValue] : 0
    readonly property real pullDb: reading.live ? reading.values[reading.words.pullValue] : 0

    readonly property string stateNow: reading.words.stateOf(reading.settings, reading.live, reading.gainDb,
                                                             reading.readingLufs, reading.pullDb)

    /// What the words show, as of the last tick while it runs.
    property string heldState: "stopped"
    property real heldGainDb: 0
    property real heldPullDb: 0
    readonly property string state: reading.live ? reading.heldState : reading.stateNow
    readonly property real shownGainDb: reading.live ? reading.heldGainDb : 0
    readonly property real shownPullDb: reading.live ? reading.heldPullDb : 0

    readonly property Timer ticker: Timer {
        interval: 250
        repeat: true
        triggeredOnStart: true
        running: reading.live
        onTriggered: {
            reading.heldState = reading.stateNow;
            reading.heldGainDb = reading.gainDb;
            reading.heldPullDb = reading.pullDb;
            if (reading.keepsHistory) {
                AutoLevelHistory.note(reading.blockId, reading.gainDb, Date.now());
            }
        }
    }

    /// Whether a program that feeds this block says it plays music or a film.
    readonly property LaneProgram program: LaneProgram {
        blockId: reading.blockId
        roles: ["Music", "Movie"]
    }
    readonly property bool declaredMusic: reading.program.declared
}
