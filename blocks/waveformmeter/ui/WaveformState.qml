// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

// What a Waveform's tile and card both read: its settings, as the session has
// them, whatever undid or changed them.
QtObject {
    id: reading

    /// The block this reads.
    property string blockId: ""

    readonly property WaveformWords words: WaveformWords {}

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(reading.blockId) : ({})
    readonly property string refresh: reading.params.refresh ?? "hz30"
    readonly property real refreshHz: reading.words.hertzOf(reading.refresh)
    readonly property string motion: reading.params.motion ?? "realTime"
    readonly property string style: reading.params.style ?? "line"
    readonly property real spanMs: reading.params.spanMs ?? 200
    readonly property real gainDb: reading.params.gainDb ?? 0
    readonly property bool autoFit: reading.params.autoFit === true
    readonly property string channels: reading.params.channels ?? "both"
    readonly property bool trigger: reading.params.trigger === true
    readonly property bool hold: reading.params.hold === true
    readonly property bool grid: reading.params.grid !== false
    readonly property string scale: reading.params.scale ?? "linear"
}
