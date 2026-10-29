// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The words the Channels block's face and panel both use, so the two
// cannot name an action differently.
//
// **Not from the engine**: the block holds stable ids, and the words are
// the interface's, translated and keyed by them. tst_channels.qml holds the
// list below against the engine's, so the two cannot drift.
QtObject {
    id: words

    /// The actions' stable ids, in the order the block offers them.
    readonly property var actions: ["none", "swap", "mono", "leftToBoth", "rightToBoth", "muteLeft", "muteRight",
        "invertLeft", "invertRight", "difference"]

    /// What the list and the tile call an action.
    function actionName(id: string): string {
        switch (id) {
        case "none":
            return KI18n.i18nc("@item a Channels action that leaves the two sides alone", "No change");
        case "swap":
            return KI18n.i18nc("@item a Channels action", "Swap sides");
        case "mono":
            return KI18n.i18nc("@item a Channels action", "Mono");
        case "leftToBoth":
            return KI18n.i18nc("@item a Channels action", "Left to both");
        case "rightToBoth":
            return KI18n.i18nc("@item a Channels action", "Right to both");
        case "muteLeft":
            return KI18n.i18nc("@item a Channels action", "Mute left");
        case "muteRight":
            return KI18n.i18nc("@item a Channels action", "Mute right");
        case "invertLeft":
            return KI18n.i18nc("@item a Channels action: flip the polarity of the left side", "Invert left");
        case "invertRight":
            return KI18n.i18nc("@item a Channels action: flip the polarity of the right side", "Invert right");
        case "difference":
            return KI18n.i18nc("@item a Channels action: the left side minus the right", "Difference");
        }
        return id;
    }

    /// Where each side comes from, the tile's caption: "L→R · R→L".
    function routing(id: string): string {
        switch (id) {
        case "none":
            return KI18n.i18nc("@info where each output side comes from: unchanged", "L→L · R→R");
        case "swap":
            return KI18n.i18nc("@info where each output side comes from: swapped", "R→L · L→R");
        case "mono":
            return KI18n.i18nc("@info where each output side comes from: the average of both", "L+R → both");
        case "leftToBoth":
            return KI18n.i18nc("@info where each output side comes from: the left", "L → both");
        case "rightToBoth":
            return KI18n.i18nc("@info where each output side comes from: the right", "R → both");
        case "muteLeft":
            return KI18n.i18nc("@info where each output side comes from: left silent", "L off · R→R");
        case "muteRight":
            return KI18n.i18nc("@info where each output side comes from: right silent", "L→L · R off");
        case "invertLeft":
            return KI18n.i18nc("@info where each output side comes from: left inverted", "−L→L · R→R");
        case "invertRight":
            return KI18n.i18nc("@info where each output side comes from: right inverted", "L→L · −R→R");
        case "difference":
            return KI18n.i18nc("@info where each output side comes from: left minus right", "L−R → both");
        }
        return "";
    }

    /// What an action does and what it is for, in one line under the list.
    function explanation(id: string): string {
        switch (id) {
        case "none":
            return KI18n.i18nc("@info what the Channels action does", "Passes both sides through as they arrive.");
        case "swap":
            return KI18n.i18nc("@info what the Channels action does",
                               "Puts the left side on the right and the right on the left.");
        case "mono":
            return KI18n.i18nc("@info what the Channels action does",
                               "Averages the two sides and plays the result on both.");
        case "leftToBoth":
            return KI18n.i18nc("@info what the Channels action does",
                               "Plays the left side on both, for a source that only arrives on one.");
        case "rightToBoth":
            return KI18n.i18nc("@info what the Channels action does",
                               "Plays the right side on both, for a source that only arrives on one.");
        case "muteLeft":
            return KI18n.i18nc("@info what the Channels action does",
                               "Silences the left side and leaves the right as it is.");
        case "muteRight":
            return KI18n.i18nc("@info what the Channels action does",
                               "Silences the right side and leaves the left as it is.");
        case "invertLeft":
            return KI18n.i18nc("@info what the Channels action does",
                               "Flips the left side upside down, for a cable or microphone wired the wrong way round.");
        case "invertRight":
            return KI18n.i18nc("@info what the Channels action does",
                               "Flips the right side upside down, for a cable or microphone wired the wrong way round.");
        case "difference":
            return KI18n.i18nc("@info what the Channels action does",
                               "Plays what differs between the sides on both. Whatever sits in the middle, often a voice, drops out.");
        }
        return "";
    }
}
