// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

// What one Sound Side shows, for a tile, a card or the strip. **Reads
// SoundSideReadings and works nothing out**, so all three say the same.
QtObject {
    id: reading

    /// The block this reads, and the session it is in: the one in front
    /// unless told otherwise, as the strip's may not be.
    property string blockId: ""
    property string sessionId: SessionModel.sessionId

    readonly property SoundSideWords words: SoundSideWords {}

    readonly property var shown: SoundSideReadings.revision >= 0
        ? SoundSideReadings.shown(reading.sessionId, reading.blockId) : ({})
    readonly property string kind: reading.shown.kind ?? "stopped"
    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(reading.blockId) : ({})
    readonly property real gate: reading.params.gateDb ?? -50

    readonly property string word: reading.words.tileWord(reading.kind)
    readonly property string caption: reading.words.caption(reading.shown)

    /// What this has told SoundSideReadings it shows.
    property string heldSession: ""
    property string heldBlock: ""

    function follow() {
        if (reading.heldSession === reading.sessionId && reading.heldBlock === reading.blockId) {
            return;
        }
        SoundSideReadings.release(reading.heldSession, reading.heldBlock);
        SoundSideReadings.hold(reading.sessionId, reading.blockId);
        reading.heldSession = reading.sessionId;
        reading.heldBlock = reading.blockId;
    }

    onBlockIdChanged: reading.follow()
    onSessionIdChanged: reading.follow()
    Component.onCompleted: reading.follow()
    Component.onDestruction: SoundSideReadings.release(reading.heldSession, reading.heldBlock)
}
