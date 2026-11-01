// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// What the Speech Now block says, in one place, so that the tile, the card,
// the tray, the floating label and the spoken sentence can't disagree.
//
// **Only what the sound did.** Nothing here says a talker has finished or that
// anybody may speak. The block knows a lane's sound and not a conversation.
QtObject {
    id: words

    /// The block's published states (SpeechNow::kStateValue).
    readonly property int quietState: 0
    readonly property int speakingState: 1
    readonly property int quietAgainState: 2

    /// What can be shown of a block now: "speaking", "again", "quiet",
    /// "notWatching" or "off". \a watch is Meters.watchState()'s word.
    function kindOf(watch: string, enabled: bool, state: int): string {
        if (!enabled) {
            return "off";
        }
        if (watch !== "watching") {
            return "notWatching";
        }
        if (state === words.speakingState) {
            return "speaking";
        }
        return state === words.quietAgainState ? "again" : "quiet";
    }

    function word(kind: string): string {
        switch (kind) {
        case "speaking":
            return KI18n.i18nc("@info a voice is on this lane now", "Speaking");
        case "again":
            return KI18n.i18nc("@info a voice on this lane has just stopped", "Quiet again");
        case "quiet":
            return KI18n.i18nc("@info no voice on this lane", "Quiet");
        case "notWatching":
            return KI18n.i18nc("@info a Speech Now that cannot hear its lane", "Not watching");
        }
        return KI18n.i18nc("@info a Speech Now that is switched off", "Off");
    }

    /// The tile's dot: filled for a voice, and never a verdict on a level.
    function dot(kind: string): string {
        if (kind === "speaking") {
            return "live";
        }
        return kind === "again" || kind === "quiet" ? "ok" : "off";
    }

    /// Why nothing is being watched, from Meters.watchState()'s word.
    function reason(watch: string): string {
        switch (watch) {
        case "overloaded":
            return KI18n.i18nc("@info a session that gave way to overload", "Session overloaded");
        case "noNews":
            return KI18n.i18nc("@info the engine has stopped saying anything", "Engine not answering");
        }
        return KI18n.i18nc("@info a Speech Now whose session is not running", "Session stopped");
    }

    /// The detector in use, from what the block publishes.
    function detectorName(reading: int): string {
        return reading === words.modelListening ? words.modelName : words.levelName;
    }

    readonly property int modelListening: 0
    readonly property int levelChosen: 1
    readonly property int noModel: 2
    readonly property int wrongRate: 3

    readonly property string modelName: KI18n.i18nc("@item:inlistbox how Speech Now listens", "Voice model")
    readonly property string levelName: KI18n.i18nc("@item:inlistbox how Speech Now listens", "Level and rhythm")

    /// Why the voice model can't be picked, or nothing.
    function modelRefused(reading: int): string {
        if (reading === words.noModel) {
            return KI18n.i18nc("@info:tooltip why Voice model can't be chosen", "Not in this build");
        }
        if (reading === words.wrongRate) {
            return KI18n.i18nc("@info:tooltip why Voice model can't be chosen", "Needs 48 kHz");
        }
        return "";
    }

    /// The word for a choice's stable id (ChoiceWords).
    function detectorChoice(id: string): string {
        return id === "levelAndRhythm" ? words.levelName : id === "model" ? words.modelName : "";
    }

    /// The floating label's sizes.
    function sizeName(id: string): string {
        return id === "large" ? KI18n.i18nc("@item:inlistbox a size of label", "Large")
                              : KI18n.i18nc("@item:inlistbox a size of label", "Small");
    }

    /// One name's part of the sentence a screen reader says.
    function spokenPart(name: string, kind: string): string {
        switch (kind) {
        case "speaking":
            return KI18n.i18nc("@info:status spoken, %1 is what a block is called", "%1 speaking", name);
        case "again":
            return KI18n.i18nc("@info:status spoken, %1 is what a block is called", "%1 quiet again", name);
        case "quiet":
            return KI18n.i18nc("@info:status spoken, %1 is what a block is called", "%1 quiet", name);
        case "notWatching":
            return KI18n.i18nc("@info:status spoken, %1 is what a block is called", "%1 not watching", name);
        }
        return KI18n.i18nc("@info:status spoken, %1 is what a block is called", "%1 off", name);
    }

    function percent(share: real): string {
        return KI18n.i18nc("@info how likely a voice is, in percent", "%1 %", Math.round(share * 100));
    }

    function milliseconds(value: real): string {
        return KI18n.i18nc("@info a length of time", "%1 ms", Math.round(value));
    }

    function seconds(value: real): string {
        return KI18n.i18nc("@info a length of time", "%1 s", value.toLocaleString(Qt.locale(), 'f', 1));
    }
}
