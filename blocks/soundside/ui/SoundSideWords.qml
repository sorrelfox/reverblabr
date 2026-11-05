// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// What the Sound Side block says, in one place, so that the tile, the card, the
// strip and the shortcut can't disagree. **Which word it is comes from
// SoundSideReadings**, once for all of them; here it only gets its spelling.
//
// **It says which channel is louder, and no more.** There is no word here for
// what made the sound, for front or back, or for what anyone should do.
QtObject {
    id: words

    readonly property real noLevel: -120
    /// To stays this many times From (SoundSide::kLeastBandRatio).
    readonly property real leastBandRatio: 2

    readonly property SpeechNowWords watchWords: SpeechNowWords {}

    /// The two ends of the strip.
    readonly property string leftEnd: KI18n.i18nc("@label the left end of the strip, one letter", "L")
    readonly property string rightEnd: KI18n.i18nc("@label the right end of the strip, one letter", "R")

    /// On the tile and at the top of the card.
    function tileWord(kind: string): string {
        switch (kind) {
        case "stopped":
            return KI18n.i18nc("@info a Sound Side that cannot hear its lane", "Not watching");
        case "off":
            return KI18n.i18nc("@info a block switched off", "Off");
        }
        return words.stripWord(kind);
    }

    /// On the strip, which has one fixed box and no room for a reason.
    function stripWord(kind: string): string {
        switch (kind) {
        case "quiet":
            return KI18n.i18nc("@info no sound over the gate on a Sound Side", "Quiet");
        case "left":
            return KI18n.i18nc("@info the louder side of a mix", "Left");
        case "right":
            return KI18n.i18nc("@info the louder side of a mix", "Right");
        case "centre":
            return KI18n.i18nc("@info neither side of a mix is louder", "Centre");
        case "both":
            return KI18n.i18nc("@info sound fills both sides of a mix with no one place", "Both");
        case "same":
            return KI18n.i18nc("@info both sides of a mix are the same sound", "Same");
        }
        return KI18n.i18nc("@info a Sound Side that cannot hear its lane", "Stopped");
    }

    function level(value: real): string {
        return value <= words.noLevel + 0.5 ? KI18n.i18nc("@info no level at all on a Sound Side", "No sound")
                                            : KI18n.i18nc("@info a level, %1 in dBFS", "%1 dBFS", Math.round(value));
    }

    function caption(shown: var): string {
        switch (shown.kind) {
        case "quiet":
            return words.level(shown.level);
        case "left":
        case "right":
        case "centre":
            return KI18n.i18nc("@info how far to one side, then the level: '62 % · -31 dBFS'", "%1 % · %2",
                               shown.percent, words.level(shown.markLevel));
        case "both":
            return KI18n.i18nc("@info sound fills both sides, then the level: 'wide · -24 dBFS'", "wide · %1",
                               words.level(shown.markLevel));
        case "same":
            return KI18n.i18nc("@info the two sides of a lane are one sound", "Sides identical");
        case "stopped":
            return words.watchWords.reason(shown.watch);
        }
        return "";
    }

    /// The tile's dot. Green says the block works and says nothing of a level.
    function dot(kind: string): string {
        switch (kind) {
        case "quiet":
            return "ok";
        case "same":
            return "warning";
        case "stopped":
        case "off":
            return "off";
        }
        return "live";
    }

    function status(kind: string): string {
        switch (kind) {
        case "quiet":
            return KI18n.i18nc("@info what a Sound Side is doing", "Quiet");
        case "same":
            return KI18n.i18nc("@info what a Sound Side is doing", "Sides alike");
        case "stopped":
        case "off":
            return words.tileWord(kind);
        }
        return KI18n.i18nc("@info what a Sound Side is doing", "Sounding");
    }

    /// The figure beside Side now.
    function sideNow(kind: string, percent: int): string {
        return kind === "left" || kind === "right" || kind === "centre"
            ? KI18n.i18nc("@info a side and how far to it: 'Left 62 %'", "%1 %2 %", words.tileWord(kind), percent)
            : words.tileWord(kind);
    }

    /// The same for a screen reader.
    function sideSpoken(kind: string, percent: int): string {
        switch (kind) {
        case "left":
            return KI18n.i18nc("@info:status spoken, of a mix", "left %1 percent", percent);
        case "right":
            return KI18n.i18nc("@info:status spoken, of a mix", "right %1 percent", percent);
        case "centre":
            return KI18n.i18nc("@info:status spoken, of a mix", "centre %1 percent", percent);
        }
        return words.tileWord(kind);
    }

    /// From and To after one moved, as the block works them out
    /// (SoundSide::octaveApart), so that what is written is what will be used.
    function octaveApart(low: real, high: real, highMoved: bool): var {
        const lowest = highMoved ? Math.min(low, high / words.leastBandRatio) : low;
        const highest = highMoved ? high : Math.max(high, low * words.leastBandRatio);
        return { from: lowest, to: highest };
    }

    /// How far one side's bar reaches, 0 at the gate and 1 at full scale.
    /// \a side is -1 left and +1 right.
    function barShare(level: real, pan: real, gate: real, side: int): real {
        const sideLevel = level + 10 * Math.log10(Math.max(1 + side * pan, 1e-6));
        return Math.max(0, Math.min(1, (sideLevel - gate) / -gate));
    }

    function bandName(id: string): string {
        return id === "range" ? KI18n.i18nc("@item:inlistbox the band a Sound Side reads", "Range")
                              : KI18n.i18nc("@item:inlistbox the band a Sound Side reads", "All");
    }

    function sizeName(id: string): string {
        switch (id) {
        case "small":
            return KI18n.i18nc("@item:inlistbox a size of strip", "Small");
        case "large":
            return KI18n.i18nc("@item:inlistbox a size of strip", "Large");
        }
        return KI18n.i18nc("@item:inlistbox a size of strip", "Medium");
    }

    function showName(id: string): string {
        return id === "bars" ? KI18n.i18nc("@item:inlistbox what the strip draws", "Bars")
                             : KI18n.i18nc("@item:inlistbox what the strip draws", "Mark");
    }

    function hertz(value: real): string {
        return KI18n.i18nc("@info a frequency, %1 in hertz", "%1 Hz", Math.round(value));
    }
}
