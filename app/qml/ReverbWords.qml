// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The words and numbers the Reverb's face and panel both use, so the
// two cannot say the same setting differently. Times and per cents are
// written as the Echo writes them.
QtObject {
    id: words

    /// The block's ranges and defaults. The interface layer does not depend
    /// on the engine's, so they are written down twice; tst_reverb.qml holds
    /// these against the engine's catalogue.
    readonly property real longestPreDelay: 250
    readonly property real defaultPreDelay: 20
    readonly property real defaultSize: 50
    readonly property real shortestDecay: 100
    readonly property real longestDecay: 20000
    readonly property real defaultDecay: 2000
    readonly property real defaultDamping: 40
    readonly property real defaultWidth: 100
    readonly property real defaultMix: 25

    readonly property EchoWords echoWords: EchoWords {}

    /// A time the way it is said: "20 ms", "2.00 s".
    function timeText(ms: real): string {
        return words.echoWords.timeText(ms);
    }

    function percentText(percent: real): string {
        return words.echoWords.percentText(percent);
    }

    /// Where \a value sits on a logarithmic slider from \a lowest to
    /// \a highest, 0 to 1, and back: a short decay has as much room as a long
    /// one.
    function positionOf(value: real, lowest: real, highest: real): real {
        return words.echoWords.positionOf(value, lowest, highest);
    }

    function valueAt(position: real, lowest: real, highest: real): real {
        return words.echoWords.valueAt(position, lowest, highest);
    }
}
