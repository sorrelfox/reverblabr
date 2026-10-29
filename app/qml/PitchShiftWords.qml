// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// How the Pitch Shift's numbers are written: always with their sign, since
// down and up are the whole point, and in the unit ParamFormat gives them.
QtObject {
    /// A signed number, "+3", "−0.5" or "0", with the decimals it deserves.
    /// The sign is the translation's to place, as some languages write it
    /// otherwise.
    function signed(value: real, digits: int): string {
        const size = Number(Math.abs(value)).toLocaleString(Qt.locale(), 'f', digits);
        if (Math.abs(value) < Math.pow(10, -digits) / 2) {
            return Number(0).toLocaleString(Qt.locale(), 'f', digits);
        }
        return value > 0 ? KI18n.i18nc("@label a shift upwards, as in +3", "+%1", size)
                         : KI18n.i18nc("@label a shift downwards, as in −3", "−%1", size);
    }

    /// The whole shift in semitones, the cents folded in: "+3 st", "−4.63 st".
    function shift(semitones: real): string {
        const whole = Math.abs(semitones - Math.round(semitones)) < 0.005;
        return ParamFormat.withUnit(signed(semitones, whole ? 0 : 2), "st");
    }

    /// Whole semitones: "+3 st".
    function semitones(value: real): string {
        return ParamFormat.withUnit(signed(value, 0), "st");
    }

    /// Cents: "+25 ct", "−37.5 ct".
    function cents(value: real): string {
        const whole = Math.abs(value - Math.round(value)) < 0.05;
        return ParamFormat.withUnit(signed(value, whole ? 0 : 1), "ct");
    }
}
