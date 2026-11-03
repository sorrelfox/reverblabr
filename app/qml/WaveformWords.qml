// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The words of a Waveform block's tile and card, so that the two cannot name
// one setting differently. The catalogue holds stable ids and the words are
// the interface's, translated and keyed by them. tst_waveform.qml holds the
// lists against the catalogue.
QtObject {
    id: words

    readonly property var refreshRates: ["hz10", "hz15", "hz20", "hz30", "hz60", "hz120"]
    readonly property var motions: ["realTime", "smooth"]
    readonly property var styles: ["line", "filled", "bars", "dots"]
    readonly property var channelChoices: ["both", "left", "right", "mid", "side", "stacked"]
    readonly property var scales: ["linear", "decibels"]

    /// A refresh rate's id as a number.
    function hertzOf(id: string): real {
        return Number(id.replace("hz", ""));
    }

    /// A refresh rate's number, written as the reader writes numbers.
    function refreshName(id: string): string {
        const hertz = words.hertzOf(id);
        return Number.isNaN(hertz) ? "" : hertz.toLocaleString(Qt.locale(), "f", 0);
    }

    function motionName(id: string): string {
        switch (id) {
        case "realTime":
            return KI18n.i18nc("@item a Waveform shows each refresh as it is", "Real-time");
        case "smooth":
            return KI18n.i18nc("@item a Waveform eases and flows between refreshes", "Smooth");
        }
        return "";
    }

    function motionTip(id: string): string {
        switch (id) {
        case "realTime":
            return KI18n.i18nc("@info:tooltip", "Shows the newest sound as it is, once each refresh");
        case "smooth":
            return KI18n.i18nc("@info:tooltip", "Eases from one refresh to the next and flows at the rate of your screen");
        }
        return "";
    }

    function styleName(id: string): string {
        switch (id) {
        case "line":
            return KI18n.i18nc("@item how a Waveform is drawn", "Line");
        case "filled":
            return KI18n.i18nc("@item how a Waveform is drawn", "Filled");
        case "bars":
            return KI18n.i18nc("@item how a Waveform is drawn", "Bars");
        case "dots":
            return KI18n.i18nc("@item how a Waveform is drawn", "Dots");
        }
        return "";
    }

    function channelsName(id: string): string {
        switch (id) {
        case "both":
            return KI18n.i18nc("@item a Waveform draws left and right over each other", "Both");
        case "left":
            return KI18n.i18nc("@item a Waveform draws the left channel", "Left");
        case "right":
            return KI18n.i18nc("@item a Waveform draws the right channel", "Right");
        case "mid":
            return KI18n.i18nc("@item a Waveform draws what left and right have in common", "Mid");
        case "side":
            return KI18n.i18nc("@item a Waveform draws where left and right differ", "Side");
        case "stacked":
            return KI18n.i18nc("@item a Waveform draws left above right", "Stacked");
        }
        return "";
    }

    function scaleName(id: string): string {
        switch (id) {
        case "linear":
            return KI18n.i18nc("@item the height of a Waveform is the amplitude", "Linear");
        case "decibels":
            return KI18n.i18nc("@item the height of a Waveform is in decibels", "Decibels");
        }
        return "";
    }

    /// A stretch of time, in the unit that reads best.
    function spanText(ms: real): string {
        return ms >= 1000 ? KI18n.i18nc("@info a stretch of time in seconds", "%1 s", Math.round(ms / 100) / 10)
                          : KI18n.i18nc("@info a stretch of time in milliseconds", "%1 ms", Math.round(ms));
    }

    function gainText(db: real): string {
        return KI18n.i18nc("@info how much a trace is enlarged, in decibels", "%1 dB", Math.round(db * 10) / 10);
    }

    /// Why no trace is shown, for the words under one: \a state is
    /// ScopeItem's. Nothing for a trace that is.
    function absence(state: string): string {
        switch (state) {
        case "waiting":
            return KI18n.i18nc("@info a Waveform has asked for the sound and it is not there yet", "Waiting for sound");
        case "noRoom":
            return KI18n.i18nc("@info every Waveform slot of the session is in use", "Too many Waveforms are showing");
        case "stopped":
            return KI18n.i18nc("@info a Waveform in a session that is not running", "Not running");
        case "off":
            return KI18n.i18nc("@info a Waveform block that is switched off", "Off");
        case "overloaded":
            return KI18n.i18nc("@info a Waveform in a session that has given way to overload", "Overloaded");
        case "noNews":
            return KI18n.i18nc("@info a Waveform with no sound arriving and no word why", "No sound is arriving");
        }
        return "";
    }

    /// What is said of the block now, short enough for a tile.
    function status(state: string, held: bool): string {
        if (held) {
            return KI18n.i18nc("@info a Waveform keeps the trace it has", "Held");
        }
        return state === "live" ? KI18n.i18nc("@info a Waveform is drawing the sound as it passes", "Live")
                                : words.absence(state);
    }

    /// The tile's dot: "live", "warning" or "off".
    function dot(state: string, held: bool): string {
        if (held) {
            return "off";
        }
        return state === "live" ? "live" : state === "overloaded" || state === "noNews" ? "warning" : "off";
    }
}
