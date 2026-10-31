// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

// Where a One Ear Mix stands, worked out once for its face and its panel:
// its settings, and what the running block says about itself.
//
// **A stopped session publishes nothing**, so `live` is whether values
// arrive at all.
QtObject {
    id: reading

    /// The block this is about.
    required property string blockId

    /// Which of the block's published values is which (OneEarMix::
    /// kStateValue, kFoldLoudnessValue, kMonoLoudnessValue, kReadingValue).
    readonly property int stateValue: 0
    readonly property int foldValue: 1
    readonly property int monoValue: 2
    readonly property int readingValue: 3
    /// kReading and kRead.
    readonly property int measuring: 1
    readonly property int measured: 2

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(reading.blockId) : ({})
    readonly property string sendTo: reading.params.sendTo ?? "none"
    readonly property string hear: reading.params.hear ?? "fold"
    readonly property real lowerDb: reading.params.lowerDb ?? 0
    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(reading.blockId).enabled ?? true)

    readonly property var levels: Meters.frame >= 0 ? Meters.levels(reading.blockId) : ({})
    readonly property var values: reading.levels.values ?? []
    readonly property bool live: Meters.live && reading.values.length > reading.readingValue

    readonly property bool acting: reading.sendTo !== "none"
    readonly property bool arriving: reading.live && reading.acting && reading.values[reading.stateValue] === 1
    readonly property bool lowered: reading.acting && reading.hear === "fold" && reading.lowerDb > 0
    readonly property int stage: reading.live ? reading.values[reading.readingValue] : 0
}
