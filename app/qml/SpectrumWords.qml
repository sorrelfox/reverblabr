// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The words of a Spectrum block's tile and card, so that the two cannot name
// one setting differently. The catalogue holds stable ids and the words are
// the interface's, translated and keyed by them. tst_spectrum.qml holds the
// lists against the catalogue. What a Spectrum has in common with a Waveform
// is said in one place, WaveformWords.
QtObject {
    id: words

    readonly property WaveformWords scope: WaveformWords {}

    readonly property var refreshRates: words.scope.refreshRates
    readonly property var motions: words.scope.motions
    readonly property var barCounts: ["bars16", "bars32", "bars64", "bars96", "bars128"]
    readonly property var scales: ["logarithmic", "linear"]
    readonly property var fftSizes: ["fft1024", "fft2048", "fft4096", "fft8192"]
    readonly property var floors: ["db60", "db80", "db100", "db120"]
    readonly property var tilts: ["tilt0", "tilt3", "tilt4p5"]
    readonly property var channelChoices: ["mix", "left", "right"]
    readonly property var styles: ["solid", "segmented"]

    function hertzOf(id: string): real {
        return words.scope.hertzOf(id);
    }

    function refreshName(id: string): string {
        return words.scope.refreshName(id);
    }

    function barCountOf(id: string): int {
        return Number(id.replace("bars", ""));
    }

    function fftSizeOf(id: string): int {
        return Number(id.replace("fft", ""));
    }

    /// The bottom of the view, in dB.
    function floorDbOf(id: string): real {
        return -Number(id.replace("db", ""));
    }

    /// How much the bars lean, in dB an octave.
    function tiltDbOf(id: string): real {
        return Number(id.replace("tilt", "").replace("p", "."));
    }

    function motionName(id: string): string {
        switch (id) {
        case "realTime":
            return KI18n.i18nc("@item a Spectrum shows each refresh as it is", "Real-time");
        case "smooth":
            return KI18n.i18nc("@item a Spectrum's bars rise quickly and ease down between refreshes", "Smooth");
        }
        return "";
    }

    function motionTip(id: string): string {
        switch (id) {
        case "realTime":
            return KI18n.i18nc("@info:tooltip", "Shows each refresh's bars as they are");
        case "smooth":
            return KI18n.i18nc("@info:tooltip", "Moves the bars at the rate of your screen: a quick rise and an eased fall");
        }
        return "";
    }

    function barsName(id: string): string {
        const count = words.barCountOf(id);
        return words.barCounts.indexOf(id) < 0 ? "" : count.toLocaleString(Qt.locale(), "f", 0);
    }

    function scaleName(id: string): string {
        switch (id) {
        case "logarithmic":
            return KI18n.i18nc("@item a Spectrum gives each octave the same width", "Logarithmic");
        case "linear":
            return KI18n.i18nc("@item a Spectrum gives each hertz the same width", "Linear");
        }
        return "";
    }

    function fftSizeName(id: string): string {
        // Plain, with no thousands separator: these are powers of two that
        // are known by their digits.
        return words.fftSizes.indexOf(id) < 0 ? "" : String(words.fftSizeOf(id));
    }

    function floorName(id: string): string {
        return words.floors.indexOf(id) < 0 ? ""
            : KI18n.i18nc("@item the bottom of a Spectrum's view, in decibels below full scale", "%1 dBFS", words.floorDbOf(id));
    }

    function tiltName(id: string): string {
        switch (id) {
        case "tilt0":
            return KI18n.i18nc("@item a Spectrum shows the levels as they are", "None");
        case "tilt3":
        case "tilt4p5":
            return KI18n.i18nc("@item a Spectrum lifts high frequencies by this many decibels an octave", "%1 dB/oct",
                               words.tiltDbOf(id).toLocaleString(Qt.locale(), "g", 2));
        }
        return "";
    }

    function channelsName(id: string): string {
        switch (id) {
        case "mix":
            return KI18n.i18nc("@item a Spectrum reads left and right mixed together", "Mix");
        case "left":
            return KI18n.i18nc("@item a Spectrum reads the left channel", "Left");
        case "right":
            return KI18n.i18nc("@item a Spectrum reads the right channel", "Right");
        }
        return "";
    }

    function styleName(id: string): string {
        switch (id) {
        case "solid":
            return KI18n.i18nc("@item how a Spectrum's bars are drawn", "Solid");
        case "segmented":
            return KI18n.i18nc("@item how a Spectrum's bars are drawn, in lit steps like a row of lamps", "Segmented");
        }
        return "";
    }

    /// A frequency on the grid, in the unit that reads best.
    function frequencyText(hz: real): string {
        return hz >= 1000 ? KI18n.i18nc("@info a frequency on a Spectrum's grid, in kilohertz", "%1 kHz", hz / 1000)
                          : KI18n.i18nc("@info a frequency on a Spectrum's grid, in hertz", "%1 Hz", hz);
    }

    /// A level on the grid.
    function levelText(db: real): string {
        return Number(db).toLocaleString(Qt.locale(), "f", 0);
    }

    /// Why no bars are shown: \a state is SpectrumItem's. Nothing for bars
    /// that are.
    function absence(state: string): string {
        return words.scope.absence(state);
    }

    /// What is said of the block now, short enough for a tile.
    function status(state: string, held: bool): string {
        if (held) {
            return KI18n.i18nc("@info a Spectrum keeps the bars it has", "Held");
        }
        return state === "live" ? KI18n.i18nc("@info a Spectrum is showing the sound as it passes", "Live")
                                : words.absence(state);
    }

    /// The tile's dot: "live", "warning" or "off".
    function dot(state: string, held: bool): string {
        return words.scope.dot(state, held);
    }
}
