// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma Singleton

import QtQuick

// What the window says to a screen reader on its own, unasked: a
// meter that has clipped, and the like.
//
// **One voice for the whole window, and not a chatty one.** A session with
// twenty meters that all clip on the same drum hit must not read out twenty
// sentences; somebody listening needs to hear that something clipped, and
// where, and then be left alone to fix it. So nothing is said within
// `gapMs` of the last thing, and nothing about the same `key` within
// `repeatMs`.
QtObject {
    id: announcer

    readonly property int gapMs: 1000
    readonly property int repeatMs: 5000

    /// What was said last, for a test to read.
    property string last: ""
    property double lastAt: 0
    property var lastByKey: ({})

    /// Says \a message through \a item, unless something was said too
    /// recently, or the same thing about \a key. Answers whether it was said.
    function say(item: Item, key: string, message: string): bool {
        const now = Date.now();
        if (now - announcer.lastAt < announcer.gapMs || now - (announcer.lastByKey[key] ?? 0) < announcer.repeatMs) {
            return false;
        }
        const said = announcer.lastByKey;
        said[key] = now;
        announcer.lastByKey = said;
        announcer.lastAt = now;
        announcer.last = message;
        item.Accessible.announce(message);
        return true;
    }

    /// Forgets what was said and when, for a test that starts afresh.
    function reset() {
        announcer.last = "";
        announcer.lastAt = 0;
        announcer.lastByKey = {};
    }
}
