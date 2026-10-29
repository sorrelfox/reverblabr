// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The words the Simple Compressor's face and panel both use, so the
// two cannot name a mode differently.
//
// **The ids and the numbers come from the block** (CompressorModes); the
// names and the one line saying what each mode is for are the interface's,
// translated and keyed by those ids.
QtObject {
    id: words

    /// The modes' stable ids, in the order the block offers them.
    readonly property var modes: CompressorModes.ids

    function modeName(id: string): string {
        switch (id) {
        case "off":
            return KI18n.i18nc("@item a Simple Compressor mode: not compressing", "Off");
        case "gentle":
            return KI18n.i18nc("@item a Simple Compressor mode", "Gentle");
        case "moderate":
            return KI18n.i18nc("@item a Simple Compressor mode", "Moderate");
        case "heavy":
            return KI18n.i18nc("@item a Simple Compressor mode", "Heavy");
        case "voice":
            return KI18n.i18nc("@item a Simple Compressor mode for speech", "Voice");
        case "broadcast":
            return KI18n.i18nc("@item a Simple Compressor mode for a dense, even sound", "Broadcast");
        }
        return id;
    }

    /// What a mode is for, in one line under the list.
    function explanation(id: string): string {
        switch (id) {
        case "off":
            return KI18n.i18nc("@info what the Simple Compressor's Off mode does",
                               "Passes the sound through exactly as it arrives.");
        case "gentle":
            return KI18n.i18nc("@info what the Simple Compressor's Gentle mode does",
                               "Takes the edge off the loudest moments. Hard to hear working, which suits music.");
        case "moderate":
            return KI18n.i18nc("@info what the Simple Compressor's Moderate mode does",
                               "Brings loud and quiet passages closer together, for films and games at low volume.");
        case "heavy":
            return KI18n.i18nc("@info what the Simple Compressor's Heavy mode does",
                               "Holds the level firmly, so quiet details come up close to the loud ones.");
        case "voice":
            return KI18n.i18nc("@info what the Simple Compressor's Voice mode does",
                               "Keeps a voice steady and close, catching each loud syllable as it starts.");
        case "broadcast":
            return KI18n.i18nc("@info what the Simple Compressor's Broadcast mode does",
                               "Dense and even, like a radio station: nothing stands out and nothing drops away.");
        }
        return "";
    }

    /// A ratio the way it is written: 2:1, 10:1.
    function ratioText(ratio: real): string {
        return KI18n.i18nc("@info a compression ratio, '2:1'", "%1:1",
                           Number(ratio).toLocaleString(Qt.locale(), 'f', Number.isInteger(ratio) ? 0 : 1));
    }

    /// A level or a gain in decibels, to a whole one unless it has a half.
    function decibelText(db: real): string {
        return KI18n.i18nc("@info a level or a gain in decibels", "%1 dB",
                           Number(db).toLocaleString(Qt.locale(), 'f', Number.isInteger(db) ? 0 : 1));
    }

    /// A gain that is added, with its sign: +3 dB.
    function gainText(db: real): string {
        const number = Number(db).toLocaleString(Qt.locale(), 'f', Number.isInteger(db) ? 0 : 1);
        return KI18n.i18nc("@info a gain in decibels", "%1 dB", db > 0 ? "+" + number : number);
    }

    /// A time in milliseconds.
    function millisecondsText(ms: real): string {
        return KI18n.i18nc("@info a time in milliseconds", "%1 ms",
                           Number(ms).toLocaleString(Qt.locale(), 'f', Number.isInteger(ms) ? 0 : 1));
    }

    function detectorName(id: string): string {
        switch (id) {
        case "peak":
            return KI18n.i18nc("@item the compressor follows the signal's peaks", "Peaks");
        case "rms":
            return KI18n.i18nc("@item the compressor follows the signal's average level", "Average (RMS)");
        }
        return id;
    }

    /// The line under a mode's name on the tile: how hard and from where,
    /// "2:1 · -18 dB", or that nothing is happening.
    function summary(id: string): string {
        if (id === "off") {
            return KI18n.i18nc("@info the Simple Compressor is off and leaves the sound alone", "no compression");
        }
        const settings = CompressorModes.settings(id);
        if (settings.ratio === undefined) {
            return "";
        }
        return KI18n.i18nc("@info a compression ratio and the level it starts at, '2:1 · -18 dB'", "%1 · %2",
                           words.ratioText(settings.ratio), words.decibelText(settings.threshold));
    }
}
