// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Night Level block's help page (docs/dev/block-help.md). The facts are
// the block's (blocks/nightlevel/nightlevel.h and .cpp: the law, the
// readings, the ceiling, the lapse) and the panel's (NightLevelPanel,
// NightLevelGraph, NightLevelWords).
HelpPage {
    typeId: "effect.nightlevel"
    summary: KI18n.i18nc("@info help page of the Night Level block", "Finds the level of speech in what is playing. Sounds near it stay as they are, louder ones come down, quieter ones come up. Until it hears speech it changes nothing. It can't know what comes next, so a sudden sound can start loud.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Night Level block", "A new block is set to nothing. Press Light, Night or Strong on its card to start. Put it after the Application block of the program that plays the film. Use one leveller on a lane: this block or Auto Level, not both."),
            KI18n.i18nc("@info help page of the Night Level block", "The block listens for a voice. After about three seconds of speech it has a level, and the tile says it. The level then follows the speech slowly, counting only the time a voice is heard, so one shouted line moves it little. With no voice the level stays where it is, and after half a minute the tile says it is held. After 15 minutes of sound with no voice the level is let go, and the block goes back to changing nothing."),
            KI18n.i18nc("@info help page of the Night Level block", "The level comes from a voice detector, and it can be wrong. While people talk it reads the voices and everything under them, so it may be the level of the mix more than of the voices. Singing counts as a voice. If the level looks wrong, type one."),
            KI18n.i18nc("@info help page of the Night Level block", "Sound from 5 LU under the speech level to 3 LU over it is left alone. Louder sound comes down within a tenth of a second or so. Quieter sound comes up after about four seconds, by at most 10 dB, and never past the speech level. Sound more than 30 LU under speech is not lifted. A line shorter than about three seconds is not lifted either."),
            KI18n.i18nc("@info help page of the Night Level block", "When a loud sound stops, the gain comes back over a second or two and no faster than 12 dB a second, so a line right after a bang stays a little quiet."),
            KI18n.i18nc("@info help page of the Night Level block", "A limit stops peaks at a level tied to the speech level, or at -1 dBFS with no level found. It is a digital limit, not a level at your ears. It holds the sound back by less than two thousandths of a second, far too little to hear, and that delay stays even with the block set to nothing."),
            KI18n.i18nc("@info help page of the Night Level block", "It presses music too, so turn it off for music. Music with no voice finds no level on a new session. After a film, music is pressed around the film's level for up to 15 minutes. If a program declares that it plays music, the tile and the card say so. A program that declares nothing tells the block nothing."),
            KI18n.i18nc("@info help page of the Night Level block", "It uses the voice model where the build has one and the session runs at 48 kHz, and Level and rhythm otherwise, which finds levels less often. The card says which."),
            KI18n.i18nc("@info help page of the Night Level block", "The card shows the lowest and highest change of the last minute as a line of numbers. Nothing else is kept, and the level found is not saved.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Night Level block", "For a film or a show, when the loud parts are too loud for the voices. Put it after the Application block of the program that plays it, before One Ear Mix. For voices on a call use Voice Evener, and for music use neither.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label Night Level's choice of amounts", "How much")
        covers: [
            KI18n.i18nc("@item:inlistbox how much Night Level does", "Light"),
            KI18n.i18nc("@item:inlistbox how much Night Level does", "Night"),
            KI18n.i18nc("@item:inlistbox how much Night Level does", "Strong")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Night Level block", "Light sets Loud sounds to 50 % and Quiet sounds to 0 %. Night sets 100 % and 50 %. Strong sets 100 % and 100 %. Nothing is picked once you set other amounts.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label where Night Level's speech level comes from", "Speech level")
        covers: [
            KI18n.i18nc("@item:inlistbox where Night Level's speech level comes from", "Found"),
            KI18n.i18nc("@item:inlistbox where Night Level's speech level comes from", "Typed"),
            KI18n.i18nc("@label:slider Night Level's speech level, typed", "Typed level")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Night Level block", "Found uses the level the block heard. Typed uses a level you give it from the first second and never lets it go. It starts from the level found, or from -28 LUFS with none. The block goes on listening, so going back to Found is immediate.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider how far Night Level turns loud sounds down", "Loud sounds")
        paragraphs: [
            KI18n.i18nc("@info help page of the Night Level block", "How far sound over the speech level comes down. At 100 % a sound 20 LU over speech ends 9.8 LU over it.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider how far Night Level lifts quiet sounds", "Quiet sounds")
        paragraphs: [
            KI18n.i18nc("@info help page of the Night Level block", "How far sound under the speech level comes up. At 100 % a sound 20 LU under speech ends 14 LU under it.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@title:group Night Level's graph", "Level in and out")
        covers: [KI18n.i18nc("@title:group Night Level's graph as numbers", "Graph as a table")]
        paragraphs: [
            KI18n.i18nc("@info help page of the Night Level block", "Both axes count from the speech level, so 0 is the speech and +10 is 10 LU louder than voices. The shaded strip is left alone. The dot is the last 400 ms and the change made now, so it leaves the curve while the change is on its way. The same figures are in the table.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Night Level block", "Start with Light. It lifts nothing, so the quiet parts stay as they are."),
            KI18n.i18nc("@info help page of the Night Level block", "If speech seems to be turned down, the level found may be too low. Type a level a little higher."),
            KI18n.i18nc("@info help page of the Night Level block", "Set the volume for the voices as you always would. The first moment of a bang still comes through, so don't rely on the block to hold it.")
        ]
    }
}
