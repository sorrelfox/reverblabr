// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Parametric EQ block's help page (docs/dev/block-help.md). The facts
// are the block's (engine/blocks/parametriceq.h and .cpp: ten slots, three
// bells at first, the types, the ranges, the glides and fades) and the
// panel's (ParametricEqPanel, ParametricEqBands, FilterWords: the handles,
// the wheel, where a new band goes, which types have a gain).
HelpPage {
    typeId: "advanced.parametriceq"
    summary: KI18n.i18nc("@info help page of the Parametric EQ block", "Shapes the tone of the sound with up to ten bands that you place yourself, each lifting, cutting or filtering the sound around a frequency you choose.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            // i18n: Hz stands for hertz, kHz for kilohertz.
            KI18n.i18nc("@info help page of the Parametric EQ block", "Every sound is a mix of low and high pitches. Their frequency is counted in hertz (Hz), vibrations a second: a deep bass is around 50 Hz, a voice mostly between 100 Hz and 4000 Hz, and the hiss of a cymbal near 10 000 Hz, or 10 kHz. An equaliser, EQ for short, makes some of those frequencies louder or quieter."),
            KI18n.i18nc("@info help page of the Parametric EQ block", "Each band works on one part of that range. You choose where it is, how wide it is, and how much it lifts or cuts. The curve at the top shows what all the bands do together: up is louder, down is quieter, from 20 Hz at the left to 20 kHz at the right."),
            KI18n.i18nc("@info help page of the Parametric EQ block", "Each band has a round handle on the curve. Drag it sideways to move the band's frequency, and up or down to change its gain. Turn the mouse wheel over a handle to make the band narrower or wider."),
            KI18n.i18nc("@info help page of the Parametric EQ block", "While the session runs, the sound leaving the block is drawn faintly behind the curve, so you can see where its low and high parts are."),
            KI18n.i18nc("@info help page of the Parametric EQ block", "A new block has three bands, at 100 Hz, 1 kHz and 8 kHz, all at 0 dB, so it changes nothing until you move one. Every change glides in, and a band that is added, removed or changed in type fades, so nothing clicks. The block adds no delay.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@title:group the Parametric EQ's bands, one to pick", "Bands")
        paragraphs: [
            KI18n.i18nc("@info help page of the Parametric EQ block", "A button for each band, named by its frequency, from low to high. Click one to pick that band; the settings below are the picked band's. Clicking or dragging a handle on the curve picks its band too. A band that is switched off is drawn faded.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@action:button add a band to a Parametric EQ", "Add Band")
        covers: [
            KI18n.i18nc("@action:button remove the selected band from a Parametric EQ", "Delete Band")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Parametric EQ block", "Add Band adds a bell at 0 dB in the middle of the widest stretch of the curve with no band in it, and picks it. There can be ten bands at most."),
            KI18n.i18nc("@info help page of the Parametric EQ block", "Delete Band removes the picked band.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:listbox the type of a Parametric EQ band", "Type")
        paragraphs: [
            KI18n.i18nc("@info help page of the Parametric EQ block", "What the band does to the sound around its frequency:")
        ]
        points: [
            KI18n.i18nc("@info help page of the Parametric EQ block, a type of band", "Bell: lifts or cuts a range of frequencies around the band's frequency, as much as its gain says. What a new band is."),
            KI18n.i18nc("@info help page of the Parametric EQ block, a type of band", "Low shelf: lifts or cuts everything below the frequency, for more or less bass."),
            KI18n.i18nc("@info help page of the Parametric EQ block, a type of band", "High shelf: lifts or cuts everything above the frequency, for more or less treble."),
            KI18n.i18nc("@info help page of the Parametric EQ block, a type of band", "Low-pass: keeps what is below the frequency and takes away more and more of what is above it."),
            KI18n.i18nc("@info help page of the Parametric EQ block, a type of band", "High-pass: keeps what is above the frequency and takes away more and more of what is below it, such as rumble."),
            KI18n.i18nc("@info help page of the Parametric EQ block, a type of band", "Notch: takes out a narrow range around the frequency almost completely, such as a whistle."),
            KI18n.i18nc("@info help page of the Parametric EQ block, a type of band", "Band-pass: keeps only a range around the frequency, as a telephone does.")
        ]
        closing: [
            KI18n.i18nc("@info help page of the Parametric EQ block", "Only a bell and the shelves have a gain. The others leave Gain unavailable, and their handle stays on the 0 dB line.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check a Parametric EQ band is switched on", "Band on")
        paragraphs: [
            KI18n.i18nc("@info help page of the Parametric EQ block", "The switch beside Type turns the picked band off and on again, keeping its settings. Use it to hear the sound with and without the band.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider a Parametric EQ band's frequency", "Frequency")
        paragraphs: [
            KI18n.i18nc("@info help page of the Parametric EQ block", "Where the band is, from 20 Hz to 20 kHz. The slider moves by octaves rather than by hertz, as the ear hears pitch, so the low end has as much room as the high.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider a Parametric EQ band's gain", "Gain")
        paragraphs: [
            // i18n: dB stands for decibels.
            KI18n.i18nc("@info help page of the Parametric EQ block", "How much a bell or a shelf lifts or cuts, in decibels (dB), from −18 dB to +18 dB. 0 dB changes nothing, +6 dB is about twice the strength and −6 dB about half.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider how narrow a Parametric EQ band is", "Q")
        paragraphs: [
            KI18n.i18nc("@info help page of the Parametric EQ block", "How narrow the band is, from 0.1 to 18. A new band has 1. Higher is narrower: a high Q picks out a single note or a whistle, a low Q changes a broad part of the sound gently."),
            KI18n.i18nc("@info help page of the Parametric EQ block", "For the shelves and the pass filters it shapes the bend where they start: around 0.7 is smooth, and higher adds a small bump just before it.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Parametric EQ block", "A voice sounds muddy, boomy or harsh, and you want to fix just that part."),
            KI18n.i18nc("@info help page of the Parametric EQ block", "A steady whistle or ring needs taking out without dulling the rest."),
            KI18n.i18nc("@info help page of the Parametric EQ block", "The bass rumbles from handling the microphone, or traffic outside."),
            KI18n.i18nc("@info help page of the Parametric EQ block", "Your speakers or headphones have too much or too little of something, and you want to even them out.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Parametric EQ block", "To find a problem, raise a narrow bell by 10 dB or so and sweep it slowly left and right. Where the problem gets loudest, stop, then pull the gain down below 0 dB."),
            KI18n.i18nc("@info help page of the Parametric EQ block", "Cut more than you lift. Taking away what is wrong usually sounds better than adding more of everything else, and keeps the sound from getting too loud."),
            KI18n.i18nc("@info help page of the Parametric EQ block", "A high-pass around 80 Hz clears rumble from most voices without changing how they sound."),
            KI18n.i18nc("@info help page of the Parametric EQ block", "Switch a band off and on again with Band on to check that it still helps.")
        ]
    }
}
