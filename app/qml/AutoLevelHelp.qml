// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Auto Level block's help page (docs/dev/block-help.md). The facts are
// the block's (engine/blocks/autolevel.h and .cpp: the ranges, the gate,
// the rise and fall times, the guard, the limiter it keeps in its path,
// engine/dsp/limiter.h), the face's (AutoLevelFace: the gain on the tile)
// and the panel's (AutoLevelPanel, AutoLevelGraph, AutoLevelWords).
HelpPage {
    typeId: "effect.autolevel"
    summary: KI18n.i18nc("@info help page of the Auto Level block", "Lifts quiet passages towards a steady loudness and, if you ask, brings loud ones down to it. Use it for a source whose level keeps changing, such as a voice that comes and goes. The Voice Evener row in the library adds this block set up for voices on a call.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Auto Level block", "The block listens to how loud the sound has been over the last three seconds, leaving out the pauses and breaths, and checks again ten times a second. When the sound is quieter than the target, it turns it up by the difference."),
            KI18n.i18nc("@info help page of the Auto Level block", "The block can also bring loud passages down. It is off until you turn it on, and the Voice Evener preset turns it on. Then a loud talker comes down towards the target as well as a quiet one coming up. It listens over the last three seconds, so a new talker takes a few seconds to settle. If a loud voice starts straight after a quiet one, the lift is pulled back within about a tenth of a second, so that it doesn't stay at the quiet talker's level. Keep this off for music and films: it would take the loud and soft out of them."),
            KI18n.i18nc("@info help page of the Auto Level block", "Sound within the Leave alone range of the target is not moved. The graph shows it as a shaded strip, and the dot is the sound now."),
            KI18n.i18nc("@info help page of the Auto Level block", "A very short sound, such as a door or a dropped headset, is over before this block can act. While a boost stands it applies to everything on the lane, so such a sound comes through louder, and only the limiter at the end holds it."),
            KI18n.i18nc("@info help page of the Auto Level block", "The lift rises slowly, over a few seconds, so a quiet word is brought up gently rather than jumping. It falls quickly, within a few tenths of a second, when the sound gets loud again, so a shout after a whisper is not lifted for long."),
            KI18n.i18nc("@info help page of the Auto Level block", "Very quiet sound, below about −50 LUFS, is taken to be background noise and is not lifted. The lift then drifts back slowly, so a short pause between sentences does not undo it."),
            KI18n.i18nc("@info help page of the Auto Level block", "The tile on the canvas shows what the block is doing to the sound right now, in decibels (dB), and says so in a word: lifting, turning down, at the maximum, pulled back, left alone or too quiet to read. While nothing plays, it shows On or Off and says Not playing. If the program in front of the block declares that it is playing music or a film, the tile says so, and the card says what to do about it. A program that declares nothing tells the block nothing."),
            KI18n.i18nc("@info help page of the Auto Level block", "The card shows the lowest and highest change of the last minute as a line of numbers. Nothing else is kept, and it is not saved."),
            // i18n: "full scale" is the highest level digital sound can have.
            KI18n.i18nc("@info help page of the Auto Level block", "At the end of the block a limiter is always at work, as in the Volume block. It keeps the sound from going higher than 1 dB below full scale, the loudest a digital signal can be, so the lift can never make it crackle. It holds the sound back by one and a half thousandths of a second, far too little to hear.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check Auto Level's lift on or off", "Lift quiet passages")
        paragraphs: [
            KI18n.i18nc("@info help page of the Auto Level block", "Turns the lifting on or off. It is on for a new block."),
            KI18n.i18nc("@info help page of the Auto Level block", "When you turn it off, the lift glides away over a few seconds rather than stopping at once. The limiter stays in place."),
            KI18n.i18nc("@info help page of the Auto Level block", "For most sources the two switches are all you need. The settings under Advanced are for the unusual cases.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check Auto Level's cut on or off", "Bring loud passages down")
        paragraphs: [
            KI18n.i18nc("@info help page of the Auto Level block", "Turns on bringing sound down towards the target as well. It is off for a new Auto Level and on for a Voice Evener."),
            KI18n.i18nc("@info help page of the Auto Level block", "When you turn it off, a cut glides away over a few seconds rather than stopping at once.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@title:group Auto Level's graph", "Level in and out")
        paragraphs: [
            KI18n.i18nc("@info help page of the Auto Level block", "What the settings do, drawn: how loud the sound comes in along the bottom, how loud it goes out up the side, both from −60 to 0 LUFS. The dashed line is sound left unchanged. The dot is the sound now, worked out from the reading and the change, and the limiter is not in it. While the change glides the dot sits off the curve, and a pull drops it below."),
            KI18n.i18nc("@info help page of the Auto Level block", "The numbers under the graph say the same as the dot, and Graph as a table lists the curve with a row for now.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@title:group Auto Level's graph as numbers", "Graph as a table")
        paragraphs: [
            KI18n.i18nc("@info help page of the Auto Level block", "Click the heading to show the curve as numbers: what comes in, what goes out and the change, with a last row for now while the block runs.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@title:group the less usual Auto Level settings", "Advanced")
        paragraphs: [
            KI18n.i18nc("@info help page of the Auto Level block", "Click Advanced to show or hide these settings. Their values stay on the heading while it is closed. Target loudness and Leave alone work while either switch is on, and each maximum while its own switch is on.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider", "Target loudness")
        paragraphs: [
            // i18n: LUFS stays in Latin letters in most languages; the sentence explains it.
            KI18n.i18nc("@info help page of the Auto Level block", "How loud the block tries to make the sound. It is measured in LUFS, short for loudness units relative to full scale: a measure of how loud sound seems to the ear. The numbers are below zero, and a number nearer zero is louder."),
            KI18n.i18nc("@info help page of the Auto Level block", "The slider goes from −30 to −14 LUFS in steps of 1, and you can type the number. A new block aims for −20 LUFS, and a Voice Evener for −23 LUFS.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider", "Maximum boost")
        paragraphs: [
            KI18n.i18nc("@info help page of the Auto Level block", "The most the block may lift the sound, however quiet it is. The slider goes from 0 to 24 dB in steps of 1, and a new block allows 18 dB."),
            KI18n.i18nc("@info help page of the Auto Level block", "Lower it for a noisy source. A lift of 18 dB makes quiet sound about eight times stronger, and it lifts the hiss and hum in the background just as much."),
            KI18n.i18nc("@info help page of the Auto Level block", "At 0 dB the block lifts nothing at all.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider", "Maximum cut")
        paragraphs: [
            KI18n.i18nc("@info help page of the Auto Level block", "The most the block may take off, however loud the sound is. The slider goes from 0 to 24 dB in steps of 1, and a new block allows 18 dB. At 0 dB nothing is turned down.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider", "Leave alone")
        paragraphs: [
            KI18n.i18nc("@info help page of the Auto Level block", "How far either side of the target the sound is left as it is, in loudness units (LU). The slider goes from 0 to 6 in steps of 1. A new Auto Level leaves nothing alone and a Voice Evener ±1 LU. At 0 every level is moved to the target.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Auto Level block", "A speaker moves towards the microphone and away from it, and keeps getting louder and quieter."),
            KI18n.i18nc("@info help page of the Auto Level block", "A video or a podcast has quiet passages that are hard to hear over the noise around you."),
            KI18n.i18nc("@info help page of the Auto Level block", "Several people in a call are at very different levels, and you want them nearer each other: add Voice Evener from the library.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Auto Level block", "Start with the switch on and the Advanced settings as they are. Change them only if the result is not what you want."),
            KI18n.i18nc("@info help page of the Auto Level block", "If background noise swells up in the quiet parts, lower Maximum boost, or put a Denoise or Speech Denoise block before this one."),
            KI18n.i18nc("@info help page of the Auto Level block", "Put a Peak/RMS meter after the block to see the level it gives you, and watch the number on its tile to see what it is doing."),
            KI18n.i18nc("@info help page of the Auto Level block", "This block evens a talker or a passage over seconds and leaves the rise and fall inside a sentence. To tame what is loud moment by moment, add a compressor after it.")
        ]
    }
}
