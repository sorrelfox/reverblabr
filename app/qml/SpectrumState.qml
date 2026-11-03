// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

// What a Spectrum's tile and card both read: its settings, as the session has
// them, whatever undid or changed them.
QtObject {
    id: reading

    /// The block this reads.
    property string blockId: ""

    readonly property SpectrumWords words: SpectrumWords {}

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(reading.blockId) : ({})
    readonly property string refresh: reading.params.refresh ?? "hz30"
    readonly property real refreshHz: reading.words.hertzOf(reading.refresh)
    readonly property string motion: reading.params.motion ?? "realTime"
    readonly property string bars: reading.params.bars ?? "bars64"
    readonly property string scale: reading.params.scale ?? "logarithmic"
    readonly property string fftSize: reading.params.fftSize ?? "fft4096"
    readonly property string floor: reading.params.floor ?? "db80"
    readonly property string tilt: reading.params.tilt ?? "tilt0"
    readonly property bool peaks: reading.params.peaks !== false
    readonly property string channels: reading.params.channels ?? "mix"
    readonly property bool hold: reading.params.hold === true
    readonly property bool grid: reading.params.grid !== false
    readonly property string style: reading.params.style ?? "solid"
}
