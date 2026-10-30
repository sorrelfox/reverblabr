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
// the rise and fall times, the limiter it keeps in its path,
// engine/dsp/limiter.h), the face's (AutoLevelFace: the boost on the tile)
// and the panel's (AutoLevelPanel, AutoLevelWords).
HelpPage {
    typeId: "effect.autolevel"
    summary: KI18n.i18nc("@info help page of the Auto Level block", "Lifts quiet passages towards a steady loudness and leaves loud ones as they are. Use it for a source whose level keeps changing, such as a voice that comes and goes.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Auto Level block", "The block listens to how loud the sound has been over the last three seconds, leaving out the pauses and breaths, and checks again ten times a second. When the sound is quieter than the target, it turns it up by the difference. When the sound is at the target or louder, it adds nothing."),
            KI18n.i18nc("@info help page of the Auto Level block", "It only ever turns sound up, never down. Taming sound that is too loud is the job of a compressor, such as the Dynamics Processor."),
            KI18n.i18nc("@info help page of the Auto Level block", "The lift rises slowly, over a few seconds, so a quiet word is brought up gently rather than jumping. It falls quickly, within a few tenths of a second, when the sound gets loud again, so a shout after a whisper is not lifted for long."),
            KI18n.i18nc("@info help page of the Auto Level block", "Very quiet sound, below about −50 LUFS, is taken to be background noise and is not lifted. The lift then drifts back slowly, so a short pause between sentences does not undo it."),
            KI18n.i18nc("@info help page of the Auto Level block", "The tile on the canvas shows how much the block is lifting the sound right now, in decibels (dB). While nothing plays, it shows On or Off instead."),
            // i18n: "full scale" is the highest level digital sound can have.
            KI18n.i18nc("@info help page of the Auto Level block", "At the end of the block a safety limiter is always at work, as in the Volume block. It keeps the sound from going higher than 1 dB below full scale, the loudest a digital signal can be, so the lift can never make it crackle. It holds the sound back by one and a half thousandths of a second, far too little to hear.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check Auto Level on or off", "Lift quiet passages")
        paragraphs: [
            KI18n.i18nc("@info help page of the Auto Level block", "Turns the lifting on or off. It is on for a new block."),
            KI18n.i18nc("@info help page of the Auto Level block", "When you turn it off, the lift glides away over a few seconds rather than stopping at once. The safety limiter stays in place."),
            KI18n.i18nc("@info help page of the Auto Level block", "For most sources this switch is all you need. The two settings under Advanced are for the unusual cases.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@title:group the less usual Auto Level settings", "Advanced")
        paragraphs: [
            KI18n.i18nc("@info help page of the Auto Level block", "Click Advanced to show or hide these two settings. They can only be changed while Lift quiet passages is on.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider", "Target loudness")
        paragraphs: [
            // i18n: LUFS stays in Latin letters in most languages; the sentence explains it.
            KI18n.i18nc("@info help page of the Auto Level block", "How loud the block tries to make quiet passages. It is measured in LUFS, short for loudness units relative to full scale: a measure of how loud sound seems to the ear. The numbers are below zero, and a number nearer zero is louder."),
            KI18n.i18nc("@info help page of the Auto Level block", "The slider goes from −30 to −14 LUFS in steps of 1. A new block aims for −20 LUFS.")
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
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Auto Level block", "A speaker moves towards the microphone and away from it, and keeps getting louder and quieter."),
            KI18n.i18nc("@info help page of the Auto Level block", "A video or a podcast has quiet passages that are hard to hear over the noise around you."),
            KI18n.i18nc("@info help page of the Auto Level block", "Several people in a call are at very different levels, and you want the quiet ones to be easier to hear.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Auto Level block", "Start with the switch on and the Advanced settings as they are. Change them only if the result is not what you want."),
            KI18n.i18nc("@info help page of the Auto Level block", "If background noise swells up in the quiet parts, lower Maximum boost, or put a Denoise or Speech Denoise block before this one."),
            KI18n.i18nc("@info help page of the Auto Level block", "Put a Peak/RMS meter after the block to see the level it gives you, and watch the number on its tile to see how much it is lifting."),
            KI18n.i18nc("@info help page of the Auto Level block", "Auto Level lifts quiet sound but leaves loud sound alone. If the loud parts are too loud as well, add a compressor after it.")
        ]
    }
}
