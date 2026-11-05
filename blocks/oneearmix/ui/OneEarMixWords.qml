// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// What a One Ear Mix says, in one place for its face and its panel: the
// choices' words, the tile's caption and the status line, by the block's
// stable ids.
//
// **The words say which side has sound**, because the person reading them
// may be the one with little sound left to judge by.
QtObject {
    id: words

    /// What the list and the tile call a choice of Send to.
    function sendName(id: string): string {
        switch (id) {
        case "none":
            return KI18n.i18nc("@option:radio One Ear Mix leaves both sides alone", "No change");
        case "left":
            return KI18n.i18nc("@option:radio One Ear Mix sends the sound to", "Left ear");
        case "right":
            return KI18n.i18nc("@option:radio One Ear Mix sends the sound to", "Right ear");
        case "both":
            return KI18n.i18nc("@option:radio One Ear Mix sends the sound to", "Both ears");
        }
        return "";
    }

    /// The name of the plain fold, as a column heading.
    readonly property string plainMonoName: words.hearName("mono")

    /// What a choice of Hear is called.
    function hearName(id: string): string {
        switch (id) {
        case "fold":
            return KI18n.i18nc("@option:radio which mix a One Ear Mix block plays", "One Ear Mix");
        case "mono":
            return KI18n.i18nc("@option:radio which mix a One Ear Mix block plays", "Plain Mono");
        }
        return "";
    }

    /// The line under Send to.
    function explanation(id: string): string {
        switch (id) {
        case "none":
            return KI18n.i18nc("@info what a One Ear Mix choice does",
                               "Both sides pass as they came. Pick where the sound goes.");
        case "left":
            return KI18n.i18nc("@info what a One Ear Mix choice does",
                               "Puts both sides into the left ear. Sound panned to one side comes through 3 dB lower. The right side is silent.");
        case "right":
            return KI18n.i18nc("@info what a One Ear Mix choice does",
                               "Puts both sides into the right ear. Sound panned to one side comes through 3 dB lower. The left side is silent.");
        case "both":
            return KI18n.i18nc("@info what a One Ear Mix choice does",
                               "Puts both sides into one mix, the same on the left and on the right, for one earbud. Sound panned to one side comes through 3 dB lower.");
        }
        return "";
    }

    /// The status line, and what a change of Send to announces.
    function status(id: string): string {
        switch (id) {
        case "none":
            return KI18n.i18nc("@info where a One Ear Mix sends the sound", "No change. Both sides pass as they came.");
        case "left":
            return KI18n.i18nc("@info where a One Ear Mix sends the sound", "Left ear. The right side is silent.");
        case "right":
            return KI18n.i18nc("@info where a One Ear Mix sends the sound", "Right ear. The left side is silent.");
        case "both":
            return KI18n.i18nc("@info where a One Ear Mix sends the sound", "Both ears. One mix on both sides.");
        }
        return "";
    }

    /// \a db with one place, "1.4 dB".
    function decibels(db: real): string {
        return KI18n.i18nc("@info a level in decibels, '1.4 dB'", "%1 dB", Number(db).toLocaleString(Qt.locale(), 'f', 1));
    }

    /// The caption for a side coming in over the seconds it takes.
    function arriving(id: string): string {
        switch (id) {
        case "left":
            return KI18n.i18nc("@info a One Ear Mix is fading the right side in", "Bringing in R");
        case "right":
            return KI18n.i18nc("@info a One Ear Mix is fading the left side in", "Bringing in L");
        case "both":
            return KI18n.i18nc("@info a One Ear Mix is fading both sides in", "Bringing in both sides");
        }
        return "";
    }

    /// The caption while Plain Mono plays.
    function plainMono(id: string): string {
        switch (id) {
        case "left":
            return KI18n.i18nc("@info a One Ear Mix plays Plain Mono to the left", "Plain Mono to L");
        case "right":
            return KI18n.i18nc("@info a One Ear Mix plays Plain Mono to the right", "Plain Mono to R");
        case "both":
            return KI18n.i18nc("@info a One Ear Mix plays Plain Mono to both sides", "Plain Mono to both");
        }
        return "";
    }

    /// The caption while the mix is lowered by \a db.
    function lowered(id: string, db: real): string {
        const amount = Number(db).toLocaleString(Qt.locale(), 'f', 1);
        switch (id) {
        case "left":
            return KI18n.i18nc("@info a One Ear Mix lowered by a number of decibels, to the left", "One mix −%1 dB to L", amount);
        case "right":
            return KI18n.i18nc("@info a One Ear Mix lowered by a number of decibels, to the right", "One mix −%1 dB to R", amount);
        case "both":
            return KI18n.i18nc("@info a One Ear Mix lowered by a number of decibels, to both sides", "One mix −%1 dB to both", amount);
        }
        return "";
    }

    /// The caption when nothing else is going on.
    function route(id: string): string {
        switch (id) {
        case "none":
            return KI18n.i18nc("@info a One Ear Mix that has not been told where to send the sound", "Pick where it goes");
        case "left":
            return KI18n.i18nc("@info both sides as one mix to the left", "L+R one mix to L");
        case "right":
            return KI18n.i18nc("@info both sides as one mix to the right", "L+R one mix to R");
        case "both":
            return KI18n.i18nc("@info both sides as one mix to both sides", "L+R one mix to both");
        }
        return "";
    }

    /// The table on the card, one row a kind of sound, in whole decibels.
    function tableRows(): var {
        return [
            {
                sound: KI18n.i18nc("@item:intable a kind of sound in a mix", "Centred"),
                plain: KI18n.i18nc("@item:intable level of a kind of sound in Plain Mono", "0 dB"),
                fold: KI18n.i18nc("@item:intable level of a kind of sound in One Ear Mix", "About 0 dB")
            },
            {
                sound: KI18n.i18nc("@item:intable a kind of sound in a mix", "One side only"),
                plain: KI18n.i18nc("@item:intable level of a kind of sound in Plain Mono", "−6 dB"),
                fold: KI18n.i18nc("@item:intable level of a kind of sound in One Ear Mix", "−3 dB")
            },
            {
                sound: KI18n.i18nc("@item:intable a kind of sound in a mix", "Out of phase"),
                plain: KI18n.i18nc("@item:intable level of a kind of sound in Plain Mono", "Cancels"),
                fold: KI18n.i18nc("@item:intable level of a kind of sound in One Ear Mix", "About 0 dB")
            },
            {
                sound: KI18n.i18nc("@item:intable a kind of sound in a mix", "Unrelated sides"),
                plain: KI18n.i18nc("@item:intable level of a kind of sound in Plain Mono", "−3 dB"),
                fold: KI18n.i18nc("@item:intable level of a kind of sound in One Ear Mix", "0 dB")
            },
            {
                sound: KI18n.i18nc("@item:intable peaks of the sound", "Peaks"),
                plain: KI18n.i18nc("@item:intable what happens to peaks in Plain Mono", "No rise"),
                fold: KI18n.i18nc("@item:intable what happens to peaks in One Ear Mix", "Can rise")
            }
        ];
    }

    /// The same, cell by cell, for a three-column grid. `name` is what a
    /// screen reader says for a cell.
    function tableCells(): var {
        const cells = [];
        const plainMono = words.hearName("mono");
        const oneEarMix = words.hearName("fold");
        for (const row of words.tableRows()) {
            cells.push({ text: row.sound, name: row.sound, strong: true });
            cells.push({ text: row.plain, name: words.cellName(row.sound, plainMono, row.plain), strong: false });
            cells.push({ text: row.fold, name: words.cellName(row.sound, oneEarMix, row.fold), strong: false });
        }
        return cells;
    }

    /// What a screen reader says for one cell of that table.
    function cellName(sound: string, mix: string, level: string): string {
        return KI18n.i18nc("@info:accessibility a cell of a table, 'One side only, Plain Mono: -6 dB'", "%1, %2: %3", sound, mix, level);
    }

    /// What a reading of \a foldLufs and \a monoLufs comes to: `lower` is what
    /// to set Lower this mix by, or -1 for nothing, and `message` says so.
    function reading(foldLufs: real, monoLufs: real, audible: real, most: real): var {
        if (foldLufs <= audible) {
            return {
                lower: -1,
                message: KI18n.i18nc("@info a One Ear Mix's reading met silence", "Nothing to measure yet. Play something first.")
            };
        }
        const gap = Math.round((foldLufs - monoLufs) * 10) / 10;
        if (gap > most) {
            return {
                lower: -1,
                message: KI18n.i18nc("@info a One Ear Mix's reading, nothing changed; %1 is the most it can lower by, '6 dB'",
                                     "The two mixes are more than %1 apart here, so nothing is lowered.",
                                     KI18n.i18nc("@info a whole number of decibels", "%1 dB", most))
            };
        }
        if (gap < 0) {
            return {
                lower: -1,
                message: KI18n.i18nc("@info a One Ear Mix's reading, nothing changed", "Plain Mono was louder here, so nothing is lowered.")
            };
        }
        if (gap === 0) {
            return {
                lower: -1,
                message: KI18n.i18nc("@info a One Ear Mix's reading, nothing changed", "The two mixes read the same here, so nothing is lowered.")
            };
        }
        const shown = words.decibels(gap);
        return {
            lower: gap,
            message: KI18n.i18nc("@info a One Ear Mix's reading; %1 is a level, '1.4 dB'",
                                 "One Ear Mix was %1 louder here, so it is lowered by %1. Set it back to 0 when you are done.", shown)
        };
    }
}
