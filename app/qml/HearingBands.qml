// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

// A Hearing Compensation block's bands, read from its parameters: the
// face and the panel draw from the same numbers, so they cannot disagree.
//
// **Hearing Compensation 64 is set through control points**: up to
// 24, each with the four settings, and the 64 bands follow a curve through
// them for each setting. curveThrough() is the block's own curve
// (dsp::MonotoneCurve: Fritsch and Carlson's tangents, level beyond the
// ends), so what the panel draws is what the block does.
//
// **The numbers are the block's own** (engine/dsp/hearingbands.h): the band
// centres are evenly spaced on the ERB scale from 100 Hz to 16 kHz; a band's
// soft, average and loud gains apply at -55, -40 and -25 dBFS of its own
// level; ceilings are on the fitting rule's listening scale, 85 dB above
// dBFS. steadyGain() is the block's law, so the band inspector's curve is
// what the block does.
QtObject {
    id: bands

    /// The block's parameters (SessionModel.params()).
    property var params: ({})
    /// Which block this is: "hearing.compensation16" or
    /// "hearing.compensation64".
    property string typeId: "hearing.compensation16"
    /// Whether it is set through control points rather than band by band.
    readonly property bool throughPoints: bands.typeId === "hearing.compensation64"
    /// How many bands the block has.
    readonly property int count: bands.throughPoints ? 64 : 16
    /// How many control points it has, and where it starts with them.
    readonly property int pointCount: bands.throughPoints ? 24 : 0
    readonly property var defaultPointsHz: [250, 500, 1000, 2000, 4000, 8000]

    readonly property real lowestHz: 100
    readonly property real highestHz: 16000
    readonly property real averageInputDbfs: -40
    readonly property real levelStepDb: 15
    readonly property real listeningOffsetDb: 85
    readonly property real defaultCeilingDb: 100
    /// Which of the four settings a band has, in the order the ids spell them.
    readonly property var settings: ["Soft", "Average", "Loud", "Ceiling"]

    /// The bands' centres, in hertz, low to high.
    readonly property var centresHz: {
        const low = bands.erbNumber(bands.lowestHz);
        const high = bands.erbNumber(bands.highestHz);
        const centres = [];
        for (let band = 0; band < bands.count; ++band) {
            centres.push(bands.hzAtErbNumber(low + (high - low) * band / Math.max(1, bands.count - 1)));
        }
        return centres;
    }

    /// Each band's four settings as the block holds them, a list per band:
    /// {soft, average, loud, ceiling}. Through points, what the curves
    /// through them come to at each band's centre.
    readonly property var all: {
        const list = [];
        if (bands.throughPoints) {
            const on = bands.points.filter(point => point.on);
            if (on.length === 0) {
                for (let band = 0; band < bands.count; ++band) {
                    list.push({ soft: 0, average: 0, loud: 0, ceiling: bands.defaultCeilingDb });
                }
                return list;
            }
            // Low to high, one to a frequency: the first of any on the same.
            on.sort((a, b) => a.hz - b.hz || a.index - b.index);
            const kept = on.filter((point, at) => at === 0 || Math.log(point.hz) - Math.log(on[at - 1].hz) >= 1e-9);
            const x = kept.map(point => Math.log(point.hz));
            const curves = {};
            for (const which of ["soft", "average", "loud", "ceiling"]) {
                curves[which] = bands.curveThrough(x, kept.map(point => point[which]));
            }
            for (let band = 0; band < bands.count; ++band) {
                const at = Math.log(bands.centresHz[band]);
                list.push({
                    soft: curves.soft(at),
                    average: curves.average(at),
                    loud: curves.loud(at),
                    ceiling: curves.ceiling(at)
                });
            }
            return list;
        }
        for (let band = 0; band < bands.count; ++band) {
            list.push({
                soft: bands.held(bands.paramId(band, "Soft"), 0),
                average: bands.held(bands.paramId(band, "Average"), 0),
                loud: bands.held(bands.paramId(band, "Loud"), 0),
                ceiling: bands.held(bands.paramId(band, "Ceiling"), bands.defaultCeilingDb)
            });
        }
        return list;
    }

    /// Every control point as the block holds it, placed or not:
    /// {index, on, hz, soft, average, loud, ceiling}.
    readonly property var points: {
        const list = [];
        for (let point = 0; point < bands.pointCount; ++point) {
            const placed = point < bands.defaultPointsHz.length;
            list.push({
                index: point,
                on: bands.held(bands.pointParamId(point, "On"), placed) === true,
                hz: bands.held(bands.pointParamId(point, "Hz"), placed ? bands.defaultPointsHz[point] : 1000),
                soft: bands.held(bands.pointParamId(point, "Soft"), 0),
                average: bands.held(bands.pointParamId(point, "Average"), 0),
                loud: bands.held(bands.pointParamId(point, "Loud"), 0),
                ceiling: bands.held(bands.pointParamId(point, "Ceiling"), bands.defaultCeilingDb)
            });
        }
        return list;
    }

    /// The points that are on, as a list of their indices, low to high.
    readonly property var placed: bands.points.filter(point => point.on).sort((a, b) => a.hz - b.hz)
                                              .map(point => point.index)

    function erbNumber(hz: real): real {
        return 21.4 * Math.log10(4.37 * hz / 1000 + 1);
    }

    function hzAtErbNumber(erbs: real): real {
        return (Math.pow(10, erbs / 21.4) - 1) * 1000 / 4.37;
    }

    /// A band setting's id: `band<N>SoftGainDb` … `band<N>CeilingDb`.
    function paramId(band: int, which: string): string {
        return "band" + band + (which === "Ceiling" ? "CeilingDb" : which + "GainDb");
    }

    /// A control point setting's id: `point<N>On`, `point<N>Hz`,
    /// `point<N>SoftGainDb` … `point<N>CeilingDb`.
    function pointParamId(point: int, which: string): string {
        if (which === "On" || which === "Hz") {
            return "point" + point + which;
        }
        return "point" + point + (which === "Ceiling" ? "CeilingDb" : which + "GainDb");
    }

    /// A parameter as the block holds it, or \a fallback.
    function held(paramId: string, fallback: var): var {
        const value = bands.params[paramId];
        return value !== undefined ? value : fallback;
    }

    /// A band setting as the block holds it, or the block's default; through
    /// points, what the curve comes to at the band.
    function value(band: int, which: string): real {
        const settings = bands.all[band];
        return settings !== undefined ? settings[which.toLowerCase()] : (which === "Ceiling" ? bands.defaultCeilingDb : 0);
    }

    /// A control point's setting (\a which as the settings name them).
    function pointValue(point: int, which: string): real {
        const at = bands.points[point];
        return at !== undefined ? at[which.toLowerCase()] : 0;
    }

    /// **The block's curve through points** (dsp::MonotoneCurve): piecewise
    /// cubic Hermite with Fritsch and Carlson's tangents, level beyond the
    /// ends, never beyond two points between them. \a x rising. A function
    /// of x.
    function curveThrough(x: var, y: var): var {
        const n = Math.min(x.length, y.length);
        if (n === 0) {
            return () => 0;
        }
        if (n === 1) {
            return () => y[0];
        }
        const secant = [];
        for (let point = 0; point + 1 < n; ++point) {
            secant.push((y[point + 1] - y[point]) / (x[point + 1] - x[point]));
        }
        const slope = new Array(n).fill(0);
        slope[0] = secant[0];
        slope[n - 1] = secant[n - 2];
        for (let point = 1; point + 1 < n; ++point) {
            const before = secant[point - 1];
            const after = secant[point];
            if (before * after <= 0) {
                slope[point] = 0;
                continue;
            }
            const wide = x[point] - x[point - 1];
            const next = x[point + 1] - x[point];
            const w1 = 2 * next + wide;
            const w2 = next + 2 * wide;
            slope[point] = (w1 + w2) / (w1 / before + w2 / after);
        }
        return at => {
            if (at <= x[0]) {
                return y[0];
            }
            if (at >= x[n - 1]) {
                return y[n - 1];
            }
            let piece = 0;
            while (piece + 2 < n && at > x[piece + 1]) {
                ++piece;
            }
            const width = x[piece + 1] - x[piece];
            const t = (at - x[piece]) / width;
            const t2 = t * t;
            const t3 = t2 * t;
            return (2 * t3 - 3 * t2 + 1) * y[piece] + (t3 - 2 * t2 + t) * width * slope[piece]
                + (-2 * t3 + 3 * t2) * y[piece + 1] + (t3 - t2) * width * slope[piece + 1];
        };
    }

    /// A ceiling on the listening scale, as dBFS of a band's own level.
    function ceilingDbfs(ceiling: real): real {
        return ceiling - bands.listeningOffsetDb;
    }

    /// The gain for a steady sound at \a levelDbfs in a band set as \a band
    /// ({soft, average, loud, ceiling}): the block's own law, before
    /// Loudness, the noise floor and the most gain.
    function steadyGain(band: var, levelDbfs: real): real {
        const step = bands.levelStepDb;
        const softLevel = bands.averageInputDbfs - step;
        const loudLevel = bands.averageInputDbfs + step;
        // No step falls faster than the level rises, as in the block.
        const soft = band.soft;
        const average = Math.max(band.average, soft - step);
        const loud = Math.max(band.loud, average - step);
        let gain = soft;
        if (levelDbfs > loudLevel) {
            gain = loud + Math.min((loud - average) / step, 0) * (levelDbfs - loudLevel);
        } else if (levelDbfs > bands.averageInputDbfs) {
            gain = average + (levelDbfs - bands.averageInputDbfs) / step * (loud - average);
        } else if (levelDbfs > softLevel) {
            gain = soft + (levelDbfs - softLevel) / step * (average - soft);
        }
        return Math.min(gain, bands.ceilingDbfs(band.ceiling) - levelDbfs);
    }

    /// How much a band compresses between its average and loud levels, as a
    /// ratio: 15 dB in, this many out.
    function ratio(band: var): real {
        const out = bands.levelStepDb + (band.loud - band.average);
        return out > 0 ? bands.levelStepDb / out : Infinity;
    }
}
