// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

// The 10-Band EQ's bands as its face and its panel both read them:
// which parameter each one is, and where it is centred.
//
// **Written down twice**, here and in the engine, because the interface does
// not depend on the engine's code; tst_equalizer10.qml holds the ids against
// the catalogue so the two cannot drift.
QtObject {
    readonly property var ids: ["gain31", "gain63", "gain125", "gain250", "gain500", "gain1k", "gain2k", "gain4k",
        "gain8k", "gain16k"]
    readonly property var frequencies: [31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]
    /// The most a band or the preamp lifts or cuts, in dB.
    readonly property real rangeDb: 12
}
