// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

// A Frequency Lowering or Frequency Raising block's moves, as
// its face and its panel read them: each move's settings from the session,
// or the block's default where the session names none, and where a move
// carries a frequency.
//
// **carriedTo() is the engine's dsp::carriedTo()**, the same arithmetic, so
// that the arrows and the map show what the sound does;
// tst_frequencylowering.qml and tst_frequencyraising.qml hold the two
// together.
QtObject {
    id: moves

    /// The block's parameters, by id, as the session holds them.
    property var params: ({})

    /// Which of the two blocks: Frequency Lowering unless said.
    property string typeId: "hearing.frequencylowering"
    /// Whether it carries sound up, its destinations above its sources.
    readonly property bool raises: moves.typeId === "hearing.frequencyraising"
    /// FrequencyMoveBlock::kMoveCount.
    readonly property int count: 4
    /// The range a move's ends may take, as the block's parameters have it:
    /// FrequencyLowering::kMinimumHz or FrequencyRaising::kMinimumHz, and
    /// kMaximumHz.
    readonly property real lowestHz: moves.raises ? 60 : 125
    readonly property real highestHz: 16000
    /// dsp::kMaxHarmonic.
    readonly property int maxHarmonic: 16

    /// The moves in use, in slot order.
    readonly property var used: {
        const found = [];
        for (let move = 0; move < moves.count; ++move) {
            if (moves.value(move, "Used") === true) {
                found.push(move);
            }
        }
        return found;
    }
    /// The first slot nobody is using, or -1 when all four are.
    readonly property int free: {
        for (let move = 0; move < moves.count; ++move) {
            if (moves.value(move, "Used") !== true) {
                return move;
            }
        }
        return -1;
    }

    /// The id of one of a move's settings: "move1SourceLow".
    function paramId(move: int, which: string): string {
        return "move" + (move + 1) + which;
    }

    /// One of a move's settings — "Used", "On", "Method", "SourceLow",
    /// "SourceHigh", "DestinationLow", "DestinationHigh", "Level", "Source"
    /// or "When" — as the session has it, or the block's default.
    function value(move: int, which: string): var {
        const id = moves.paramId(move, which);
        const held = moves.params[id];
        if (held !== undefined) {
            return held;
        }
        return BlockCatalogue.ready ? BlockCatalogue.describeParam(moves.typeId, id).default : undefined;
    }

    /// A move as the engine takes it: its ranges the right way up, and
    /// whether it acts — in use, on, and its destination on the block's side
    /// of its source: below it for a lowering, reaching above it for a
    /// raising.
    function move(slot: int): var {
        let sourceLow = moves.value(slot, "SourceLow");
        let sourceHigh = moves.value(slot, "SourceHigh");
        let destinationLow = moves.value(slot, "DestinationLow");
        let destinationHigh = moves.value(slot, "DestinationHigh");
        if (sourceLow > sourceHigh) {
            [sourceLow, sourceHigh] = [sourceHigh, sourceLow];
        }
        if (destinationLow > destinationHigh) {
            [destinationLow, destinationHigh] = [destinationHigh, destinationLow];
        }
        return {
            "used": moves.value(slot, "Used") === true,
            "active": moves.value(slot, "Used") === true && moves.value(slot, "On") !== false
                      && (moves.raises ? destinationHigh > sourceHigh : destinationLow < sourceLow),
            "method": moves.value(slot, "Method"),
            "sourceLow": sourceLow,
            "sourceHigh": sourceHigh,
            "destinationLow": destinationLow,
            "destinationHigh": destinationHigh,
            "level": moves.value(slot, "Level"),
            "source": moves.value(slot, "Source"),
            "when": moves.value(slot, "When")
        };
    }

    /// Where \a move (from move()) carries a sound at \a hz, or 0 when it
    /// carries nothing from there: dsp::carriedTo().
    function carriedTo(move: var, hz: real): real {
        if (!(hz >= move.sourceLow && hz <= move.sourceHigh) || move.sourceLow <= 0 || move.destinationLow <= 0) {
            return 0;
        }
        if (move.method === "harmonics") {
            for (let harmonic = 2; harmonic <= moves.maxHarmonic; ++harmonic) {
                const to = hz * harmonic;
                if (to > move.destinationHigh) {
                    break;
                }
                if (to >= move.destinationLow) {
                    return to;
                }
            }
            return 0;
        }
        if (move.method === "transpose") {
            const to = hz * move.destinationLow / move.sourceLow;
            return to <= move.destinationHigh ? to : 0;
        }
        if (move.sourceHigh <= move.sourceLow || move.destinationHigh <= move.destinationLow) {
            return move.destinationLow;
        }
        const along = Math.log(hz / move.sourceLow) / Math.log(move.sourceHigh / move.sourceLow);
        return move.destinationLow * Math.pow(move.destinationHigh / move.destinationLow, along);
    }

    /// The highest frequency of the source that \a move still carries: all
    /// of it when squeezed, and only what lands below the destination's top
    /// when copied. Not for harmonics, which branches() draws.
    function carriedUpTo(move: var): real {
        if (move.method === "transpose") {
            return Math.min(move.sourceHigh, move.destinationHigh * move.sourceLow / move.destinationLow);
        }
        return move.sourceHigh;
    }

    /// The lines \a move draws on the map: frequency in against frequency
    /// out, each {from, to, fromOut, toOut} in hertz. One for a squeeze or
    /// a copy; for harmonics, one for each harmonic that lands in the
    /// destination, from the lowest sound of the source it lands there for
    /// to the highest.
    function branches(move: var): var {
        if (move.method !== "harmonics") {
            const top = moves.carriedUpTo(move);
            return [{
                    "from": move.sourceLow,
                    "to": top,
                    "fromOut": moves.carriedTo(move, move.sourceLow),
                    "toOut": moves.carriedTo(move, top)
                }];
        }
        const found = [];
        for (let harmonic = 2; harmonic <= moves.maxHarmonic; ++harmonic) {
            const from = Math.max(move.sourceLow, move.destinationLow / harmonic);
            const to = Math.min(move.sourceHigh, move.destinationHigh / harmonic);
            if (from < to) {
                found.push({ "from": from, "to": to, "fromOut": from * harmonic, "toOut": to * harmonic });
            }
        }
        return found;
    }
}
