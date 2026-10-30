// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Low Pass Filter block's help page (docs/dev/block-help.md). The
// facts are the block's (engine/blocks/lowpassfilter.h: the range and the
// start; cutofffilter.h: the flat pass band, 3 dB down at the cutoff, the
// glides) and the panel's words (CutoffPanel, FilterWords).
HelpPage {
    typeId: "effect.lowpass"
    summary: KI18n.i18nc("@info help page of the Low Pass Filter block", "Lets the low and middle sounds through and rolls off everything above a frequency you choose. Use it to take hiss and other high noise out of a source.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            // i18n: Hz stands for hertz, kHz for kilohertz.
            KI18n.i18nc("@info help page of the Low Pass Filter block", "Every sound is made of frequencies, counted in hertz (Hz): how many times a second the air shakes. Low ones are the bass and the body of a voice; high ones are the hiss, the crispness of an “s” and the sparkle of cymbals. A thousand hertz is a kilohertz (kHz)."),
            KI18n.i18nc("@info help page of the Low Pass Filter block", "Below the cutoff, the sound passes as it was. At the cutoff it is already a little quieter, and above it the sound gets quieter and quieter the higher it goes, at the rate the slope sets."),
            KI18n.i18nc("@info help page of the Low Pass Filter block", "A new block starts with the cutoff at 20 kHz, about as high as anybody hears, so it changes nothing until you bring the cutoff down."),
            KI18n.i18nc("@info help page of the Low Pass Filter block", "The cutoff glides to a new setting over a few hundredths of a second, so you can drag it while the sound plays and hear it sweep without clicks. The sound is not delayed.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider a cutoff filter's cutoff frequency", "Cutoff")
        covers: [KI18n.i18nc("@label:textbox a cutoff frequency to type", "Cutoff in hertz")]
        paragraphs: [
            KI18n.i18nc("@info help page of the Low Pass Filter block", "Where the roll-off starts, from 200 Hz to 20 kHz. Drag the slider, which gives each octave the same room, or type an exact number in the box beside it and press Enter."),
            KI18n.i18nc("@info help page of the Low Pass Filter block", "In the box, 8000, 8k and 8 kHz all mean the same. A number outside the range is brought to its nearest end.")
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
            // i18n: dB/oct stands for decibels per octave.
            KI18n.i18nc("@info help page of the Low Pass Filter block", "How quickly the sound fades above the cutoff, in decibels (dB) per octave. An octave is a doubling of the frequency, and every 6 dB less is about half the strength.")
        ]
        points: [
            KI18n.i18nc("@info help page of the Low Pass Filter block, one choice of Slope", "12 dB/oct: gentle. One octave above the cutoff, the sound has about a quarter of its strength. It sounds natural, and a new block uses it."),
            KI18n.i18nc("@info help page of the Low Pass Filter block, one choice of Slope", "24 dB/oct: steep. One octave above the cutoff, the sound is down to a sixteenth. Use it when the noise sits close above what you want to keep.")
        ]
        closing: [
            KI18n.i18nc("@info help page of the Low Pass Filter block", "Changing the slope fades from one to the other, so it makes no click.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Low Pass Filter block", "An old recording or a cheap microphone hisses."),
            KI18n.i18nc("@info help page of the Low Pass Filter block", "A sound is too sharp or too bright, and you want it duller or further away."),
            KI18n.i18nc("@info help page of the Low Pass Filter block", "You want a sound to be muffled on purpose, as if heard through a wall.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Low Pass Filter block", "Start at the top and bring the cutoff down slowly while you listen. Stop as soon as the hiss is gone, before the sound you want gets dull."),
            KI18n.i18nc("@info help page of the Low Pass Filter block", "For speech, try a cutoff around 8 kHz first: it takes away much of the hiss and keeps the words clear."),
            KI18n.i18nc("@info help page of the Low Pass Filter block", "To take away a low rumble instead, use the High Pass Filter. The two together keep only the range in between.")
        ]
    }
}
