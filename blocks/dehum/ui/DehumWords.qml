// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The words the Dehum block's face and panel share.
QtObject {
    /// A base frequency, to a tenth: "50.0 Hz", "59.4 Hz".
    function baseText(hz: real): string {
        return KI18n.i18nc("@info a mains hum's base frequency", "%1 Hz", Number(hz).toLocaleString(Qt.locale(), 'f', 1));
    }

    /// The base Auto has the notches on, to a hundredth: "50.03 Hz".
    function trackedText(hz: real): string {
        return KI18n.i18nc("@info a hum's base frequency as a Dehum block follows it", "%1 Hz", Number(hz).toLocaleString(Qt.locale(), 'f', 2));
    }

    /// What Auto is doing: waiting to run, following a hum, or holding
    /// where it was with no hum to follow.
    function autoStatus(live: bool, locked: bool, hz: real): string {
        if (!live) {
            return KI18n.i18nc("@info a Dehum block with Auto on, before its session runs", "While it runs, it follows the hum, starting from Base.");
        }
        return locked ? KI18n.i18nc("@info a Dehum block locked onto a hum; %1 is its frequency", "Following the hum at %1", trackedText(hz))
                      : KI18n.i18nc("@info a Dehum block with Auto on and no hum to follow; %1 is where it holds", "No hum to follow: holding at %1", trackedText(hz));
    }

    /// A mains supply's frequency, whole: "50 Hz".
    function mainsText(hz: real): string {
        return KI18n.i18nc("@item a mains supply's frequency", "%1 Hz", Math.round(hz));
    }

    /// The amount, to a tenth, as the slider reads: "2.3".
    function amountText(amount: real): string {
        return Number(amount).toLocaleString(Qt.locale(), 'f', 1);
    }

    /// How deep that amount takes the notches: "14 dB down", or "Off".
    function depthText(amount: real): string {
        const db = Math.round(amount * 6);
        return db <= 0 ? KI18n.i18nc("@info a Dehum block that takes nothing out", "Off")
                       : KI18n.i18nc("@info how deep a Dehum block's notches go", "%1 dB down", db);
    }
}
