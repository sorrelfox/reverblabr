// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma Singleton

import QtQuick

// What each Auto Level's gain did over the last minute, for the range line on
// its card. **Kept in the window, from the meter, and never saved**: the tile
// lives as long as the block does, so a card opened now can say what went
// before it opened. A second at a time, so a block costs sixty small entries.
QtObject {
    id: history

    readonly property int keptSeconds: 60

    /// Counts up as seconds fill, for a binding to watch.
    property int revision: 0
    /// Each block's seconds, oldest first, as `{ at, low, high }`.
    property var kept: ({})

    /// A block's gain at \a nowMs.
    function note(blockId: string, gainDb: real, nowMs: real) {
        const second = Math.floor(nowMs / 1000);
        const seconds = (history.kept[blockId] ?? []).filter(entry => entry.at > second - history.keptSeconds);
        const last = seconds[seconds.length - 1];
        if (last !== undefined && last.at === second) {
            last.low = Math.min(last.low, gainDb);
            last.high = Math.max(last.high, gainDb);
        } else {
            seconds.push({ at: second, low: gainDb, high: gainDb });
        }
        history.kept[blockId] = seconds;
        ++history.revision;
    }

    /// The lowest and highest gain of the last minute as `{ low, high }`, or
    /// null when nothing was heard in it.
    function rangeOf(blockId: string, nowMs: real): var {
        const second = Math.floor(nowMs / 1000);
        const seconds = (history.kept[blockId] ?? []).filter(entry => entry.at > second - history.keptSeconds);
        if (seconds.length === 0) {
            return null;
        }
        return { low: Math.min(...seconds.map(entry => entry.low)), high: Math.max(...seconds.map(entry => entry.high)) };
    }

    /// Forgets a block, for a test that starts afresh.
    function forget(blockId: string) {
        delete history.kept[blockId];
        ++history.revision;
    }
}
