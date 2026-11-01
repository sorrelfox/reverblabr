// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Voice Forward block's help page (docs/dev/block-help.md). The facts are
// the block's (engine/blocks/voiceforward.h and .cpp, engine/dsp/centresplit.h)
// and the panel's (VoiceForwardPanel, VoiceForwardWords, HoldForBefore).
HelpPage {
    typeId: "effect.voiceforward"
    summary: KI18n.i18nc("@info help page of the Voice Forward block", "Finds what is alike on both sides of a stereo sound and turns the rest down. At 0 dB, or switched off, the sound passes unchanged.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Voice Forward block", "A new block is set to 0 dB and changes nothing. Pull Background down on the tile while people talk, until the voices sit where you want them. Switched off, the sound is the input as it came, and it stays lined up with the picture."),
            KI18n.i18nc("@info help page of the Voice Forward block", "The block looks for what is alike on both sides, band by band. It does not know what a voice is. A voice in the middle stays, and so do a kick drum, a bass and a lead singer. A voice off to one side goes down with the rest. A sound panned part-way can move towards the middle, and its quieter side can get louder."),
            KI18n.i18nc("@info help page of the Voice Forward block", "While a good part of the voice band is in the centre the rest goes down by Background. Otherwise it goes down by Between voices, so the room and the music stay. A line's first syllable can come under the louder background, since the block does not look ahead."),
            KI18n.i18nc("@info help page of the Voice Forward block", "A dB on the slider does not buy a dB. Taken out now shows what it really does, often much less, and -24 dB is not far beyond -12 dB. It may sound watery."),
            KI18n.i18nc("@info help page of the Voice Forward block", "On a mono sound both sides are alike, so between 100 Hz and 12 kHz there is nothing to move, and the card says so. Under and over that the sound still goes down, so a low rumble or a bass falls. With one side silent, or one side in reversed polarity, everything counts as the rest."),
            KI18n.i18nc("@info help page of the Voice Forward block", "It holds the sound back by about 23 ms at 48 kHz, even at 0 dB. A wireless link or another delay adds to that, and the picture can fall behind."),
            KI18n.i18nc("@info help page of the Voice Forward block", "A limit at -1 dBFS stops peaks whenever the block changes anything. It is a digital limit, not a level at your ears. At 0 dB, after the lift has gone, the limit steps aside too, so nothing is changed at all.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Voice Forward block", "For a film or a show in stereo, when music and effects cover the voices. Put it after any other cleaning and levelling, and before One Ear Mix: after a fold there is no left and right left to compare. Not for a game, where sounds at the sides tell you where things are. No measurement here shows that it helps anyone follow speech, and many people prefer the original.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider how far Voice Forward turns the rest of the sound down", "Background")
        paragraphs: [
            KI18n.i18nc("@info help page of the Voice Forward block", "How far the rest of the sound goes down while there is sound in the centre, from 0 to -24 dB. The presets Music stays and Voices first set -6 dB and -15 dB.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@title:group Voice Forward", "Between voices")
        paragraphs: [
            KI18n.i18nc("@info help page of the Voice Forward block", "How far the rest goes down when the centre is quiet. The cut used stops at Background. Set it to the same value as Background for one cut all the time.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check Voice Forward", "Hold level")
        paragraphs: [
            KI18n.i18nc("@info help page of the Voice Forward block", "What is taken out makes the whole sound quieter. This raises it again, slowly and by up to 6 dB, from the difference between the sound before and after. It starts after 3 s, and it follows the quiet parts too, so the voices stand out a little less. Leave it on when you compare, since the louder version tends to win.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@label how much lower Voice Forward has made the sound", "Taken out now")
        paragraphs: [
            KI18n.i18nc("@info help page of the Voice Forward block", "How much lower this sound is now, before any lift. The white line is Background.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@label what Voice Forward's mask found", "Found now")
        covers: [
            KI18n.i18nc("@info what Voice Forward found", "Centre sound"),
            KI18n.i18nc("@info what Voice Forward found", "Wide sound"),
            KI18n.i18nc("@info what Voice Forward found", "Both sides alike"),
            KI18n.i18nc("@info what Voice Forward found", "Quiet")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Voice Forward block", "One word from what the block found. Centre sound is what takes Background, and Wide sound what takes Between voices. Both sides alike comes after five seconds of nothing else. Quiet keeps the last cut.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@action:button hold to hear the sound without the block", "Hold for before")
        paragraphs: [
            KI18n.i18nc("@info help page of the Voice Forward block", "Press and hold it, or hold B with the focus in the card, on a talk scene with music under it. Enter on the button keeps it on until the next Enter. It is the block's real off state, so the picture does not move, and a level gap is stated and not hidden: with Hold level off, a line says by how much.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Voice Forward block", "For a whole evening, fork the lane after the source and send one side through Voice Forward and one straight to an Input Switch. The two stay lined up, and the switch has a global shortcut that works over a full-screen film."),
            KI18n.i18nc("@info help page of the Voice Forward block", "A global shortcut can switch the block on and off. System Settings lists it as Voice Forward on or off."),
            KI18n.i18nc("@info help page of the Voice Forward block", "Headphones show the change more than laptop speakers. Try it on what you really watch on.")
        ]
    }
}
