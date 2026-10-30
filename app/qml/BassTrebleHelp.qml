// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Bass & Treble block's help page (docs/dev/block-help.md). The facts
// are the block's (engine/blocks/basstreble.h and .cpp: the two shelves,
// their frequencies, the range and the glide) and the panel's
// (BassTreblePanel: the half-decibel steps and the double-click).
HelpPage {
    typeId: "effect.basstreble"
    summary: KI18n.i18nc("@info help page of the Bass & Treble block", "Lifts or cuts the low end and the high end of the sound, to make it warmer, fuller, brighter or softer.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Bass & Treble block", "The bass is the deep part of the sound: a bass drum, a bass guitar, a rumble. The treble is the bright part: cymbals, the sparkle and air of a recording, and hiss."),
            // i18n: Hz stands for hertz, kHz for kilohertz.
            KI18n.i18nc("@info help page of the Bass & Treble block", "Each of the two settings works on its own end. Bass works on the sound below about 100 hertz (Hz), Treble on the sound above about 10 kilohertz (kHz). The change fades in gradually towards each end, so the middle of the range, where voices are, is left alone."),
            // i18n: dB stands for decibels.
            KI18n.i18nc("@info help page of the Bass & Treble block", "Both are set in decibels (dB). A higher number lifts that end, a number below zero cuts it, and 0 dB leaves it exactly as it arrived. Every 6 dB is about twice or half the strength."),
            KI18n.i18nc("@info help page of the Bass & Treble block", "A change glides in over a fiftieth of a second, so moving a slider while sound plays makes no click.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider the low end on a Bass & Treble block", "Bass")
        paragraphs: [
            KI18n.i18nc("@info help page of the Bass & Treble block", "Lifts or cuts the low end, from −12 to +12 dB in steps of half a decibel. Lift it to make a thin sound warmer and fuller; cut it to take away boom and rumble."),
            KI18n.i18nc("@info help page of the Bass & Treble block", "A new block starts at 0 dB. Double-click the slider to put it back there.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider the high end on a Bass & Treble block", "Treble")
        paragraphs: [
            KI18n.i18nc("@info help page of the Bass & Treble block", "Lifts or cuts the high end, from −12 to +12 dB in steps of half a decibel. Lift it to make a dull sound brighter and more open; cut it to soften hiss or a sound that is too bright."),
            KI18n.i18nc("@info help page of the Bass & Treble block", "A new block starts at 0 dB. Double-click the slider to put it back there.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Bass & Treble block", "Music on small speakers sounds thin, or on big ones sounds boomy."),
            KI18n.i18nc("@info help page of the Bass & Treble block", "A recording sounds dull, or has a hiss you would rather not hear."),
            KI18n.i18nc("@info help page of the Bass & Treble block", "You want a quick change of tone, without the ten sliders of the 10-Band EQ.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Bass & Treble block", "Small changes go a long way. Try 3 dB before you try 12."),
            KI18n.i18nc("@info help page of the Bass & Treble block", "Cutting is often better than lifting: if the sound is too dull, try a little less bass before you add treble."),
            KI18n.i18nc("@info help page of the Bass & Treble block", "A lift makes the sound louder as well. If a meter after the block reaches the red part at the top, turn the lift down, or the sound down before it."),
            KI18n.i18nc("@info help page of the Bass & Treble block", "Both sliders are on the block's tile too, so you can change the tone without opening the settings.")
        ]
    }
}
