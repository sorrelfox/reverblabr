// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// How a Frequency Lowering or Frequency Raising block's numbers
// are written.
QtObject {
    /// A frequency as short as a tile has room for: "4k", "2.5k", "800".
    function shortHz(hz: real): string {
        if (hz >= 1000) {
            const thousands = Math.round(hz / 100) / 10;
            return KI18n.i18nc("@info a frequency in thousands of hertz, short, '2.5k'", "%1k",
                               Number(thousands).toLocaleString(Qt.locale(), 'f', thousands % 1 === 0 ? 0 : 1));
        }
        return Number(Math.round(hz)).toLocaleString(Qt.locale(), 'f', 0);
    }

    /// A frequency in full: "4.0 kHz", "800 Hz".
    function hz(hz: real): string {
        if (hz >= 1000) {
            return KI18n.i18nc("@info a frequency in kilohertz", "%1 kHz",
                               Number(hz / 1000).toLocaleString(Qt.locale(), 'f', 1));
        }
        return KI18n.i18nc("@info a frequency in hertz", "%1 Hz", Number(Math.round(hz)).toLocaleString(Qt.locale(), 'f', 0));
    }

    /// A range: "4.0–8.0 kHz".
    function range(low: real, high: real): string {
        return KI18n.i18nc("@info a range of frequencies, low to high", "%1 – %2", hz(low), hz(high));
    }

    /// What a tile says about the moves: the first, and how many more,
    /// "4k → 2.5k (+1)".
    function summary(from: real, to: real, more: int): string {
        const move = KI18n.i18nc("@info a move from one frequency to another, on a tile", "%1 → %2",
                                 shortHz(from), shortHz(to));
        return more > 0 ? KI18n.i18nc("@info a move and how many more there are, on a tile", "%1 (+%2)", move, more)
                        : move;
    }

    /// A level: "+3.0 dB", "0.0 dB".
    function decibels(db: real): string {
        const sign = db > 0.05 ? "+" : "";
        return KI18n.i18nc("@info a level in decibels", "%1 dB",
                           sign + Number(db).toLocaleString(Qt.locale(), 'f', 1));
    }

    /// A move's name on its chip.
    function moveName(move: int): string {
        return KI18n.i18nc("@item one of a Frequency Lowering or Raising block's moves, numbered", "Move %1", move + 1);
    }
}
