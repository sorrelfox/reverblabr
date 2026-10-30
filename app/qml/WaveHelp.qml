// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Wave block's help page (docs/dev/block-help.md). The facts are the
// block's (engine/blocks/wave.h: the ranges, the defaults, the glide and
// the band-limited shapes) and the panel's (WavePanel: the typed
// frequency and the warning colour; WaveWords).
HelpPage {
    typeId: "output.wave"
    summary: KI18n.i18nc("@info help page of the Wave block", "Makes a test sound: a steady tone or a hiss of noise, at the pitch and the level you set, on the left, the right or both sides.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Wave block", "The block makes its own sound and takes nothing in. It is listed with the outputs, but it starts a lane: wire it to an Output Device to hear it, or through other blocks first to see what they do."),
            KI18n.i18nc("@info help page of the Wave block", "A known, steady sound is what you need to check a speaker, a cable or a setting. You always know what should come out."),
            KI18n.i18nc("@info help page of the Wave block", "Every change glides in over a fiftieth of a second, so changing the pitch, the level or the side never makes a click.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:listbox", "Waveform")
        covers: [
            KI18n.i18nc("@item a waveform", "Sine"),
            KI18n.i18nc("@item a waveform", "Square"),
            KI18n.i18nc("@item a waveform", "Triangle"),
            KI18n.i18nc("@item a waveform", "Sawtooth"),
            KI18n.i18nc("@item a kind of noise", "White noise"),
            KI18n.i18nc("@item a kind of noise", "Pink noise")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Wave block", "What the sound is. The first four are tones, each with a pitch; the last two are noise, with none.")
        ]
        points: [
            KI18n.i18nc("@info help page of the Wave block, a choice of Waveform", "Sine: the purest tone, a single pitch and nothing else. What a new block makes, and the usual test tone."),
            KI18n.i18nc("@info help page of the Wave block, a choice of Waveform", "Square, Triangle and Sawtooth: the same pitch with other tones above it, so they sound brighter and buzzier. Triangle is the softest of the three, and Square and Sawtooth the harshest. They are made so that a high pitch never brings a false, lower tone with it."),
            KI18n.i18nc("@info help page of the Wave block, a choice of Waveform", "White noise: a hiss with every pitch in it at once, at the same strength. It sounds bright and sharp."),
            KI18n.i18nc("@info help page of the Wave block, a choice of Waveform", "Pink noise: noise with the high pitches turned down, which sounds more even to the ear, like rushing water. It is the usual noise for checking speakers.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider", "Frequency")
        covers: [KI18n.i18nc("@label:textbox a frequency to type", "Frequency in hertz")]
        paragraphs: [
            // i18n: Hz stands for hertz, kHz for kilohertz.
            KI18n.i18nc("@info help page of the Wave block", "The pitch of a tone, in hertz (Hz): how many times a second the wave repeats. The slider goes from 20 Hz, a deep rumble, to 20 kilohertz (kHz), 20,000 Hz, higher than most adults can hear. A new block starts at 1 kHz."),
            KI18n.i18nc("@info help page of the Wave block", "The slider spreads the range the way the ear hears it: each step up in pitch, from one note to the same note an octave higher, takes the same distance anywhere on it."),
            // i18n: keep 440, 1k and 1.5 kHz as they are; they are what is typed.
            KI18n.i18nc("@info help page of the Wave block", "For an exact pitch, type it in the box beside the slider and press Enter: 440 for 440 Hz, or 1k or 1.5 kHz for kilohertz."),
            KI18n.i18nc("@info help page of the Wave block", "Noise has no pitch, so the frequency is greyed out while the block makes noise. It is kept for when you go back to a tone.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider how loud a Wave block's signal is", "Level")
        paragraphs: [
            // i18n: dB stands for decibels.
            KI18n.i18nc("@info help page of the Wave block", "How loud the sound is, in decibels (dB) below full scale, the loudest a digital signal can be. 0 dB is full scale, and every 6 dB lower is about half the strength. The slider goes from −80 dB, barely there, to 0 dB."),
            KI18n.i18nc("@info help page of the Wave block", "The level is the height of the wave's peaks. A new block starts at −18 dB, well below full scale, so that a test is never loud unless you ask for loud."),
            KI18n.i18nc("@info help page of the Wave block", "Above −6 dB the number turns to the warning colour. A loud steady tone can hurt your ears or a small speaker, so go up a little at a time.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label which channels a Wave block's signal is on", "Channels")
        covers: [
            KI18n.i18nc("@item the signal goes to the left channel only", "Left"),
            KI18n.i18nc("@item the signal goes to both channels", "Both"),
            KI18n.i18nc("@item the signal goes to the right channel only", "Right")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Wave block", "Which side the sound is on. Left or Right puts it on that side only, and the other side is silent. Both, what a new block uses, puts the same sound on each side.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Wave block", "You want to check that a speaker works, or that left and right are the right way round."),
            KI18n.i18nc("@info help page of the Wave block", "You want to know whether a cable or an adapter crackles or hums."),
            KI18n.i18nc("@info help page of the Wave block", "You want to see what a block does to a sound you know, with a meter after it.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Wave block", "Turn your speakers or headphones down before you start, then bring the level up slowly."),
            KI18n.i18nc("@info help page of the Wave block", "To check left and right, choose Left: the sound should come only from the left speaker. Then choose Right."),
            KI18n.i18nc("@info help page of the Wave block", "Pink noise at a modest level is a good sound to compare two speakers with, or to set the volumes of several to match."),
            KI18n.i18nc("@info help page of the Wave block", "A sweep from low to high, with the slider, shows where a small speaker stops playing the bass.")
        ]
    }
}
