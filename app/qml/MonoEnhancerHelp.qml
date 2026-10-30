// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Mono Enhancer block's help page (docs/dev/block-help.md). The facts
// are the block's (engine/blocks/monoenhancer.h: the fold to one signal,
// the 12 ms copy above the crossover, the sides adding back up, the range
// and the start) and the panel's words (MonoEnhancerPanel).
HelpPage {
    typeId: "effect.monoenhancer"
    summary: KI18n.i18nc("@info help page of the Mono Enhancer block", "Makes a mono recording sound wider in stereo, and still sounds just as it did when it is played in mono.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Mono Enhancer block", "A mono recording has one channel. Played on two speakers, the same sound comes out of both, so it seems to come from a single point between them."),
            KI18n.i18nc("@info help page of the Mono Enhancer block", "This block makes the two sides a little different from each other above a frequency you choose. It adds a copy of the higher sounds, twelve thousandths of a second late, to one side, and takes the same copy away from the other. That is far too short to hear as an echo; the ear hears it as width."),
            KI18n.i18nc("@info help page of the Mono Enhancer block", "Because one side gains what the other loses, the two sides always add back up to the sound that went in. Played on one speaker, or mixed down to mono later, it sounds exactly as it did."),
            KI18n.i18nc("@info help page of the Mono Enhancer block", "Below the chosen frequency nothing is added, so the bass stays in the middle, where it sounds firm."),
            KI18n.i18nc("@info help page of the Mono Enhancer block", "A stereo source is first made mono, by averaging its two sides, so its own stereo is replaced by the block's width.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider where a Mono Enhancer's width starts", "Crossover frequency")
        paragraphs: [
            // i18n: Hz stands for hertz, kHz for kilohertz.
            KI18n.i18nc("@info help page of the Mono Enhancer block", "Where the width starts, in hertz (Hz), from 100 Hz to 2 kHz. Everything above it is spread out; everything below it stays in the middle. A new block starts at 500 Hz."),
            KI18n.i18nc("@info help page of the Mono Enhancer block", "Lower spreads more of the sound, including the body of a voice. Higher spreads only the brighter part and keeps more of the sound in the middle."),
            KI18n.i18nc("@info help page of the Mono Enhancer block", "The slider gives each octave, a doubling of the frequency, the same room. The change glides in, so you can drag it while you listen.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Mono Enhancer block", "An old recording, a radio show or a phone call is in mono and sounds narrow on headphones."),
            KI18n.i18nc("@info help page of the Mono Enhancer block", "A mono microphone should feel less flat, and the result must still work for listeners in mono.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Mono Enhancer block", "Listen on headphones or with the speakers apart while you set it: that is where the width is heard."),
            KI18n.i18nc("@info help page of the Mono Enhancer block", "Start at 500 Hz. If a voice starts to sound hollow, raise the crossover."),
            KI18n.i18nc("@info help page of the Mono Enhancer block", "Do not use it on a recording that is already stereo: its own left and right would be lost.")
        ]
    }
}
