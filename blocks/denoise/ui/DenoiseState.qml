// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// Where a Denoise block stands, worked out once for its face and its
// panel: whether it is listening, has a noise to take out, or has none; and
// the words for each.
//
// **While the session runs, the block says so itself** (its published
// state); a noise learned in this run is not in the session's settings
// until they are next saved. **While it does not, the saved profile says**:
// a profile's text records how many seconds it was learned from.
//
// **A noise the block's first version learned is not used** ("v1", an
// average where the block now keeps a fingerprint): the block has nothing
// to take out until the noise is learned again, and says so.
QtObject {
    id: reading

    /// The block this is about.
    required property string blockId

    /// Which of the block's published values is which
    /// (Denoise::kStateValue, kProgressValue, kLearnedSecondsValue), and
    /// what the state's numbers mean.
    readonly property int stateValue: 0
    readonly property int progressValue: 1
    readonly property int secondsValue: 2
    readonly property int learning: 1
    readonly property int active: 2

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(reading.blockId) : ({})
    readonly property string profile: reading.params.profile ?? ""
    readonly property var levels: Meters.frame >= 0 ? Meters.levels(reading.blockId) : ({})
    readonly property var values: reading.levels.values ?? []
    /// Whether the running block is publishing.
    readonly property bool live: Meters.live && reading.values.length > reading.secondsValue

    /// Whether the saved noise is one an earlier version learned, which the
    /// block does not use (Denoise::decodeProfile).
    readonly property bool isOutdated: reading.profile.startsWith("v1;")

    readonly property bool isLearning: reading.live && Math.round(reading.values[reading.stateValue]) === reading.learning
    readonly property bool isActive: reading.live ? Math.round(reading.values[reading.stateValue]) === reading.active
                                                  : reading.profile.startsWith("v2;")
    readonly property real progress: reading.isLearning ? reading.values[reading.progressValue] : 0
    readonly property real seconds: reading.live ? reading.values[reading.secondsValue] : reading.savedSeconds
    /// The seconds a saved profile's text says it was learned from.
    readonly property real savedSeconds: {
        const found = /;seconds=([0-9.]+);/.exec(reading.profile);
        return found ? Number(found[1]) : 0;
    }

    /// The state in a word: "Learning", "Active", "Inactive".
    readonly property string headline: reading.isLearning
        ? KI18n.i18nc("@info a Denoise block listening to the noise", "Learning")
        : (reading.isActive ? KI18n.i18nc("@info a Denoise block taking a learned noise out", "Active")
                            : KI18n.i18nc("@info a Denoise block with no noise learned", "Inactive"))
    /// The line under it: how far through, how much was learned, or none.
    readonly property string detail: reading.isLearning
        ? KI18n.i18nc("@info how far a Denoise block is through learning, '40 %'", "Listening… %1 %",
                      Math.round(reading.progress * 100))
        : (reading.isActive
           ? KI18n.i18nc("@info how much noise a Denoise block learned, '10.0 s noise learned'", "%1 s noise learned",
                         Number(reading.seconds).toLocaleString(Qt.locale(), 'f', 1))
           : (reading.isOutdated
              ? KI18n.i18nc("@info a Denoise block whose saved noise an earlier version learned, and which it does not use",
                            "Learn the noise again")
              : KI18n.i18nc("@info a Denoise block with no noise learned", "No noise learned")))

    /// Asks the block to listen, if the session is running to hear.
    function learn() {
        SessionModel.setParam(reading.blockId, "learn", (reading.params.learn ?? 0) + 1);
    }

    /// Drops the noise: the saved one, and the one the running block holds.
    function forget() {
        SessionModel.setParam(reading.blockId, "profile", "");
        SessionModel.setParam(reading.blockId, "forget", (reading.params.forget ?? 0) + 1);
    }
}
