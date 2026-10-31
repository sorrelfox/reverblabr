// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The One Ear Mix block's help page (docs/dev/block-help.md). The facts are
// the block's (engine/blocks/oneearmix.h and .cpp, engine/dsp/quadraturefold.h)
// and the panel's words. docs/hearing.md, "One Ear Mix", is the user
// documentation it agrees with.
HelpPage {
    typeId: "hearing.oneearmix"
    summary: KI18n.i18nc("@info help page of the One Ear Mix block", "Puts both sides of a stereo mix into the ear you choose, or one mix on both sides for one earbud.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the One Ear Mix block", "Without it, what is panned to the other side is lost to the ear you use. A plain mono mix, such as Mono in the Channels block, drops a sound panned to one side by 6 dB, and cancels sound that is out of phase between the sides."),
            KI18n.i18nc("@info help page of the One Ear Mix block", "One Ear Mix first shifts the two sides a quarter of a cycle apart, then adds them. A sound panned to one side comes through 3 dB lower, and sound out of phase between the sides doesn't cancel the way it does in a plain mono mix."),
            KI18n.i18nc("@info help page of the One Ear Mix block", "When you pick an ear, the sound on that side goes on playing and what was on the other side comes in over about five seconds. It does the same when the session starts and when the block is switched back on.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@title a part of the One Ear Mix block's help page", "What it cannot do")
        covers: [
            KI18n.i18nc("@action:button show the figures behind the line beside it", "Figures")
        ]
        points: [
            KI18n.i18nc("@info help page of the One Ear Mix block", "Direction doesn't come back: a mix in one ear can't tell you where a sound is."),
            KI18n.i18nc("@info help page of the One Ear Mix block", "A sound a quarter of a cycle apart between the sides can get quieter. If a track goes thin at one place, try Plain Mono."),
            KI18n.i18nc("@info help page of the One Ear Mix block", "Peaks can rise by several dB, so keep a limit after this block, as Volume has."),
            KI18n.i18nc("@info help page of the One Ear Mix block", "If the block is switched off or the session stops, stereo comes back, and what is on the other side is lost to that ear again.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@title a part of the One Ear Mix block's help page", "What each mix does to sound")
        points: [
            KI18n.i18nc("@info help page of the One Ear Mix block, one row of a table", "Centred sound: Plain Mono 0 dB, One Ear Mix about 0 dB."),
            KI18n.i18nc("@info help page of the One Ear Mix block, one row of a table", "Sound on one side only: Plain Mono 6 dB lower, One Ear Mix 3 dB lower."),
            KI18n.i18nc("@info help page of the One Ear Mix block, one row of a table", "Sound out of phase between the sides: Plain Mono cancels it, One Ear Mix about 0 dB."),
            KI18n.i18nc("@info help page of the One Ear Mix block, one row of a table", "Unrelated left and right: Plain Mono 3 dB lower, One Ear Mix 0 dB."),
            KI18n.i18nc("@info help page of the One Ear Mix block, one row of a table", "Peaks: no rise in Plain Mono, several dB in One Ear Mix."),
            KI18n.i18nc("@info help page of the One Ear Mix block, one row of a table", "At 1 kHz and a 48 kHz rate, the right side leading the left by a quarter of a cycle: Plain Mono 3 dB lower, One Ear Mix 30 dB lower."),
            KI18n.i18nc("@info help page of the One Ear Mix block, one row of a table", "The same with the right side lagging: Plain Mono 3 dB lower, One Ear Mix 3 dB higher.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label which ear a One Ear Mix sends the sound to", "Send to")
        covers: [
            KI18n.i18nc("@option:radio One Ear Mix leaves both sides alone", "No change"),
            KI18n.i18nc("@option:radio One Ear Mix sends the sound to", "Left ear"),
            KI18n.i18nc("@option:radio One Ear Mix sends the sound to", "Right ear"),
            KI18n.i18nc("@option:radio One Ear Mix sends the sound to", "Both ears")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the One Ear Mix block", "No change passes both sides as they came, and a new block starts there. Left ear and Right ear put both sides into that ear and leave the other side silent. Both ears puts one mix on both sides, for one earbud.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label which of two mixes to listen to", "Hear")
        covers: [
            KI18n.i18nc("@title:group a part of the One Ear Mix card", "Compare with Plain Mono"),
            KI18n.i18nc("@option:radio which mix a One Ear Mix block plays", "One Ear Mix"),
            KI18n.i18nc("@option:radio which mix a One Ear Mix block plays", "Plain Mono")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the One Ear Mix block", "Switches between One Ear Mix and Plain Mono, which adds the sides and halves them. It is under Compare with Plain Mono. Plain Mono is also the better choice for a mono recording in two channels.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@action:button read both mixes and lower the louder", "Match Levels")
        covers: [
            KI18n.i18nc("@action:button a One Ear Mix is reading the two levels", "Reading, 3 s")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the One Ear Mix block", "Play something and press it. It reads both mixes for three seconds. One Ear Mix usually reads louder, and by how much depends on the music, so if it was the louder, the difference goes into Lower this mix by. If Plain Mono was the louder, or the two are more than 6 dB apart, the card says so and changes nothing.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider how far a One Ear Mix turns its mix down", "Lower this mix by")
        paragraphs: [
            KI18n.i18nc("@info help page of the One Ear Mix block", "Turns One Ear Mix down, from 0 to 6 dB, so that a comparison is about the sound and not the level. A new block starts at 0 dB. The tile shows it while it is above 0, so that it isn't forgotten. It never touches Plain Mono.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the One Ear Mix block", "You want all of a stereo mix in one ear."),
            KI18n.i18nc("@info help page of the One Ear Mix block", "You have one earbud in, and want all of the mix in it.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the One Ear Mix block", "Put it after anything that needs the two sides, just before a lane forks."),
            KI18n.i18nc("@info help page of the One Ear Mix block", "A Hearing Compensation block set to the same ear, after it, works on what that ear receives."),
            KI18n.i18nc("@info help page of the One Ear Mix block", "Set Lower this mix by back to 0 when you are done comparing.")
        ]
    }
}
