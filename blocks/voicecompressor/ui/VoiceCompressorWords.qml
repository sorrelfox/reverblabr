// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import org.kde.ki18n

// The Voice Compressor's numbers and words. **Numbers the block holds are
// written down twice**, here and in
// blocks/voicecompressor/voicecompressor.h. A test holds them against the
// catalogue, and the curve against the block's table.
QtObject {
    id: words

    readonly property AutoLevelWords common: AutoLevelWords {}

    readonly property real lowestCompression: -36
    readonly property real defaultCompression: -12
    readonly property real lowestCeiling: -36
    readonly property real defaultCeiling: -6
    readonly property real gateOff: -80

    /// Which of the block's published values is which (VoiceCompressor).
    readonly property int compressedValue: 0
    readonly property int gatedValue: 1
    readonly property int limitedValue: 2
    readonly property int inputValue: 3

    /// Under this a reading is no work to speak of, and under this level no
    /// sound at all.
    readonly property real workingDb: 0.5
    readonly property real silentDb: -100

    /// The presets the choice names, and what each stands for.
    readonly property var presets: [{ id: "soft-voice", compression: -20, gate: -60, ceiling: -6 },
                                    { id: "speech", compression: -14, gate: -55, ceiling: -6 },
                                    { id: "strong-voice", compression: -8, gate: -50, ceiling: -6 },
                                    { id: "singing", compression: -12, gate: -80, ceiling: -3 }]

    // --- the block's law (VoiceCompressor) ---

    function ratioAt(compression: real): real {
        return 2 + 4 * compression / -36;
    }

    function compressorDb(level: real, compression: real): real {
        const slope = 1 - 1 / ratioAt(compression);
        const over = level - compression;
        if (over >= 6) {
            return -slope * over;
        }
        return over > -6 ? -slope * (over + 6) * (over + 6) / 24 : 0;
    }

    function gateDb(level: real, gate: real): real {
        if (gate <= words.gateOff) {
            return 0;
        }
        const under = gate - level;
        if (under <= -6) {
            return 0;
        }
        return -Math.min(under < 6 ? (under + 6) * (under + 6) / 24 : under, 24);
    }

    function makeupDb(compression: real, ceiling: real): real {
        const push = Math.max(0, Math.min(1, 1 + ceiling / 12));
        return -compressorDb(0, compression) * (1 + 0.25 * push);
    }

    function outputDb(level: real, compression: real, ceiling: real, gate: real): real {
        const gated = level + gateDb(level, gate);
        return Math.min(ceiling, gated + compressorDb(gated, compression) + makeupDb(compression, ceiling) + ceiling);
    }

    /// The curve, as points of a level in and out, from -80 dBFS to full scale.
    function curve(compression: real, ceiling: real, gate: real): var {
        const points = [];
        for (let level = -80; level <= 0; level += 1) {
            points.push({ x: level, y: outputDb(level, compression, ceiling, gate) });
        }
        return points;
    }

    // --- what the block is doing ---

    function kindOf(watching: bool, blockEnabled: bool, compressed: real, gated: real, limited: real,
                    input: real): string {
        if (!blockEnabled) {
            return "off";
        }
        if (!watching) {
            return "stopped";
        }
        if (input < silentDb) {
            return "silent";
        }
        if (limited >= workingDb) {
            return "limiting";
        }
        if (gated >= workingDb && gated >= compressed) {
            return "gating";
        }
        return compressed >= workingDb ? "compressing" : "under";
    }

    /// The state dot (BlockTile.dotColor()).
    function dotOf(kind: string): string {
        switch (kind) {
        case "limiting":
        case "gating":
        case "compressing":
            return "live";
        case "silent":
        case "under":
            return "ok";
        }
        return "off";
    }

    /// The tile's line under the number.
    function captionOf(kind: string): string {
        switch (kind) {
        case "silent":
            return KI18n.i18nc("@label what a Voice Compressor tile says it is doing", "no sound");
        case "under":
            return KI18n.i18nc("@label what a Voice Compressor tile says it is doing", "below compression");
        case "compressing":
            return KI18n.i18nc("@label what a Voice Compressor tile says it is doing", "compressing");
        case "gating":
            return KI18n.i18nc("@label what a Voice Compressor tile says it is doing", "gate lowering");
        case "limiting":
            return KI18n.i18nc("@label what a Voice Compressor tile says it is doing", "at the ceiling");
        case "stopped":
            return KI18n.i18nc("@label what a Voice Compressor tile says while its session is stopped", "Not playing");
        case "off":
            return KI18n.i18nc("@label what a Voice Compressor tile says when switched off", "Switched off");
        }
        return "";
    }

    /// The card's status line.
    function statusOf(kind: string, total: real): string {
        const amount = decibels(total);
        switch (kind) {
        case "silent":
            return KI18n.i18nc("@info:status Voice Compressor", "No sound coming in");
        case "under":
            return KI18n.i18nc("@info:status Voice Compressor", "Below compression, nothing taken off");
        case "compressing":
            return KI18n.i18nc("@info:status Voice Compressor, 'Taking off 6.2 dB'", "Taking off %1 dB", amount);
        case "gating":
            return KI18n.i18nc("@info:status Voice Compressor, 'Gate lowering, 12.0 dB off'",
                               "Gate lowering, %1 dB off", amount);
        case "limiting":
            return KI18n.i18nc("@info:status Voice Compressor, 'At the ceiling, compression and gate took off 6.2 dB'",
                               "At the ceiling, compression and gate took off %1 dB", amount);
        case "off":
            return KI18n.i18nc("@info:status a Voice Compressor that is switched off", "Switched off");
        }
        return KI18n.i18nc("@info:status Voice Compressor while its session is stopped", "Not playing");
    }

    /// A positive amount to one place, in the reader's locale.
    function decibels(amount: real): string {
        return Number(Math.abs(amount)).toLocaleString(Qt.locale(), 'f', 1);
    }

    /// The most taken off, as the tile's headline says it.
    function reductionText(total: real): string {
        return total < 0.05 ? KI18n.i18nc("@info no change at all", "0 dB")
                            : KI18n.i18nc("@info a reduction in decibels, '-6.2 dB'", "-%1 dB", decibels(total));
    }

    /// The settings on a tile, in the little room it has.
    function tileLine(compression: real, ceiling: real): string {
        return KI18n.i18nc("@info Voice Compressor's settings on its tile: compression, then ceiling, '-12 dB, ceiling -6'",
                           "%1 dB, ceiling %2", common.plainNumber(compression), common.plainNumber(ceiling));
    }

    /// The settings in full, for the tooltip and a screen reader.
    function settingsLine(compression: real, ceiling: real): string {
        return KI18n.i18nc("@info Voice Compressor's settings, 'Compression -12 dB, ceiling -6 dBFS'",
                           "Compression %1 dB, ceiling %2 dBFS", common.plainNumber(compression),
                           common.plainNumber(ceiling));
    }

    function gateLine(gate: real): string {
        return gate <= words.gateOff
            ? KI18n.i18nc("@info Voice Compressor's gate", "Gate off")
            : KI18n.i18nc("@info Voice Compressor's gate, 'Gate -50 dB'", "Gate %1 dB", common.plainNumber(gate));
    }

    function presetName(id: string): string {
        switch (id) {
        case "soft-voice":
            return KI18n.i18nc("@item:inlistbox a voice type for the Voice Compressor", "Soft voice");
        case "speech":
            return KI18n.i18nc("@item:inlistbox a voice type for the Voice Compressor", "Speech");
        case "strong-voice":
            return KI18n.i18nc("@item:inlistbox a voice type for the Voice Compressor", "Strong voice");
        case "singing":
            return KI18n.i18nc("@item:inlistbox a voice type for the Voice Compressor", "Singing");
        }
        return "";
    }
}
