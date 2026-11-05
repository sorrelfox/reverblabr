// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The numbers the Ducking block's face and panel both write, so the
// two cannot say the same setting two ways.
QtObject {
    id: words

    /// Which of the block's published values is which: how far down the
    /// background is, in dB, and whether it is ducking (1) or not (0).
    /// Written down again from blocks::Ducking::kDuckingValue and
    /// kActiveValue, as the interface does the ids it cannot include; the
    /// real-PipeWire ducking scenario reads the same two positions.
    readonly property int duckingValue: 0
    readonly property int activeValue: 1

    /// The ducked volume, an amplitude, as the percentage it is shown as.
    function percentText(amplitude: real): string {
        return KI18n.i18nc("@info a volume as a percentage of the sound as it arrived", "%1 %",
                           Math.round(amplitude * 100));
    }

    /// The same in decibels, which is what a person who thinks in them
    /// reads beside it: 40 % is −8.0 dB. Silence has none to give.
    function amplitudeDbText(amplitude: real): string {
        if (amplitude <= 0) {
            return KI18n.i18nc("@info the background is turned all the way down", "silent");
        }
        return words.decibelText(20 * Math.log10(amplitude));
    }

    function decibelText(db: real): string {
        return KI18n.i18nc("@info a level in decibels", "%1 dB", Number(db).toLocaleString(Qt.locale(), 'f', 1));
    }

    function millisecondsText(ms: real): string {
        return KI18n.i18nc("@info a time in milliseconds", "%1 ms", Math.round(ms));
    }
}
