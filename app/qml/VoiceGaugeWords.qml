// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// What the Voice Gauge block says, in one place, so that the tile, the card,
// the tray, the floating bar and the spoken sentence can't disagree.
//
// **It says where a reading stands against the person's own marks, and never
// whether that is good.** There is no word here about how a voice sounds, and
// none that claims more than a level at this point of a lane. The position
// comes from the engine, and only its word is chosen here.
QtObject {
    id: words

    /// Not set, and what no reading publishes (VoiceGauge).
    readonly property real notSet: -61
    readonly property real lowestMark: -60
    readonly property real highestMark: -6
    readonly property real leastGap: 1
    readonly property real noReading: -120

    /// The fields of the state number (VoiceGauge::kStateValue).
    function soundOf(state: real): int {
        return Math.round(state) % 10;
    }
    function voiceNowOf(state: real): bool {
        return Math.floor(Math.round(state) / 10) % 10 === 1;
    }
    function positionOf(state: real): int {
        return Math.floor(Math.round(state) / 100) % 10;
    }
    function secondsOf(state: real): int {
        return Math.floor(Math.round(state) / 1000) % 100;
    }
    function detectorOf(state: real): int {
        return Math.floor(Math.round(state) / 100000);
    }

    readonly property int noSound: 0
    readonly property int roomOnly: 1
    readonly property int voice: 2

    /// The marks in use (VoiceGauge::kMarksValue), as `{upper, lower}`.
    function marksOf(packed: real): var {
        const whole = Math.round(packed);
        return { upper: Math.floor(whole / 100) + words.notSet, lower: whole % 100 + words.notSet };
    }

    function isSet(mark: real): bool {
        return mark > words.notSet;
    }

    /// Two marks after one moved, as the block works them out
    /// (VoiceGauge::pushed), so that what is written is what will be used.
    function moved(upper: real, lower: real, lowerMoved: bool): var {
        const marks = { upper: Math.round(upper), lower: Math.round(lower) };
        if (!words.isSet(marks.upper) || !words.isSet(marks.lower)) {
            return marks;
        }
        if (lowerMoved) {
            marks.lower = Math.min(marks.lower, words.highestMark - words.leastGap);
            marks.upper = Math.max(marks.upper, marks.lower + words.leastGap);
        } else {
            marks.upper = Math.max(marks.upper, words.lowestMark + words.leastGap);
            marks.lower = Math.min(marks.lower, marks.upper - words.leastGap);
        }
        return marks;
    }

    /// The marks Use This Level places around \a level, \a room either side.
    function around(level: real, room: real): var {
        const upper = Math.max(words.lowestMark + words.leastGap, Math.min(words.highestMark, Math.round(level + room)));
        const lower = Math.min(upper - words.leastGap, Math.max(words.lowestMark, Math.round(level - room)));
        return { upper: upper, lower: lower };
    }

    /// The scale of the bar on the tile and the floating window: the two marks
    /// keep one place, a third and two thirds along, so a glance works the
    /// same in every session. One mark sits in the middle with 12 LU each
    /// side, and with none it runs over the card's whole scale.
    function span(upper: real, lower: real): var {
        const hasUpper = words.isSet(upper);
        const hasLower = words.isSet(lower);
        if (hasUpper && hasLower) {
            const third = upper - lower;
            return { from: lower - third, to: upper + third };
        }
        if (hasUpper || hasLower) {
            const mark = hasUpper ? upper : lower;
            return { from: mark - 12, to: mark + 12 };
        }
        return { from: -60, to: 0 };
    }

    /// What can be shown of a block now: "voice", "held", "noReading",
    /// "noSound", "notWatching" or "off". \a watch is Meters.watchState()'s word.
    function kindOf(watch: string, enabled: bool, state: real, reading: real): string {
        if (!enabled) {
            return "off";
        }
        if (watch !== "watching") {
            return "notWatching";
        }
        if (words.soundOf(state) === words.noSound) {
            return "noSound";
        }
        if (reading <= words.noReading) {
            return "noReading";
        }
        return words.voiceNowOf(state) ? "voice" : "held";
    }

    /// Whether a kind has a number to show.
    function showsNumber(kind: string): bool {
        return kind === "voice" || kind === "held";
    }

    /// The tile's dot. **Never "ok".** Green beside "Between marks" would
    /// read as a verdict, so orange says a voice is being counted, grey that
    /// it is not, and the warning is for a signal, not for a level.
    function dot(kind: string): string {
        if (kind === "voice") {
            return "live";
        }
        return kind === "noSound" ? "warning" : "off";
    }

    function loudness(value: real): string {
        return KI18n.i18nc("@info a level, '-24 LUFS'", "%1 LUFS", Math.round(value));
    }

    /// The line above everything: the number, or why there is none.
    function headline(kind: string, reading: real): string {
        switch (kind) {
        case "voice":
        case "held":
            return words.loudness(reading);
        case "noReading":
            return KI18n.i18nc("@info a Voice Gauge has no number yet", "No reading yet");
        case "noSound":
            return KI18n.i18nc("@info nothing reaches a Voice Gauge", "No sound here");
        case "notWatching":
            return KI18n.i18nc("@info a Voice Gauge that cannot hear its lane", "Not watching");
        }
        return KI18n.i18nc("@info a Voice Gauge that is switched off", "Off");
    }

    /// A position by its number (MarkPosition), as a line of its own.
    function position(where: int): string {
        switch (where) {
        case 1:
            return KI18n.i18nc("@info where a voice level stands", "Between marks");
        case 2:
            return KI18n.i18nc("@info where a voice level stands", "Above upper mark");
        case 3:
            return KI18n.i18nc("@info where a voice level stands", "Below upper mark");
        case 4:
            return KI18n.i18nc("@info where a voice level stands", "Above lower mark");
        case 5:
            return KI18n.i18nc("@info where a voice level stands", "Below lower mark");
        }
        return KI18n.i18nc("@info a Voice Gauge has no marks", "No marks set");
    }

    /// The same inside a sentence.
    function positionInSentence(where: int): string {
        switch (where) {
        case 1:
            return KI18n.i18nc("@info where a voice level stands, inside a sentence", "between marks");
        case 2:
            return KI18n.i18nc("@info where a voice level stands, inside a sentence", "above upper mark");
        case 3:
            return KI18n.i18nc("@info where a voice level stands, inside a sentence", "below upper mark");
        case 4:
            return KI18n.i18nc("@info where a voice level stands, inside a sentence", "above lower mark");
        case 5:
            return KI18n.i18nc("@info where a voice level stands, inside a sentence", "below lower mark");
        }
        return KI18n.i18nc("@info a Voice Gauge has no marks, inside a sentence", "no marks set");
    }

    /// The line under the number on the tile and the floating window.
    function line(kind: string, state: real, watch: string): string {
        switch (kind) {
        case "voice":
            return words.position(words.positionOf(state));
        case "held":
            return KI18n.i18nc("@info a held reading and where it stands, 'Held · between marks'", "Held · %1",
                               words.positionInSentence(words.positionOf(state)));
        case "noReading":
            return words.soundOf(state) === words.voice
                ? KI18n.i18nc("@info a Voice Gauge waiting for a first second of voice", "Talk to start")
                : KI18n.i18nc("@info sound reaches a Voice Gauge and it hears no voice", "Sound, no voice");
        case "noSound":
            return KI18n.i18nc("@info under No sound here", "At this point");
        case "notWatching":
            return words.reason(watch);
        }
        return "";
    }

    /// Why nothing is being watched, from Meters.watchState()'s word.
    function reason(watch: string): string {
        switch (watch) {
        case "overloaded":
            return KI18n.i18nc("@info a session that gave way to overload", "Session overloaded");
        case "noNews":
            return KI18n.i18nc("@info the engine has stopped saying anything", "Engine not answering");
        }
        return KI18n.i18nc("@info a Voice Gauge whose session is not running", "Session stopped");
    }

    /// The card's status row.
    function status(kind: string, state: real): string {
        switch (kind) {
        case "voice":
            return KI18n.i18nc("@info what arrives at a Voice Gauge", "Voice at this point");
        case "held":
            return KI18n.i18nc("@info a Voice Gauge keeps its number through a pause", "Held");
        case "noSound":
            return KI18n.i18nc("@info what arrives at a Voice Gauge", "No sound at this point");
        case "noReading":
            return words.soundOf(state) === words.voice
                ? KI18n.i18nc("@info a Voice Gauge has no number yet", "No reading yet")
                : KI18n.i18nc("@info what arrives at a Voice Gauge", "Sound at this point, no voice");
        case "notWatching":
            return KI18n.i18nc("@info a Voice Gauge that cannot hear its lane", "Not watching");
        }
        return KI18n.i18nc("@info a Voice Gauge that is switched off", "Off");
    }

    /// What the tray writes under a name: "-24 LUFS, between marks".
    function trayLine(kind: string, state: real, reading: real): string {
        return !words.showsNumber(kind) ? words.headline(kind, reading)
            : KI18n.i18nc("@info a level and where it stands, '-24 LUFS, between marks'", "%1, %2",
                          words.loudness(reading), words.positionInSentence(words.positionOf(state)));
    }

    /// What a screen reader says when asked: "Minus 24 LUFS, between marks".
    function spoken(kind: string, state: real, reading: real): string {
        if (!words.showsNumber(kind)) {
            return words.headline(kind, reading);
        }
        const number = Math.round(reading);
        const level = number < 0 ? KI18n.i18nc("@info:status spoken, a level below zero", "Minus %1 LUFS", -number)
                                 : words.loudness(reading);
        return KI18n.i18nc("@info:status spoken, a level and where it stands", "%1, %2", level,
                           words.positionInSentence(words.positionOf(state)));
    }

    /// How many seconds of voice a person has given it so far, on the button.
    function useThisLevel(seconds: int, ready: bool): string {
        return ready ? KI18n.i18nc("@action:button", "Use This Level")
                     : KI18n.i18nc("@action:button %1 counts up to 20 seconds of voice", "Use This Level (%1 of 20 s)", seconds);
    }

    function secondsOfVoice(value: real): string {
        return KI18n.i18nc("@info how long a reading runs", "%1 s of voice", Math.round(value));
    }

    function seconds(value: real): string {
        return KI18n.i18nc("@info a length of time", "%1 s", Math.round(value));
    }

    function roomEachSide(value: real): string {
        return KI18n.i18nc("@info a room either side of a level", "±%1 LU", Math.round(value));
    }
}
