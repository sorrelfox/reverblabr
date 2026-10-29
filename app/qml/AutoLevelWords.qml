// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The words and numbers Auto Level's face and panel both use, so the
// two cannot say the same setting differently.
QtObject {
    /// The block's defaults and ranges. The interface layer does not depend
    /// on the engine's, so they are written down twice; tst_autolevel.qml
    /// holds these against the engine's catalogue.
    readonly property real defaultTarget: -20
    readonly property real lowestTarget: -30
    readonly property real highestTarget: -14
    readonly property real defaultMaximum: 18
    readonly property real highestMaximum: 24

    /// Which of the block's published values is the boost it is applying
    /// (AutoLevel::kBoostValue), and which the loudness it measured.
    readonly property int boostValue: 0
    readonly property int loudnessValue: 1

    /// A boost as it is said: "+6.2 dB", "0.0 dB".
    function boostText(db: real): string {
        const tenths = Math.round(db * 10) / 10;
        return tenths > 0
            ? KI18n.i18nc("@info a boost in decibels, '+6.2 dB'", "+%1 dB",
                          Number(tenths).toLocaleString(Qt.locale(), 'f', 1))
            : KI18n.i18nc("@info no boost at all", "0.0 dB");
    }

    /// A loudness as it is said: "−20 LUFS".
    function loudnessText(lufs: real): string {
        return KI18n.i18nc("@info a loudness in loudness units relative to full scale, '-20 LUFS'", "%1 LUFS",
                           Number(Math.round(lufs)).toLocaleString(Qt.locale(), 'f', 0));
    }

    /// A limit in decibels: "18 dB".
    function decibelText(db: real): string {
        return KI18n.i18nc("@info a level in decibels, '18 dB'", "%1 dB",
                           Number(Math.round(db)).toLocaleString(Qt.locale(), 'f', 0));
    }
}
