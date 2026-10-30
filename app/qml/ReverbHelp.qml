// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Reverb block's help page (docs/dev/block-help.md). The facts are the
// block's (engine/blocks/reverb.h and .cpp: the ranges and defaults, the
// damping's tenfold, the mix as shares of one, no latency, the glides),
// its presets (engine/presets/effect.reverb) and the panel's words
// (ReverbPanel, ReverbWords).
HelpPage {
    typeId: "effect.reverb"
    summary: KI18n.i18nc("@info help page of the Reverb block", "Makes the sound seem to be in a room, from a small booth to a large hall, by adding the echoes a room would give it.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Reverb block", "In a real room, a sound bounces off the walls, the floor and the ceiling. Thousands of these echoes reach you, so close together that you hear them as one ring that slowly dies away. That ring is called reverb, and it is what tells the ear how big a place is."),
            KI18n.i18nc("@info help page of the Reverb block", "This block makes such a ring from the sound that reaches it and mixes it in behind the original. The original passes straight through, so the lane is not delayed."),
            KI18n.i18nc("@info help page of the Reverb block", "Every setting can be moved while the sound plays: each change glides in. A change of Size takes a third of a second and bends the pitch of the ring a little on the way, rather than clicking."),
            KI18n.i18nc("@info help page of the Reverb block", "The Presets list under the settings has three rooms to start from: Small room, Hall and Plate, the bright, dense ring of a metal plate.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider how long a Reverb waits before its room is heard", "Pre-delay")
        paragraphs: [
            // i18n: ms stands for milliseconds.
            KI18n.i18nc("@info help page of the Reverb block", "How long the room waits before it joins in, from 0 to 250 milliseconds (ms), a quarter of a second. A new block uses 20 ms."),
            KI18n.i18nc("@info help page of the Reverb block", "A short wait keeps the original clear in front of the room, which helps speech stay easy to follow. In a large real room the first echo also takes longer to come back.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider how big a Reverb's room is, from small to a hall", "Size")
        paragraphs: [
            KI18n.i18nc("@info help page of the Reverb block", "How big the room is, from 0 %, a small booth, to 100 %, a hall. A new block starts at 50 %."),
            KI18n.i18nc("@info help page of the Reverb block", "Small rooms ring densely and close; large ones spread out. Size sets how the ring sounds, not how long it lasts: that is Decay.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider how long a Reverb's room rings", "Decay")
        paragraphs: [
            KI18n.i18nc("@info help page of the Reverb block", "How long the room rings after the sound stops, from a tenth of a second to 20 seconds. A new block uses 2 seconds."),
            KI18n.i18nc("@info help page of the Reverb block", "It is the time the ring takes to fall to a thousandth of its strength, which is close to silence. The slider gives short times as much room as long ones."),
            KI18n.i18nc("@info help page of the Reverb block", "A longer decay does not make the reverb louder, only longer. Mix sets how loud it is.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider how much sooner the top of a Reverb's room fades", "Damping")
        paragraphs: [
            KI18n.i18nc("@info help page of the Reverb block", "How much sooner the high sounds of the ring fade than the low ones, the way soft furnishings and air soak up the top. From 0 %, where the top rings as long as the bottom, to 100 %, where it dies away ten times as fast. A new block uses 40 %."),
            KI18n.i18nc("@info help page of the Reverb block", "More damping gives a warmer, darker room; less gives a brighter one, like a tiled bathroom.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider how wide a Reverb's room sounds, from the middle to both sides", "Width")
        paragraphs: [
            KI18n.i18nc("@info help page of the Reverb block", "How far the ring spreads across the two speakers, from 0 %, all of it in the middle, to 100 %, as wide as it goes. A new block uses 100 %."),
            KI18n.i18nc("@info help page of the Reverb block", "Width changes only the ring. The original keeps its own place.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider how loud a Reverb is against the original", "Mix")
        paragraphs: [
            KI18n.i18nc("@info help page of the Reverb block", "How much of what you hear is the room, from 0 %, the original alone, to 100 %, the room alone. A new block uses 25 %."),
            KI18n.i18nc("@info help page of the Reverb block", "As the room gets louder, the original gets quieter by the same share. At 100 % none of the original is left.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Reverb block", "A voice recorded close to the microphone sounds dry and boxed in."),
            KI18n.i18nc("@info help page of the Reverb block", "Several sources recorded in different places should sound as if they were in the same room."),
            KI18n.i18nc("@info help page of the Reverb block", "You want a voice or an instrument to sound as if it were in a church, a hall or a cave.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Reverb block", "Start from one of the presets, then change one setting at a time."),
            KI18n.i18nc("@info help page of the Reverb block", "For speech, keep Mix low, around 10 to 20 %, and Decay short: too much room makes words hard to follow."),
            KI18n.i18nc("@info help page of the Reverb block", "If the reverb sounds harsh or hissy, raise Damping. If it sounds muddy, shorten Decay."),
            KI18n.i18nc("@info help page of the Reverb block", "Put the Reverb after blocks that clean up or even out the sound, such as a filter or a compressor, so they work on the original and not on the ring.")
        ]
    }
}
