// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

// The Parametric EQ's bands as its face and its panel both read them:
// which slots are in use, low to high, and each band's settings.
//
// **A setting the session does not hold is the block's default**, read from
// the engine's description of the block rather than written down here: a
// new block's session names no parameters at all, and its three bells in
// use are the engine's say, not the interface's.
QtObject {
    id: bands

    /// The block's parameters, by id, as the session holds them.
    property var params: ({})
    /// The plot, for the parameter ids and the number of slots.
    required property EqualizerPlot plot

    readonly property string typeId: "advanced.parametriceq"

    /// The slots in use, low to high.
    readonly property var used: {
        const found = [];
        for (let band = 0; band < bands.plot.bandCount; ++band) {
            if (bands.value(band, "Used") === true) {
                found.push(band);
            }
        }
        return found.sort((a, b) => bands.value(a, "Frequency") - bands.value(b, "Frequency"));
    }
    /// The ones of those that are switched on.
    readonly property var active: bands.used.filter(band => bands.value(band, "On") !== false)
    /// The first slot nobody is using, or -1 when all ten are.
    readonly property int free: {
        for (let band = 0; band < bands.plot.bandCount; ++band) {
            if (bands.value(band, "Used") !== true) {
                return band;
            }
        }
        return -1;
    }

    /// One of a band's settings — "Used", "On", "Type", "Frequency", "Gain"
    /// or "Q" — as the session has it, or the block's default.
    function value(band: int, which: string): var {
        const id = bands.plot.paramId(band, which);
        const held = bands.params[id];
        if (held !== undefined) {
            return held;
        }
        return BlockCatalogue.ready ? BlockCatalogue.describeParam(bands.typeId, id).default : undefined;
    }

    /// A band's frequency, which is what most places ask for.
    function frequency(band: int): real {
        return bands.value(band, "Frequency");
    }

    /// Where a band added now goes: the middle, in octaves, of the widest
    /// stretch of the range with no band in it, so a new band never lands
    /// on top of one already there.
    function roomFor(): real {
        const edges = [bands.plot.lowestHz].concat(bands.used.map(band => bands.value(band, "Frequency")),
                                                    [bands.plot.highestHz]);
        let best = 0;
        let widest = -1;
        for (let index = 1; index < edges.length; ++index) {
            const gap = Math.log(edges[index] / edges[index - 1]);
            if (gap > widest) {
                widest = gap;
                best = index;
            }
        }
        return Math.sqrt(edges[best - 1] * edges[best]);
    }
}
