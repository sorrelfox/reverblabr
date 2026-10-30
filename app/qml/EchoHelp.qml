// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Echo block's help page (docs/dev/block-help.md). The facts are the
// block's (engine/blocks/echo.h and .cpp: the ranges and defaults, the
// filter in the loop, the mix and the fades) and the panel's (EchoPanel,
// EchoWords; EchoFace for the slider on the tile).
HelpPage {
    typeId: "effect.echo"
    summary: KI18n.i18nc("@info help page of the Echo block", "Repeats the sound a moment later, again and again, each repeat quieter and duller than the one before, mixed in behind the original.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Echo block", "The block keeps a copy of the sound and plays it back after the time you set. Part of each repeat is fed back in to be repeated again, so you hear a trail of echoes that fades away."),
            KI18n.i18nc("@info help page of the Echo block", "The first repeat sounds like the original. Each one after it loses some of its highest sounds, as an echo off a far wall does, and some of its level, so the trail always dies away in the end."),
            KI18n.i18nc("@info help page of the Echo block", "The original sound goes straight through with no delay, so the block does not hold the lane back."),
            KI18n.i18nc("@info help page of the Echo block", "Every setting can be changed while sound plays without a click. A new time fades across from the old echoes to the new ones over a twentieth of a second, rather than bending the pitch.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider the time between an Echo's repeats", "Time")
        paragraphs: [
            // i18n: ms stands for milliseconds, s for seconds.
            KI18n.i18nc("@info help page of the Echo block", "How long after the sound each repeat comes, from 1 millisecond (ms) to 2 seconds (s). A new block starts at 350 ms, about a third of a second."),
            KI18n.i18nc("@info help page of the Echo block", "Drag the slider here or on the block's tile. It gives short times as much room as long ones, so a short echo is easy to aim at."),
            KI18n.i18nc("@info help page of the Echo block", "Below about 50 ms the repeats are too close to be heard apart, and they colour the sound rather than echo it. From about 100 ms they become separate echoes.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider how much of each repeat an Echo feeds back in", "Feedback")
        paragraphs: [
            KI18n.i18nc("@info help page of the Echo block", "How much of each repeat is fed back in to be repeated again, from 0 to 95 %. It decides how many repeats you hear."),
            KI18n.i18nc("@info help page of the Echo block", "At 0 % there is a single repeat. Higher numbers give a longer trail; even at 95 % it fades away in the end. A new block starts at 40 %.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider how bright an Echo's repeats stay", "Tone")
        paragraphs: [
            // i18n: Hz stands for hertz, kHz for kilohertz.
            KI18n.i18nc("@info help page of the Echo block", "How bright the repeats stay, from 500 hertz (Hz) to 20 kilohertz (kHz). Each repeat loses the sound above this frequency a little more than the one before it."),
            KI18n.i18nc("@info help page of the Echo block", "A low setting gives dark, soft echoes that stay in the background. A high one keeps them as bright as the original. A new block starts at 5 kHz.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider how loud an Echo's repeats are against the original", "Mix")
        paragraphs: [
            KI18n.i18nc("@info help page of the Echo block", "How loud the echoes are against the original, from 0 to 100 %. At 0 % you hear the original alone, at 50 % the two equally, and at 100 % the echoes alone. A new block starts at 35 %."),
            KI18n.i18nc("@info help page of the Echo block", "As the echoes get louder, the original gets quieter by the same share, so the overall level stays about the same.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Echo block", "You want a voice or an instrument to sound as if it were in a large space, such as a canyon or a stadium."),
            KI18n.i18nc("@info help page of the Echo block", "A sound effect, a jingle or a single word should stand out with a trail of repeats."),
            KI18n.i18nc("@info help page of the Echo block", "Music sounds flat and dry, and a quiet echo behind it would give it depth.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Echo block", "Keep the Mix low, between 15 and 35 %, so the echoes sit behind the sound rather than over it."),
            KI18n.i18nc("@info help page of the Echo block", "Echo on speech makes it harder to follow. For a call or a podcast, use a short time with little feedback, or leave it out."),
            KI18n.i18nc("@info help page of the Echo block", "To make echoes fit the beat of a song, work out the time between two beats: 60000 divided by the beats per minute gives it in milliseconds."),
            KI18n.i18nc("@info help page of the Echo block", "If the trail gets muddy, lower the Tone or the Feedback.")
        ]
    }
}
