// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The words the filter blocks' faces and panels share: a
// frequency said and typed the way a person says it, a slope in decibels
// an octave, and a lift or a cut with its sign. One place, so a face and its panel — and one filter block and
// the next — cannot say the same thing two ways.
QtObject {
    id: words

    /// The slopes' stable ids, in the order they are offered. Held against
    /// the catalogue by tst_lowpassfilter.qml, so the two cannot drift.
    readonly property var slopes: ["12dB", "24dB"]

    /// A slope the way it is written on a filter: "12 dB/oct".
    function slopeName(id: string): string {
        switch (id) {
        case "12dB":
            return KI18n.i18nc("@item a filter's slope, decibels per octave", "12 dB/oct");
        case "24dB":
            return KI18n.i18nc("@item a filter's slope, decibels per octave", "24 dB/oct");
        }
        return id;
    }

    /// The same, in words, for a tooltip or a screen reader.
    function slopeDescription(id: string): string {
        switch (id) {
        case "12dB":
            return KI18n.i18nc("@info a filter's slope, read out", "12 dB per octave");
        case "24dB":
            return KI18n.i18nc("@info a filter's slope, read out", "24 dB per octave");
        }
        return id;
    }

    /// A frequency to three figures: 200 Hz, 1.25 kHz, 20.0 kHz. **Three
    /// figures whatever the size**, so the number on a tile keeps its width
    /// while a cutoff is dragged across it.
    function frequencyText(hz: real): string {
        if (hz >= 1000) {
            const kilo = hz / 1000;
            return KI18n.i18nc("@info a frequency in kilohertz", "%1 kHz",
                               Number(kilo).toLocaleString(Qt.locale(), 'f', kilo < 10 ? 2 : 1));
        }
        return KI18n.i18nc("@info a frequency in hertz", "%1 Hz", Number(hz).toLocaleString(Qt.locale(), 'f', 0));
    }

    /// A lift or a cut, to a tenth, with its sign: "+3.0 dB", "-4.5 dB", and
    /// "0 dB" for none, which is the one setting that means "untouched".
    function gainText(db: real): string {
        const tenths = Math.round(db * 10) / 10;
        if (tenths === 0) {
            return KI18n.i18nc("@info a filter gain of nothing", "0 dB");
        }
        const number = Number(tenths).toLocaleString(Qt.locale(), 'f', 1);
        return tenths > 0 ? KI18n.i18nc("@info a filter lifting by some decibels", "+%1 dB", number)
                          : KI18n.i18nc("@info a filter cutting by some decibels; the number has its minus", "%1 dB", number);
    }

    /// A band's frequency as a column of sliders is headed: "31", "125",
    /// "1k", "16k". Short, because ten of them sit side by side.
    function bandLabel(hz: real): string {
        if (hz >= 1000) {
            return KI18n.i18nc("@label a band's frequency in kilohertz, short, over an equaliser slider", "%1k",
                               Number(hz / 1000).toLocaleString(Qt.locale(), 'f', 0));
        }
        return Number(Math.floor(hz)).toLocaleString(Qt.locale(), 'f', 0);
    }

    /// The same with its unit, as a band is named in words: "31 Hz",
    /// "1 kHz", "16 kHz".
    function bandText(hz: real): string {
        if (hz >= 1000) {
            return KI18n.i18nc("@info an equaliser band's frequency in kilohertz", "%1 kHz",
                               Number(hz / 1000).toLocaleString(Qt.locale(), 'f', 0));
        }
        return KI18n.i18nc("@info an equaliser band's frequency in hertz", "%1 Hz",
                           Number(Math.floor(hz)).toLocaleString(Qt.locale(), 'f', 0));
    }

    /// The Parametric EQ's band types' stable ids, in the order its menu
    /// offers them. Held against the catalogue by
    /// tst_parametriceq.qml.
    readonly property var bandTypes: ["peak", "lowShelf", "highShelf", "lowPass", "highPass", "notch", "bandPass"]

    /// A band type in words.
    function bandTypeName(id: string): string {
        switch (id) {
        case "peak":
            return KI18n.i18nc("@item an equaliser band that lifts or cuts around a frequency", "Bell");
        case "lowShelf":
            return KI18n.i18nc("@item an equaliser band that lifts or cuts everything below a frequency", "Low shelf");
        case "highShelf":
            return KI18n.i18nc("@item an equaliser band that lifts or cuts everything above a frequency", "High shelf");
        case "lowPass":
            return KI18n.i18nc("@item an equaliser band that keeps what is below a frequency", "Low-pass");
        case "highPass":
            return KI18n.i18nc("@item an equaliser band that keeps what is above a frequency", "High-pass");
        case "notch":
            return KI18n.i18nc("@item an equaliser band that takes out a narrow band", "Notch");
        case "bandPass":
            return KI18n.i18nc("@item an equaliser band that keeps only a band", "Band-pass");
        }
        return id;
    }

    /// Whether a band type has a gain to set: the bell and the shelves do,
    /// the pass filters, the notch and the band-pass do not.
    function hasGain(id: string): bool {
        return id === "peak" || id === "lowShelf" || id === "highShelf";
    }

    /// A Q, to two figures after the point below 10: "0.71", "1.40", "12.0".
    function qText(q: real): string {
        return Number(q).toLocaleString(Qt.locale(), 'f', q < 10 ? 2 : 1);
    }

    /// Where a frequency sits on a slider from \a lowest to \a highest, from
    /// 0 to 1: logarithmic, as a frequency is heard, so an octave is the
    /// same distance wherever it is.
    function positionOf(hz: real, lowest: real, highest: real): real {
        return Math.log(hz / lowest) / Math.log(highest / lowest);
    }

    function frequencyAt(position: real, lowest: real, highest: real): real {
        return lowest * Math.pow(highest / lowest, position);
    }

    /// Reads what was typed as a frequency in hertz, or NaN. "1k" and "1.5
    /// kHz" are kilohertz, which is how a cutoff is said.
    function parseFrequency(typed: string): real {
        const text = typed.trim().toLowerCase().replace(",", ".");
        const number = parseFloat(text);
        if (isNaN(number)) {
            return NaN;
        }
        return /k/.test(text) ? number * 1000 : number;
    }
}
