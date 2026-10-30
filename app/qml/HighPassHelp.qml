// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The High Pass Filter's help page (docs/dev/block-help.md). The facts are
// the block's (engine/blocks/highpassfilter.h: the range and the start;
// engine/blocks/cutofffilter.h: the slopes, 3 dB down at the cutoff, the
// glides) and the panel's (CutoffPanel and FilterWords, as BlockPanels sets
// them up for this block; CutoffFace for the slider on the tile).
HelpPage {
    typeId: "effect.highpass"
    summary: KI18n.i18nc("@info help page of the High Pass Filter block", "Takes away the deep sound below a pitch you choose, and lets everything above it pass. Use it to clean rumble, hum from handling and the boom of a room out of a voice.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            // i18n: Hz stands for hertz.
            KI18n.i18nc("@info help page of the High Pass Filter block", "Pitch is measured in hertz (Hz): low numbers are deep sounds, high numbers bright ones. The lowest note of a piano is about 27 Hz, and a man's speaking voice has little below about 80 Hz."),
            KI18n.i18nc("@info help page of the High Pass Filter block", "Sound above the cutoff passes as it is. Below it the sound fades away, more and more the deeper it goes. Right at the cutoff it is already a little quieter, by 3 decibels (dB), to about 70 % of its strength."),
            KI18n.i18nc("@info help page of the High Pass Filter block", "A new block starts at 20 Hz, the bottom of what people can hear, so it changes nothing you would notice until you move the cutoff up."),
            KI18n.i18nc("@info help page of the High Pass Filter block", "The cutoff can be moved while sound plays: it sweeps to the new place in a few hundredths of a second rather than jumping. Changing the slope fades from one to the other, so neither makes a click.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider a cutoff filter's cutoff frequency", "Cutoff")
        covers: [
            KI18n.i18nc("@label:textbox a cutoff frequency to type", "Cutoff in hertz")
        ]
        paragraphs: [
            // i18n: kHz stands for kilohertz, a thousand hertz.
            KI18n.i18nc("@info help page of the High Pass Filter block", "The pitch below which sound is taken away, from 20 Hz to 2 kilohertz (kHz), which is 2000 Hz. Drag the slider here or on the block's tile; it gives the low pitches as much room as the high ones."),
            KI18n.i18nc("@info help page of the High Pass Filter block", "For an exact number, type it in the box beside the slider and press Enter. A k means thousand, so 1.5k is 1500 Hz. A number outside the range is brought to its nearest end.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how steeply a filter rolls off", "Slope")
        covers: [
            KI18n.i18nc("@info a filter's slope, read out", "12 dB per octave"),
            KI18n.i18nc("@info a filter's slope, read out", "24 dB per octave")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the High Pass Filter block", "How quickly the sound fades below the cutoff. An octave is a halving of the pitch: from 100 Hz down to 50 Hz, for example.")
        ]
        points: [
            KI18n.i18nc("@info help page of the High Pass Filter block, one choice of Slope", "12 dB/oct: gentle. Each octave below the cutoff is 12 dB quieter, about a quarter of the strength. It sounds natural, and is where a new block starts."),
            KI18n.i18nc("@info help page of the High Pass Filter block, one choice of Slope", "24 dB/oct: steep. Each octave below the cutoff is 24 dB quieter, so the rumble goes while the sound just above the cutoff stays.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the High Pass Filter block", "A microphone picks up the rumble of traffic, a fan or an air conditioner."),
            KI18n.i18nc("@info help page of the High Pass Filter block", "You hear thumps when somebody touches the microphone or its stand, or bumps the desk."),
            KI18n.i18nc("@info help page of the High Pass Filter block", "A voice sounds boomy or muddy, especially close to the microphone."),
            KI18n.i18nc("@info help page of the High Pass Filter block", "Small speakers distort on deep sound they cannot play anyway.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the High Pass Filter block", "For speech, start at about 80 Hz and raise the cutoff until the voice starts to sound thin, then back off a little."),
            KI18n.i18nc("@info help page of the High Pass Filter block", "Keep the cutoff low for music: its bass is usually wanted."),
            KI18n.i18nc("@info help page of the High Pass Filter block", "Put the filter early in the lane, before a compressor or Auto Level, so they do not react to rumble you are going to take away anyway."),
            KI18n.i18nc("@info help page of the High Pass Filter block", "To take away the high sound instead, such as hiss, use the Low Pass Filter.")
        ]
    }
}
