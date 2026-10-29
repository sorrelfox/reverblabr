// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// Where a Time Shift block's play head is, worked out once for its
// face and its panel, and the transport that moves it.
//
// **The running block says where its head is** (its published values): how
// far behind live, how far back it could go, whether it is paused. Nothing
// is kept while the session is stopped, so there is nowhere to go: the
// transport waits for the session to run.
//
// **A jump is two settings sent as one edit, in order**: how far behind
// live to go, then a new count, which is what the block acts on. Forward
// and back are worked out from where the head is now; forward past live is
// live.
QtObject {
    id: reading

    /// The block this is about.
    required property string blockId

    /// Which of the block's published values is which (TimeShift::
    /// kOffsetValue, kAvailableValue, kPausedValue).
    readonly property int offsetValue: 0
    readonly property int availableValue: 1
    readonly property int pausedValue: 2

    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(reading.blockId) : ({})
    readonly property var levels: Meters.frame >= 0 ? Meters.levels(reading.blockId) : ({})
    readonly property var values: reading.levels.values ?? []
    /// Whether the running block is publishing: the transport works then.
    readonly property bool live: Meters.live && reading.values.length > reading.pausedValue

    readonly property bool paused: reading.params.paused ?? false
    /// Seconds behind live, and the most it could be.
    readonly property real offset: reading.live ? reading.values[reading.offsetValue] : 0
    readonly property real available: reading.live ? reading.values[reading.availableValue] : 0
    readonly property bool atLive: reading.offset < 0.05

    /// Where the head is in a word: "Paused", "Live" or "−30 s".
    readonly property string headline: reading.paused
        ? KI18n.i18nc("@info a Time Shift block holding its play head", "Paused")
        : (reading.atLive ? KI18n.i18nc("@info a Time Shift block playing what arrives as it arrives", "Live")
                          : reading.behind(reading.offset))

    /// \a seconds behind live, as the face and the scrubber say it: "−3.5 s"
    /// under ten seconds, "−42 s" under a minute, "−12:05" beyond.
    function behind(seconds: real): string {
        if (seconds < 0.05) {
            return KI18n.i18nc("@info a Time Shift play head at live", "Live");
        }
        if (seconds < 10) {
            return KI18n.i18nc("@info how far behind live, in seconds, '−3.5 s'", "−%1 s",
                               Number(seconds).toLocaleString(Qt.locale(), 'f', 1));
        }
        if (seconds < 60) {
            return KI18n.i18nc("@info how far behind live, in seconds, '−42 s'", "−%1 s", Math.round(seconds));
        }
        const whole = Math.round(seconds);
        const minutes = Math.floor(whole / 60);
        const rest = whole % 60;
        return KI18n.i18nc("@info how far behind live, in minutes and seconds, '−12:05'", "−%1:%2", minutes,
                           rest < 10 ? "0" + rest : String(rest));
    }

    /// Sends the head to \a seconds behind live, within what is kept.
    function jumpTo(seconds: real) {
        const target = Math.max(0, Math.min(reading.available, seconds));
        SessionModel.setParams(reading.blockId, [["target", target], ["jump", (reading.params.jump ?? 0) + 1]]);
    }

    /// Moves the head \a seconds later (forward) or earlier (a negative
    /// number, back) than it is now.
    function jumpBy(seconds: real) {
        reading.jumpTo(reading.offset - seconds);
    }

    function togglePause() {
        SessionModel.setParam(reading.blockId, "paused", !reading.paused);
    }

    /// Back to live, and playing.
    function goLive() {
        SessionModel.setParams(reading.blockId, [["paused", false], ["target", 0],
                                                 ["jump", (reading.params.jump ?? 0) + 1]]);
    }
}
