// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The words and numbers Auto Level's face and panel both use, so the
// two cannot say the same setting differently.
QtObject {
    /// The block's defaults and ranges. The interface layer does not depend
    /// on the engine's, so they are written down twice; tst_autolevel.qml
    /// holds these against the engine's catalogue.
    readonly property real defaultTarget: -20
    readonly property real lowestTarget: -30
    readonly property real highestTarget: -14
    readonly property real defaultMaximum: 18
    readonly property real highestMaximum: 24
    readonly property real defaultMaximumCut: 18
    readonly property real highestMaximumCut: 24
    readonly property real highestBand: 6
    /// Under this there is nothing to read (AutoLevel::kGateLufs).
    readonly property real gate: -50

    /// Which of the block's published values is the gain it is applying
    /// (AutoLevel::kBoostValue), which the loudness it measured, and which
    /// what the guard is holding back.
    readonly property int boostValue: 0
    readonly property int loudnessValue: 1
    readonly property int pullValue: 2

    /// How near a gain has to be for the tile to call it the same, in dB.
    readonly property real nearDb: 0.5

    /// The block's rule (AutoLevel::wantedGainDb()) for a reading of \a lufs
    /// under \a set, which holds the block's settings: what the graph and its
    /// table draw. The test holds it against the engine's answers.
    function gainFor(lufs: real, set: var): real {
        if (lufs < gate || (!set.boostOn && !set.cutOn)) {
            return 0;
        }
        const low = set.cutOn ? -set.maximumCut : 0;
        const high = set.boostOn ? set.maximumBoost : 0;
        const error = set.target - lufs;
        const outside = Math.abs(error) <= set.band ? 0 : error - Math.sign(error) * set.band;
        return Math.max(low, Math.min(high, outside));
    }

    /// The curve of \a set as `{ in, out }`, left to right, one point at each
    /// bend from -60 to 0 LUFS.
    function curve(set: var): var {
        const lower = set.target - set.band;
        const upper = set.target + set.band;
        const bends = [-60, gate - 0.001, gate, lower - set.maximumBoost, lower, upper, upper + set.maximumCut, 0];
        return bends.filter(lufs => lufs >= -60 && lufs <= 0)
            .sort((first, second) => first - second)
            .map(lufs => ({ in: lufs, out: lufs + gainFor(lufs, set) }));
    }

    /// The table's rows, as levels coming in: under the gate, the block's
    /// range and the target, each once.
    function tableLevels(target: real): var {
        return [-55, -45, -30, target, -15, -2].filter((lufs, at, all) => all.indexOf(lufs) === at)
            .sort((first, second) => first - second);
    }

    /// What a block's gain comes to as a word for the tile: "lifting",
    /// "down", "limit", "pulled", "left", "quiet", "bothOff", or "stopped"
    /// and "off" while nothing is read.
    function stateOf(set: var, live: bool, gainDb: real, readingLufs: real, pullDb: real): string {
        if (!set.blockEnabled) {
            return "off";
        }
        if (!live) {
            return "stopped";
        }
        if (!set.boostOn && !set.cutOn) {
            return "bothOff";
        }
        if (pullDb > nearDb) {
            return "pulled";
        }
        if (readingLufs < gate) {
            return "quiet";
        }
        if (Math.abs(gainDb) <= nearDb) {
            return "left";
        }
        const shortOfBand = Math.abs(set.target - readingLufs) > set.band;
        const atLimit = gainDb > 0 ? set.boostOn && set.maximumBoost - gainDb <= nearDb
                                   : set.cutOn && set.maximumCut + gainDb <= nearDb;
        if (atLimit && shortOfBand) {
            return "limit";
        }
        return gainDb > 0 ? "lifting" : "down";
    }

    /// The tile's line under the number.
    function captionOf(state: string): string {
        switch (state) {
        case "lifting":
            return KI18n.i18nc("@label what an Auto Level tile says it is doing", "lifting");
        case "down":
            return KI18n.i18nc("@label what an Auto Level tile says it is doing", "turning down");
        case "limit":
            return KI18n.i18nc("@label what an Auto Level tile says it is doing", "at the maximum");
        case "pulled":
            return KI18n.i18nc("@label what an Auto Level tile says it is doing", "pulled back");
        case "left":
            return KI18n.i18nc("@label what an Auto Level tile says it is doing", "left alone");
        case "quiet":
            return KI18n.i18nc("@label what an Auto Level tile says it is doing", "too quiet to read");
        case "bothOff":
            return KI18n.i18nc("@label what an Auto Level tile says it is doing", "both switches off");
        case "stopped":
            return KI18n.i18nc("@label what an Auto Level tile says while its session is stopped", "Not playing");
        }
        return "";
    }

    /// The card's status line, with \a gainDb and \a pullDb as they stand.
    function statusOf(state: string, gainDb: real, pullDb: real): string {
        const amount = Math.round(Math.abs(gainDb));
        switch (state) {
        case "lifting":
            return KI18n.i18nc("@info:status Auto Level, 'Lifting 6 dB'", "Lifting %1 dB", amount);
        case "down":
            return KI18n.i18nc("@info:status Auto Level, 'Turning down 6 dB'", "Turning down %1 dB", amount);
        case "limit":
            return gainDb > 0
                ? KI18n.i18nc("@info:status Auto Level at its Maximum boost", "Lifting %1 dB, the most it may", amount)
                : KI18n.i18nc("@info:status Auto Level at its Maximum cut", "Turning down %1 dB, the most it may", amount);
        case "pulled":
            return KI18n.i18nc("@info:status Auto Level, 'Pulling back 9 dB'", "Pulling back %1 dB", Math.round(pullDb));
        case "left":
            return KI18n.i18nc("@info:status Auto Level", "Leaving the sound as it is");
        case "quiet":
            return KI18n.i18nc("@info:status Auto Level", "Too quiet to read");
        case "bothOff":
            return KI18n.i18nc("@info:status Auto Level with both its switches off", "Both switches off");
        case "off":
            return KI18n.i18nc("@info:status an Auto Level that is switched off", "Switched off");
        }
        return KI18n.i18nc("@info:status Auto Level while its session is stopped", "Not playing");
    }

    /// A gain as it is said, in whole dB: "+6 dB", "-4 dB", "0 dB".
    function boostText(db: real): string {
        const whole = Math.round(db);
        if (whole === 0) {
            return KI18n.i18nc("@info no change at all", "0 dB");
        }
        const number = Number(Math.abs(whole)).toLocaleString(Qt.locale(), 'f', 0);
        return whole > 0 ? KI18n.i18nc("@info a change in decibels, '+6 dB'", "+%1 dB", number)
                         : KI18n.i18nc("@info a change in decibels, '-4 dB'", "-%1 dB", number);
    }

    /// A loudness as it is said: "−20 LUFS".
    function loudnessText(lufs: real): string {
        return KI18n.i18nc("@info a loudness in loudness units relative to full scale, '-20 LUFS'", "%1 LUFS",
                           Number(Math.round(lufs)).toLocaleString(Qt.locale(), 'f', 0));
    }

    /// A reading as the live line says it, which may be nothing to speak of.
    function readingText(lufs: real): string {
        return lufs < -60 ? KI18n.i18nc("@info a loudness too low for the graph", "under -60 LUFS") : loudnessText(lufs);
    }

    /// A limit in decibels: "18 dB".
    function decibelText(db: real): string {
        return KI18n.i18nc("@info a level in decibels, '18 dB'", "%1 dB",
                           Number(Math.round(db)).toLocaleString(Qt.locale(), 'f', 0));
    }

    /// The width of what is left alone: "±1 LU".
    function bandText(lu: real): string {
        return KI18n.i18nc("@info how far either side of the target is left alone, '±1 LU'", "±%1 LU",
                           Number(Math.round(lu)).toLocaleString(Qt.locale(), 'f', 0));
    }

    /// The live numbers under the graph.
    function liveText(readingLufs: real, gainDb: real): string {
        return KI18n.i18nc("@info the sound coming in and going out of Auto Level, 'In -30 LUFS · Out -24 LUFS · Change +6 dB'",
                           "In %1 · Out %2 · Change %3", readingText(readingLufs),
                           readingText(readingLufs + gainDb), boostText(gainDb));
    }

    /// The range line, from what AutoLevelHistory kept.
    function rangeText(low: real, high: real): string {
        return Math.round(low) === Math.round(high)
            ? KI18n.i18nc("@info the gain of Auto Level over the last minute, 'Stayed at +6 dB in the last minute'",
                          "Stayed at %1 in the last minute", boostText(low))
            : KI18n.i18nc("@info the gain of Auto Level over the last minute, 'Moved -9 to +6 dB in the last minute'",
                          "Moved %1 to %2 in the last minute", plainNumber(low), boostText(high));
    }

    /// The first end of a range, which shares its unit with the second.
    function plainNumber(db: real): string {
        const whole = Math.round(db);
        const number = Number(Math.abs(whole)).toLocaleString(Qt.locale(), 'f', 0);
        return whole > 0 ? "+" + number : whole < 0 ? "-" + number : number;
    }
}
