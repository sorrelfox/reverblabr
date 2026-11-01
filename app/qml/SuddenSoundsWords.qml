// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Sudden Sounds block's numbers and words, for its tile, its card and its
// tests. The numbers are engine/blocks/suddensounds.h's and
// engine/dsp/transientcut.h's, and tst_suddensounds holds them together.
//
// **Knocks are what it earns, and never the word for what a ceiling does:** a
// door's low thump goes through nearly untouched, and this block stops nothing.
QtObject {
    id: words

    readonly property AutoLevelWords common: AutoLevelWords {}

    /// The block's published slots.
    readonly property int softenedValue: 0
    readonly property int countValue: 1

    /// The most a cut takes off, and where the bar ends.
    readonly property real mostCut: 15
    /// Strength, in the order the block counts it.
    readonly property var strengths: ["off", "gentle", "medium", "strong"]

    /// One word for what the block is doing. **Strength Off comes before a
    /// stopped session:** there is nothing to be live about.
    function kindOf(watching: bool, enabled: bool, strength: string, softenedDb: real): string {
        if (!enabled) {
            return "off";
        }
        if (strength === "off") {
            return "notSet";
        }
        if (!watching) {
            return "stopped";
        }
        return softenedDb > 0 ? "softening" : "ready";
    }

    /// The tile's dot. It says the block runs, and nothing about a level.
    function dotOf(kind: string): string {
        switch (kind) {
        case "softening":
            return "live";
        case "ready":
            return "ok";
        }
        return "off";
    }

    /// The tile's number.
    function headlineOf(kind: string, softenedDb: real): string {
        switch (kind) {
        case "ready":
            return words.common.boostText(0);
        case "softening":
            return words.common.boostText(-softenedDb);
        case "stopped":
            return KI18n.i18nc("@info nothing to measure, the session is not running", "Not live");
        }
        return KI18n.i18nc("@info Sudden Sounds changes nothing", "Off");
    }

    function earLetter(ear: string): string {
        return ear === "right" ? KI18n.i18nc("@label the right ear, one letter on a tile", "R")
                               : KI18n.i18nc("@label the left ear, one letter on a tile", "L");
    }

    /// The small line on the tile.
    function captionOf(kind: string, ear: string, strength: string): string {
        switch (kind) {
        case "notSet":
            return words.earLetter(ear);
        case "ready":
        case "softening":
            return KI18n.i18nc("@label a Sudden Sounds tile, 'L · Medium'", "%1 · %2", words.earLetter(ear),
                               words.strengthName(strength));
        }
        return "";
    }

    /// The card's status line.
    function statusOf(kind: string): string {
        switch (kind) {
        case "ready":
            return KI18n.i18nc("@info:status Sudden Sounds", "Ready");
        case "softening":
            return KI18n.i18nc("@info:status Sudden Sounds", "Softening now");
        case "stopped":
            return KI18n.i18nc("@info nothing to measure, the session is not running", "Not live");
        }
        return KI18n.i18nc("@info Sudden Sounds changes nothing", "Off");
    }

    /// One choice of Strength.
    function strengthName(choice: string): string {
        switch (choice) {
        case "off":
            return KI18n.i18nc("@option:radio Sudden Sounds' Strength", "Off");
        case "gentle":
            return KI18n.i18nc("@option:radio Sudden Sounds' Strength", "Gentle");
        case "medium":
            return KI18n.i18nc("@option:radio Sudden Sounds' Strength", "Medium");
        case "strong":
            return KI18n.i18nc("@option:radio Sudden Sounds' Strength", "Strong");
        }
        return "";
    }

    /// The block's line from the library, at the top of the card.
    function description(): string {
        return BlockPresentation.description("hearing.suddensounds");
    }

    /// One number under the bar.
    function markText(db: int): string {
        return Number(db).toLocaleString(Qt.locale(), 'f', 0);
    }

    /// What Softened now shows beside its bar.
    function softenedText(live: bool, softenedDb: real): string {
        if (!live) {
            return KI18n.i18nc("@info nothing to measure, the session is not running", "Not live");
        }
        const number = Number(Math.round(softenedDb)).toLocaleString(Qt.locale(), 'f', 0);
        return KI18n.i18nc("@info how much was taken off, '11 dB'", "%1 dB", number);
    }

    /// The count line.
    function countText(count: int): string {
        return KI18n.i18ncp("@info how often Sudden Sounds acted", "Softened in the last minute: %1 time",
                            "Softened in the last minute: %1 times", count);
    }
}
