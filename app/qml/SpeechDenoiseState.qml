// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// What a Speech Denoise block is doing, worked out once for its face
// and its panel: whether it hears a voice, and whether it can run at all —
// the model only works at 48 kHz, and at another rate the block passes the
// sound through and says so.
QtObject {
    id: reading

    /// The block this is about.
    required property string blockId

    /// Which of the block's published values is which
    /// (SpeechDenoise::kStateValue, kVoiceValue, kGateValue), and what the
    /// state's numbers mean.
    readonly property int stateValue: 0
    readonly property int voiceValue: 1
    readonly property int gateValue: 2
    readonly property int wrongRate: 2

    readonly property var levels: Meters.frame >= 0 ? Meters.levels(reading.blockId) : ({})
    readonly property var values: reading.levels.values ?? []
    /// Whether the running block is publishing.
    readonly property bool live: Meters.live && reading.values.length > reading.gateValue

    readonly property bool atWrongRate: reading.live && Math.round(reading.values[reading.stateValue]) === reading.wrongRate
    readonly property bool hearsSpeech: reading.live && !reading.atWrongRate && reading.values[reading.voiceValue] >= 0.5

    /// The model, as the tile names it.
    readonly property string model: KI18n.i18nc("@info the speech model a Speech Denoise block runs, and its rate",
                                                "RNNoise · 48 kHz")
    /// Where it stands in a few words.
    readonly property string status: reading.atWrongRate
        ? KI18n.i18nc("@info a Speech Denoise block in a session at another rate than 48 kHz", "Only works at 48 kHz")
        : (reading.hearsSpeech ? KI18n.i18nc("@info a Speech Denoise block hears a voice", "Speech detected")
                               : KI18n.i18nc("@info a Speech Denoise block hears no voice", "No speech detected"))
}
