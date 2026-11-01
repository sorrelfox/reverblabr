// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The words and numbers Night Level's face and panel both use, so the two
// cannot say the same setting differently. The numbers are written down twice,
// since the interface layer does not depend on the engine's; tst_nightlevel.qml
// holds them against the catalogue, and the curve against the block's table.
QtObject {
    id: words

    readonly property AutoLevelWords common: AutoLevelWords {}

    readonly property real defaultTyped: -28
    readonly property real lowestPivot: -45
    readonly property real highestPivot: -10

    /// Which of the block's published values is which (NightLevel).
    readonly property int gainValue: 0
    readonly property int foundValue: 1
    readonly property int readingValue: 2
    readonly property int stateValue: 3
    /// What the found value says with no level.
    readonly property real noLevel: -120

    /// The block's law (NightLevel::lawDb()).
    readonly property real bandAbove: 3
    readonly property real bandBelow: 5
    readonly property real floor: -30
    readonly property real cutSlope: 0.6
    readonly property real liftSlope: 0.4
    readonly property real heldFrom: 30
    readonly property real nearDb: 0.5

    /// What the state number holds (NightLevel::kPerActing and on).
    function trackingOf(state: real): int {
        return Math.round(state) % 10;
    }

    function actingOf(state: real): bool {
        return Math.floor(state / 10) % 10 === 1;
    }

    function detectorOf(state: real): int {
        return Math.floor(state / 100) % 10;
    }

    function secondsOf(state: real): int {
        return Math.floor(state / 1000);
    }

    /// What the law does to a sound \a lu from speech, in dB, with the amounts
    /// in percent.
    function changeFor(lu: real, cut: real, boost: real): real {
        if (lu > bandAbove) {
            return -(lu - bandAbove) * cutSlope * cut / 100;
        }
        if (lu < -bandBelow && lu >= floor) {
            return (-lu - bandBelow) * liftSlope * boost / 100;
        }
        return 0;
    }

    /// The curve as `{ in, out }`, in LU from speech, one point at each bend.
    function curve(cut: real, boost: real): var {
        return [-40, floor - 0.001, floor, -bandBelow, bandAbove, 30]
            .map(lu => ({ in: lu, out: lu + changeFor(lu, cut, boost) }));
    }

    /// The ratio an amount gives, as the graph says it: "2.5:1".
    function ratioText(slope: real, amount: real): string {
        const left = 1 - slope * amount / 100;
        return KI18n.i18nc("@info a ratio on Night Level's graph, '2.5:1'", "%1:1",
                           Number(1 / left).toLocaleString(Qt.locale(), 'f', 2).replace(/0$/, ""));
    }

    /// The rows of the table, in LU from speech.
    readonly property var tableLevels: [30, 20, 10, 0, -10, -20, -30, -40]

    /// Where the ceiling stands, in dBFS (NightLevel::ceilingDb()), or a value
    /// above -1 for one out of reach. \a pivot is noLevel with none.
    function ceilingFor(pivot: real, cut: real, boost: real): real {
        if (cut <= 0 && boost <= 0) {
            return 40;
        }
        if (pivot <= noLevel || cut <= 0) {
            return -1;
        }
        return Math.min(-1, pivot + 20 + 20 * (1 - cut / 100));
    }

    /// What it all comes to as a word: "off", "stopped", "notSet", "noSpeech",
    /// "ceiling", "down", "lifting" or "left".
    function kindOf(watching: bool, blockEnabled: bool, cut: real, boost: real, state: real, gainDb: real): string {
        if (!blockEnabled) {
            return "off";
        }
        if (!watching) {
            return "stopped";
        }
        // What it is doing to the sound comes first: a block with no level
        // can still be limiting, or gliding back after a lapse.
        if (actingOf(state)) {
            return "ceiling";
        }
        if (Math.abs(gainDb) > nearDb) {
            return gainDb > 0 ? "lifting" : "down";
        }
        if (cut <= 0 && boost <= 0) {
            return "notSet";
        }
        return trackingOf(state) === 0 ? "noSpeech" : "left";
    }

    /// The state dot (BlockTile.dotColor()).
    function dotOf(kind: string): string {
        switch (kind) {
        case "ceiling":
        case "down":
        case "lifting":
            return "live";
        case "notSet":
        case "noSpeech":
        case "left":
            return "ok";
        }
        return "off";
    }

    /// The tile's line under the number.
    function captionOf(kind: string): string {
        switch (kind) {
        case "notSet":
            return KI18n.i18nc("@label what a Night Level tile says it is doing", "set an amount");
        case "noSpeech":
            return KI18n.i18nc("@label what a Night Level tile says it is doing", "passing unchanged");
        case "left":
            return KI18n.i18nc("@label what a Night Level tile says it is doing", "left alone");
        case "down":
            return KI18n.i18nc("@label what a Night Level tile says it is doing", "turning down");
        case "lifting":
            return KI18n.i18nc("@label what a Night Level tile says it is doing", "lifting");
        case "ceiling":
            return KI18n.i18nc("@label what a Night Level tile says it is doing", "peaks held");
        case "stopped":
            return KI18n.i18nc("@label what a Night Level tile says while its session is stopped", "Not playing");
        }
        return "";
    }

    /// The card's status line.
    function statusOf(kind: string, gainDb: real): string {
        const amount = Math.round(Math.abs(gainDb));
        switch (kind) {
        case "notSet":
            return KI18n.i18nc("@info:status Night Level", "Set an amount below");
        case "noSpeech":
            return KI18n.i18nc("@info:status Night Level", "No speech yet");
        case "left":
            return KI18n.i18nc("@info:status Night Level", "Left alone");
        case "down":
            return KI18n.i18nc("@info:status Night Level, 'Turning down 7 dB'", "Turning down %1 dB", amount);
        case "lifting":
            return KI18n.i18nc("@info:status Night Level, 'Lifting 3 dB'", "Lifting %1 dB", amount);
        case "ceiling":
            return KI18n.i18nc("@info:status Night Level, 'Peaks held, down 12 dB'", "Peaks held, down %1 dB", amount);
        case "off":
            return KI18n.i18nc("@info:status a Night Level that is switched off", "Switched off");
        }
        return KI18n.i18nc("@info:status Night Level while its session is stopped", "Not playing");
    }

    /// The tile's third line. \a tracking is what the block says, \a found the
    /// level it found, \a typed the level typed.
    function levelLine(tracking: int, found: real, typed: real, seconds: int): string {
        switch (tracking) {
        case 1:
            return seconds >= heldFrom
                ? KI18n.i18nc("@info Night Level's found level, held with no voice, '-27 LUFS held'", "%1 held",
                              common.loudnessText(found))
                : KI18n.i18nc("@info Night Level's found level, 'Speech -27 LUFS'", "Speech %1", common.loudnessText(found));
        case 2:
            return KI18n.i18nc("@info Night Level's typed level, 'Typed -24 LUFS'", "Typed %1", common.loudnessText(typed));
        }
        return KI18n.i18nc("@info:status Night Level", "No speech yet");
    }

    /// The card's small line about a level held.
    function heldLine(seconds: int): string {
        return seconds < 60
            ? KI18n.i18ncp("@info Night Level holds its level, 'Held. No voice for 45 s'",
                           "Held. No voice for %1 s", "Held. No voice for %1 s", seconds)
            : KI18n.i18ncp("@info Night Level holds its level, 'Held. No voice for 4 min'",
                           "Held. No voice for %1 min", "Held. No voice for %1 min", Math.floor(seconds / 60));
    }

    /// The peak line, from the ceiling in dBFS.
    function peakLine(ceiling: real): string {
        return KI18n.i18nc("@info Night Level's ceiling, 'Peaks stop at -7 dBFS. A digital limit, not a level at your ears'",
                           "Peaks stop at %1 dBFS. A digital limit, not a level at your ears",
                           common.plainNumber(ceiling));
    }

    /// The presets the choice names, and the amounts each stands for.
    readonly property var presets: [{ id: "light", cut: 50, boost: 0 }, { id: "night", cut: 100, boost: 50 },
                                    { id: "strong", cut: 100, boost: 100 }]

    function presetName(id: string): string {
        switch (id) {
        case "light":
            return KI18n.i18nc("@item:inlistbox how much Night Level does", "Light");
        case "night":
            return KI18n.i18nc("@item:inlistbox how much Night Level does", "Night");
        case "strong":
            return KI18n.i18nc("@item:inlistbox how much Night Level does", "Strong");
        }
        return "";
    }

    /// Where the speech level comes from.
    function pivotFromName(id: string): string {
        switch (id) {
        case "found":
            return KI18n.i18nc("@item:inlistbox where Night Level's speech level comes from", "Found");
        case "typed":
            return KI18n.i18nc("@item:inlistbox where Night Level's speech level comes from", "Typed");
        }
        return "";
    }
}
