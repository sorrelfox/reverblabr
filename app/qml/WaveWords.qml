// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The words the Wave block's face and panel both use, so the two
// cannot name the same setting differently.
//
// **Not from the engine**: the catalogue holds stable ids and the words are
// the interface's, translated and keyed by them.
QtObject {
    id: words

    /// The waveforms' stable ids, in the order the block offers them. Held
    /// against the catalogue by tst_wave.qml, so the two cannot drift.
    readonly property var shapes: ["sine", "square", "triangle", "saw", "white", "pink"]
    /// Where the signal can go, in the order a person reads them across.
    readonly property var sides: ["left", "both", "right"]

    function shapeName(id: string): string {
        switch (id) {
        case "sine":
            return KI18n.i18nc("@item a waveform", "Sine");
        case "square":
            return KI18n.i18nc("@item a waveform", "Square");
        case "triangle":
            return KI18n.i18nc("@item a waveform", "Triangle");
        case "saw":
            return KI18n.i18nc("@item a waveform", "Sawtooth");
        case "white":
            return KI18n.i18nc("@item a kind of noise", "White noise");
        case "pink":
            return KI18n.i18nc("@item a kind of noise", "Pink noise");
        }
        return id;
    }

    function sideName(id: string): string {
        switch (id) {
        case "left":
            return KI18n.i18nc("@item the signal goes to the left channel only", "Left");
        case "right":
            return KI18n.i18nc("@item the signal goes to the right channel only", "Right");
        case "both":
            return KI18n.i18nc("@item the signal goes to both channels", "Both");
        }
        return id;
    }

    /// Whether a shape has a frequency at all: noise has none.
    function isTone(id: string): bool {
        return id !== "white" && id !== "pink";
    }

    /// A frequency the way it is said: 440 Hz, 1 kHz, 12.5 kHz.
    function frequencyText(hz: real): string {
        if (hz >= 1000) {
            const kilo = hz / 1000;
            const digits = Math.abs(kilo - Math.round(kilo)) < 0.005 ? 0 : (kilo < 10 ? 2 : 1);
            return KI18n.i18nc("@info a frequency in kilohertz", "%1 kHz",
                               Number(kilo).toLocaleString(Qt.locale(), 'f', digits));
        }
        const digits = Math.abs(hz - Math.round(hz)) < 0.05 ? 0 : 1;
        return KI18n.i18nc("@info a frequency in hertz", "%1 Hz", Number(hz).toLocaleString(Qt.locale(), 'f', digits));
    }

    /// A level in dBFS, to a tenth.
    function levelText(db: real): string {
        return KI18n.i18nc("@info a level in decibels below full scale", "%1 dB",
                           Number(db).toLocaleString(Qt.locale(), 'f', 1));
    }
}
