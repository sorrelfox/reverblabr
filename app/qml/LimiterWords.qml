// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The words and numbers the Limiter's face and panel both use, so the
// two cannot say the same setting differently.
QtObject {
    /// The block's defaults and ranges. The interface layer does not depend
    /// on the engine's, so they are written down twice; tst_limiter.qml
    /// holds these against the engine's catalogue.
    readonly property real defaultCeiling: -1
    readonly property real lowestCeiling: -12
    readonly property real defaultRelease: 100
    readonly property real shortestRelease: 10
    readonly property real longestRelease: 1000

    /// Which of the block's published values is its gain reduction
    /// (Limiter::kGainReductionValue).
    readonly property int reductionValue: 0
    /// How much reduction fills the face's track: as far as the ceiling
    /// itself reaches down.
    readonly property real fullReduction: 12

    /// A ceiling as it is said: "−1.0 dBTP" for a true peak, "−1.0 dBFS"
    /// for a sample peak.
    function ceilingText(db: real, truePeak: bool): string {
        const number = Number(db).toLocaleString(Qt.locale(), 'f', 1);
        return truePeak ? KI18n.i18nc("@info a ceiling as a true peak, '-1.0 dBTP'", "%1 dBTP", number)
                        : KI18n.i18nc("@info a ceiling as a sample peak, '-1.0 dBFS'", "%1 dBFS", number);
    }

    /// A level in tenths of a decibel: "-1.0 dB".
    function decibelText(db: real): string {
        return KI18n.i18nc("@info a level in decibels, '-1.0 dB'", "%1 dB", Number(db).toLocaleString(Qt.locale(), 'f', 1));
    }

    /// A reduction as it is said: "-3.2 dB", "0.0 dB" — a gain, written the
    /// way the ceiling above it is.
    function reductionText(db: real): string {
        const tenths = Math.round(db * 10) / 10;
        return tenths > 0
            ? KI18n.i18nc("@info the gain taken off, '-3.2 dB'", "%1 dB",
                          Number(-tenths).toLocaleString(Qt.locale(), 'f', 1))
            : KI18n.i18nc("@info no gain taken off", "0.0 dB");
    }

    /// A release time: "100 ms", "1.00 s".
    function releaseText(ms: real): string {
        return ms >= 1000
            ? KI18n.i18nc("@info a time in seconds", "%1 s", Number(ms / 1000).toLocaleString(Qt.locale(), 'f', 2))
            : KI18n.i18nc("@info a time in milliseconds", "%1 ms", Number(Math.round(ms)).toLocaleString(Qt.locale(), 'f', 0));
    }

    /// Where a release sits on its slider, from 0 to 1: logarithmic, so the
    /// short times a limiter mostly wants have room.
    function releasePosition(ms: real): real {
        return Math.log(ms / shortestRelease) / Math.log(longestRelease / shortestRelease);
    }

    function releaseAt(position: real): real {
        return shortestRelease * Math.pow(longestRelease / shortestRelease, position);
    }
}
