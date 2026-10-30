// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The 10-Band EQ's help page (docs/dev/block-help.md). The facts are the
// block's (engine/blocks/equalizer10.h and .cpp: the bands, their width,
// the range, the preamp and Keep level, the glide), the panel's
// (EqualizerPanel, EqualizerBands, FilterWords; EqualizerFace for the
// tile) and the factory presets (engine/presets/effect.equalizer10, named
// in app/presets.cpp).
HelpPage {
    typeId: "effect.equalizer10"
    summary: KI18n.i18nc("@info help page of the 10-Band EQ block", "Shapes the tone of the sound with ten sliders, each lifting or cutting one range of pitches, from the deepest bass to the highest treble.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            // i18n: Hz stands for hertz, kHz for kilohertz.
            KI18n.i18nc("@info help page of the 10-Band EQ block", "Every sound is a mix of low and high pitches. The block splits that range into ten bands and lets you turn each one up or down. The bands are named by the pitch in their middle, in hertz (Hz) and kilohertz (kHz): 31, 63, 125, 250 and 500 Hz, then 1, 2, 4, 8 and 16 kHz. Each is twice the one before."),
            KI18n.i18nc("@info help page of the 10-Band EQ block", "Each band is about as wide as the gap to its neighbours, so a band touches the bands beside it a little. Moving several sliders next to each other makes a smooth curve rather than steps."),
            KI18n.i18nc("@info help page of the 10-Band EQ block", "As a rough guide: the two lowest bands are deep bass and rumble, 125 and 250 Hz are warmth and boom, 500 Hz to 2 kHz is the body of voices and most instruments, 4 kHz is clarity and presence, and 8 and 16 kHz are brightness, air and hiss."),
            KI18n.i18nc("@info help page of the 10-Band EQ block", "The block's tile shows the ten bands as ten bars, so you can see the shape at a glance. To change them, open the settings."),
            KI18n.i18nc("@info help page of the 10-Band EQ block", "A band at 0 dB changes nothing, and a new block starts with every band there. Every change glides in over a fiftieth of a second, so moving a slider while sound plays makes no click.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:group an equaliser's ten band sliders", "Bands")
        paragraphs: [
            // i18n: dB stands for decibels.
            KI18n.i18nc("@info help page of the 10-Band EQ block", "Ten upright sliders, one a band, with the band's pitch above it and its setting below it. Each goes from −12 to +12 decibels (dB) in steps of half a decibel: up lifts that band, down cuts it, and the line across the middle is 0 dB. The scale on the left is in decibels."),
            KI18n.i18nc("@info help page of the 10-Band EQ block", "Every 6 dB is about twice or half the strength of that band.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider the gain before an equaliser's bands", "Preamp")
        paragraphs: [
            KI18n.i18nc("@info help page of the 10-Band EQ block", "Turns the whole sound up or down before it reaches the bands, from −12 to +12 dB in steps of half a decibel. A new block starts at 0 dB."),
            KI18n.i18nc("@info help page of the 10-Band EQ block", "Lifting bands makes the sound louder, and can push it into clipping, where its loudest moments are cut off and crackle. Lower the preamp by about as much as your biggest lift to make room.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check an equaliser lowers its output by its highest lift", "Keep level")
        paragraphs: [
            KI18n.i18nc("@info help page of the 10-Band EQ block", "Does the preamp's job for you. When it is ticked, the block lowers its output by as much as the highest point of the curve the bands make, whenever that is above 0 dB. Lifting bands then changes the tone without making the loudest part of the sound any louder."),
            KI18n.i18nc("@info help page of the 10-Band EQ block", "Cutting bands changes nothing here: the sound just gets a little quieter. It is off for a new block.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@action:button put every band of an equaliser back at 0 dB", "Flatten")
        paragraphs: [
            KI18n.i18nc("@info help page of the 10-Band EQ block", "Puts all ten bands and the preamp back at 0 dB at once, so the block leaves the tone as it is. Keep level stays as it was. Ctrl+Z takes it back in one step."),
            KI18n.i18nc("@info help page of the 10-Band EQ block", "The button can only be pressed when something is not at 0 dB.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the 10-Band EQ block", "Headphones or laptop speakers make music sound thin, boomy or harsh."),
            KI18n.i18nc("@info help page of the 10-Band EQ block", "A voice sounds muddy, muffled or sharp, and you want it clearer."),
            KI18n.i18nc("@info help page of the 10-Band EQ block", "A recording has a low rumble or a high hiss you want less of."),
            KI18n.i18nc("@info help page of the 10-Band EQ block", "Some pitches are hard for you to hear, and you want them lifted.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the 10-Band EQ block", "Start from one of the presets at the bottom of the settings, such as Podcast voice, Rumble cut, Hiss cut or Laptop speakers, and adjust from there. Flat puts everything back."),
            KI18n.i18nc("@info help page of the 10-Band EQ block", "To find a band that bothers you, lift it a long way, listen, and then cut it once you know which one it is."),
            KI18n.i18nc("@info help page of the 10-Band EQ block", "Cut rather than lift where you can: taking away what is too much usually sounds more natural than adding to everything else."),
            KI18n.i18nc("@info help page of the 10-Band EQ block", "If a meter after the block keeps reaching the red part at the top, tick Keep level or lower the preamp."),
            KI18n.i18nc("@info help page of the 10-Band EQ block", "For a change at only one end, the Bass & Treble block is quicker. To aim at a single pitch precisely, use the Parametric EQ.")
        ]
    }
}
