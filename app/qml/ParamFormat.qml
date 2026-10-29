// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma Singleton

import QtQuick
import org.kde.ki18n

// How a parameter's value is written down, wherever one is written down.
//
// **One table of units, not one per place that shows a number.** The
// settings panel and a block's tile show the same parameter, and a face
// reading "1.50" beside a panel reading "1.5 ms" is two readings of one
// value. The rule for how many decimals a number deserves is the same
// argument.
QtObject {
    id: format

    // **A number with its unit, in the reader's language**. The unit
    // goes inside the translated string, as docs/i18n.md asks, so that a
    // language can choose its own spacing and order; the number is already
    // formatted, and goes in as it is.
    function withUnit(number: string, unit: string): string {
        switch (unit) {
        case "dB": return KI18n.i18nc("@label a value in decibels", "%1 dB", number);
        case "%": return KI18n.i18nc("@label a value in per cent", "%1 %", number);
        case "Hz": return KI18n.i18nc("@label a frequency in hertz", "%1 Hz", number);
        case "ms": return KI18n.i18nc("@label a time in milliseconds", "%1 ms", number);
        case "ratio": return KI18n.i18nc("@label a compression ratio, as in 4:1", "%1:1", number);
        case "st": return KI18n.i18nc("@label a musical interval in semitones, as in +3 st", "%1 st", number);
        case "ct": return KI18n.i18nc("@label hundredths of a semitone, as in +25 ct", "%1 ct", number);
        case "samples": return number + " ";
        default: return number;
        }
    }

    // The unit as it is written after a number, in the reader's language.
    function unitSuffix(unit: string): string {
        return format.withUnit("", unit);
    }

    // **Locale-formatted for the eye, never for the file.** A decimal comma
    // in de_DE is what a German reader expects; what travels to the engine
    // is a JSON number, which has no locale at all.
    function formatted(value: real, type: string, unit: string, locale: var): string {
        const digits = type === "int" ? 0 : format.decimals(value);
        return format.withUnit(Number(value).toLocaleString(locale, 'f', digits), unit);
    }

    // **How many digits the number itself deserves**, not how wide its range
    // is: an attack of 1.5 ms is a different attack from 1 ms, and a
    // threshold of −120 dB does not need two decimal places to say so.
    function decimals(value: real): int {
        const size = Math.abs(value);
        return size < 10 ? 2 : (size < 100 ? 1 : 0);
    }
}
