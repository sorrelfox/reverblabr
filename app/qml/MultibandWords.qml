// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Multiband Compressor's words, in one place for its face, its
// panel and their tests: the bands' names, their ranges, the splits, and
// how much a band is holding back.
QtObject {
    id: words

    /// The bands' stable id prefixes, low first, as the block names its
    /// parameters: "lowThreshold", "midRatio", "highMakeup".
    readonly property var bandIds: ["low", "mid", "high"]

    /// The most a meter shows held back, in decibels: past this a bar is
    /// full. A band taking off more than this is working very hard indeed.
    readonly property real deepestReductionDb: 24

    readonly property FilterWords filters: FilterWords {}
    readonly property CompressorWords compressor: CompressorWords {}

    /// A band's name, short enough for under a bar on a tile.
    function bandName(band: int): string {
        switch (band) {
        case 0:
            return KI18n.i18nc("@label the lowest of a multiband compressor's three bands", "Low");
        case 1:
            return KI18n.i18nc("@label the middle of a multiband compressor's three bands", "Mid");
        case 2:
            return KI18n.i18nc("@label the highest of a multiband compressor's three bands", "High");
        }
        return "";
    }

    /// Where a band is: "Below 200 Hz", "200 Hz to 3.00 kHz", "Above 3.00 kHz".
    function rangeText(band: int, lowHz: real, highHz: real): string {
        switch (band) {
        case 0:
            return KI18n.i18nc("@info the frequencies of a multiband compressor's low band", "Below %1",
                               words.filters.frequencyText(lowHz));
        case 1:
            return KI18n.i18nc("@info the frequencies of a multiband compressor's middle band", "%1 to %2",
                               words.filters.frequencyText(lowHz), words.filters.frequencyText(highHz));
        case 2:
            return KI18n.i18nc("@info the frequencies of a multiband compressor's high band", "Above %1",
                               words.filters.frequencyText(highHz));
        }
        return "";
    }

    /// The two splits, as a tile's caption: "200 Hz · 3.00 kHz".
    function splitsText(lowHz: real, highHz: real): string {
        return KI18n.i18nc("@info a multiband compressor's two split frequencies, low first", "%1 · %2",
                           words.filters.frequencyText(lowHz), words.filters.frequencyText(highHz));
    }

    /// What a band does, for a tooltip: "Low, below 200 Hz: from -24 dB at 3:1".
    function bandSummary(band: int, lowHz: real, highHz: real, thresholdDb: real, ratio: real): string {
        if (ratio <= 1.0) {
            return KI18n.i18nc("@info:tooltip a multiband compressor band that does nothing; band name, then its range",
                               "%1, %2: left alone", words.bandName(band), words.rangeText(band, lowHz, highHz));
        }
        return KI18n.i18nc("@info:tooltip a multiband compressor band; its name, its range, its threshold and ratio",
                           "%1, %2: from %3 at %4", words.bandName(band), words.rangeText(band, lowHz, highHz),
                           words.compressor.decibelText(thresholdDb), words.compressor.ratioText(ratio));
    }

    /// How much a band is holding back, to a tenth: "-4.5 dB", or "0 dB".
    function reductionText(db: real): string {
        const tenths = Math.round(Math.min(0, db) * 10) / 10;
        if (tenths === 0) {
            return KI18n.i18nc("@info a compressor taking nothing off", "0 dB");
        }
        return KI18n.i18nc("@info how much a compressor is taking off; the number has its minus", "%1 dB",
                           Number(tenths).toLocaleString(Qt.locale(), 'f', 1));
    }

    /// How far down a meter's bar reaches for \a db held back, 0 to 1.
    function reductionFraction(db: real): real {
        return Math.max(0, Math.min(1, -db / words.deepestReductionDb));
    }
}
