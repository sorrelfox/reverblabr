// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Voice Forward block's numbers and words, for its tile, its card and
// its tests. The numbers are blocks/voiceforward/voiceforward.h's, and
// tst_voiceforward holds the two together.
//
// **Never "voice" in what it found:** the mask finds what is alike on both
// sides and does not know a voice from a drum.
QtObject {
    id: words

    readonly property AutoLevelWords common: AutoLevelWords {}

    /// The block's published slots.
    readonly property int takenOutValue: 0
    readonly property int foundValue: 1
    readonly property int liftValue: 2
    readonly property int limitValue: 3

    /// VoiceForward::Found.
    readonly property int quiet: 0
    readonly property int wide: 1
    readonly property int centre: 2
    readonly property int alike: 3

    readonly property real lowestCut: -24
    readonly property real defaultBetween: -3
    /// A limiter acting less than this is not called acting.
    readonly property real actingFrom: 0.1
    /// Less than this of lift or of cut is no change left.
    readonly property real settledWithin: 0.05
    /// A gap smaller than this is not worth a line.
    readonly property real leastGap: 0.5

    /// What the cut in use is: Between voices stops at Background.
    function betweenActing(background: real, between: real): real {
        return Math.max(background, between);
    }

    /// One word for what the block is doing. **Background at 0 dB is not yet
    /// "not set":** the cut takes up to 2 s to come back and the lift longer,
    /// and until they have the sound is still changed.
    function kindOf(watching: bool, enabled: bool, background: real, found: int, liftDb: real, takenOutDb: real): string {
        if (!enabled) {
            return "off";
        }
        if (!watching) {
            return "stopped";
        }
        if (background === 0) {
            const settled = Math.abs(liftDb) < words.settledWithin && Math.abs(takenOutDb) < words.settledWithin;
            return settled ? "notSet" : "settling";
        }
        if (found === words.alike) {
            return "alike";
        }
        if (found === words.centre) {
            return "centre";
        }
        return found === words.wide ? "between" : "quiet";
    }

    /// The tile's dot.
    function dotOf(kind: string): string {
        switch (kind) {
        case "centre":
        case "settling":
            return "live";
        case "alike":
            return "warning";
        case "notSet":
        case "between":
        case "quiet":
            return "ok";
        }
        return "off";
    }

    /// The small line on the tile. \a actingDb is the cut between voices.
    function captionOf(kind: string, actingDb: real): string {
        switch (kind) {
        case "alike":
            return KI18n.i18nc("@label a Voice Forward tile, the setting has nothing to work on", "both sides alike");
        case "between":
            return KI18n.i18nc("@label a Voice Forward tile, 'background · -3 now'", "background · %1 now",
                               words.common.boostText(actingDb));
        case "off":
            return "";
        }
        return KI18n.i18nc("@label what the number on a Voice Forward tile is", "background");
    }

    /// The card's first line.
    function statusOf(kind: string): string {
        switch (kind) {
        case "off":
            return KI18n.i18nc("@info:status Voice Forward", "Off");
        case "stopped":
            return KI18n.i18nc("@info:status Voice Forward", "Not running");
        case "notSet":
            return KI18n.i18nc("@info:status Voice Forward", "Not set. Sound passes unchanged");
        case "alike":
            return KI18n.i18nc("@info:status Voice Forward", "Nothing to move: both sides alike");
        }
        return KI18n.i18nc("@info:status Voice Forward", "Working");
    }

    /// The word for what the mask found.
    function foundName(found: int): string {
        switch (found) {
        case words.wide:
            return KI18n.i18nc("@info what Voice Forward found", "Wide sound");
        case words.centre:
            return KI18n.i18nc("@info what Voice Forward found", "Centre sound");
        case words.alike:
            return KI18n.i18nc("@info what Voice Forward found", "Both sides alike");
        }
        return KI18n.i18nc("@info what Voice Forward found", "Quiet");
    }

    /// The line under Hold level, or nothing.
    function holdLine(on: bool, current: bool, liftDb: real, limitDb: real): string {
        if (!on || !current) {
            return "";
        }
        const lifting = KI18n.i18nc("@info under Voice Forward's Hold level, 'Lifting +3 dB'", "Lifting %1",
                                    words.common.boostText(liftDb));
        if (limitDb >= words.actingFrom) {
            return KI18n.i18nc("@info under Voice Forward's Hold level, 'Lifting +3 dB. Limit acting'",
                               "%1. Limit acting", lifting);
        }
        return lifting;
    }

    /// The line under Hold for before while Hold level is off, or nothing.
    function beforeLine(holdLevel: bool, current: bool, takenOutDb: real): string {
        if (holdLevel || !current || takenOutDb > -words.leastGap) {
            return "";
        }
        return KI18n.i18nc("@info under Voice Forward's Hold for before, 'Before is 4 dB louder than this'",
                           "Before is %1 dB louder than this", Math.round(-takenOutDb));
    }

    /// The delay line. \a ms is what the block reports.
    function delayLine(ms: real): string {
        const number = Number(ms).toLocaleString(Qt.locale(), 'f', 2);
        return KI18n.i18nc("@info 'Adds 22.83 ms to this lane'", "Adds %1 ms to this lane", number);
    }
}
