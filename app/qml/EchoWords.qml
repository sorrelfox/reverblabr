// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The words and numbers the Echo's face and panel both use, so the two
// cannot say the same setting differently.
QtObject {
    /// The block's ranges and defaults. The interface layer does not depend
    /// on the engine's, so they are written down twice; tst_echo.qml holds
    /// these against the engine's catalogue.
    readonly property real shortestTime: 1
    readonly property real longestTime: 2000
    readonly property real defaultTime: 350
    readonly property real highestFeedback: 95
    readonly property real defaultFeedback: 40
    readonly property real lowestTone: 500
    readonly property real highestTone: 20000
    readonly property real defaultTone: 5000
    readonly property real defaultMix: 35

    /// A delay time the way it is said: "350 ms", "1.25 s".
    function timeText(ms: real): string {
        if (ms >= 1000) {
            return KI18n.i18nc("@info a time in seconds", "%1 s", Number(ms / 1000).toLocaleString(Qt.locale(), 'f', 2));
        }
        return KI18n.i18nc("@info a time in milliseconds", "%1 ms",
                           Number(Math.round(ms)).toLocaleString(Qt.locale(), 'f', 0));
    }

    function percentText(percent: real): string {
        return KI18n.i18nc("@info a percentage", "%1 %", Number(Math.round(percent)).toLocaleString(Qt.locale(), 'f', 0));
    }

    /// Where \a value sits on a slider from \a lowest to \a highest, 0 to 1:
    /// logarithmic, so a short time or a low tone has as much room as a
    /// long or a high one.
    function positionOf(value: real, lowest: real, highest: real): real {
        return Math.log(value / lowest) / Math.log(highest / lowest);
    }

    function valueAt(position: real, lowest: real, highest: real): real {
        return lowest * Math.pow(highest / lowest, position);
    }
}
