// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Denoise block's help page (docs/dev/block-help.md). The facts are the
// block's (engine/blocks/denoise.h: ten seconds of listening, the noise's
// loudest moments learned and not only its average, the octaves that hold
// nothing but the noise turned down in full, the noise followed when it
// grows, an earlier version's noise not used, the profile saved with the
// session, the ranges, the 2048-sample frame it holds the sound back by)
// and the panel's (DenoisePanel, DenoiseState).
HelpPage {
    typeId: "advanced.denoise"
    summary: KI18n.i18nc("@info help page of the Denoise block", "Learns what a background noise sounds like, such as a fan, a hiss, or the buzzing and ticking a sound card picks up from the computer, and then takes that noise out of everything that follows.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Denoise block", "The block first has to hear the noise on its own. Press Learn Noise while only the noise plays: the block listens for ten seconds. At every pitch, from the lowest to the highest, it remembers how loud the noise gets, not only how loud it usually is, so a noise that ticks, crackles or comes and goes is learned with its loudest moments. While it listens, the sound passes through as it is."),
            KI18n.i18nc("@info help page of the Denoise block", "From then on, it splits the sound into many narrow bands of pitch, about a thousand of them, in groups of an octave. A group that holds nothing louder than the learned noise is turned down as a whole, so the noise's own ticks cannot get through. In a group where something else is going on, such as a voice or music, the bands where it rises above the noise pass, and the others are turned down."),
            KI18n.i18nc("@info help page of the Denoise block", "A computer's noise often grows when the computer works harder. When the whole noise becomes a little louder, the block follows it, by up to ten decibels, and never lower than what it learned."),
            KI18n.i18nc("@info help page of the Denoise block", "The line at the top says where the block stands: Learning, with how far it has got; Active, with how many seconds of noise it learned; or Inactive, when it has learned nothing yet and lets the sound through unchanged."),
            KI18n.i18nc("@info help page of the Denoise block", "The noise it learned is saved with the session, so it is still there the next time you open it. A noise learned by an earlier version of Reverblabr is not used: the block says Learn the noise again, and lets the sound through until you do."),
            KI18n.i18nc("@info help page of the Denoise block", "The block holds the sound back by about four hundredths of a second, whatever the settings. Reverblabr keeps the other lanes in step with that delay.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@action:button start listening to the noise", "Learn Noise")
        covers: [
            KI18n.i18nc("@action:button drop the learned noise", "Forget Noise")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Denoise block", "Starts listening to the noise. It needs the session running, since it learns from what is playing; while the session is stopped the button waits."),
            KI18n.i18nc("@info help page of the Denoise block", "Once a noise is learned, the button says Forget Noise. Press it to drop the noise, for example to learn a different one. The block then lets the sound through unchanged until it learns again.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider", "Noise reduction")
        paragraphs: [
            // i18n: dB stands for decibels.
            KI18n.i18nc("@info help page of the Denoise block", "How far the block turns the noise down, in decibels (dB), from 0 to 40 dB. A new block starts at 20 dB."),
            KI18n.i18nc("@info help page of the Denoise block", "Higher takes out more of the noise. Lower leaves a little of it, which often sounds more natural than none.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider", "Keep sound above noise by")
        paragraphs: [
            KI18n.i18nc("@info help page of the Denoise block", "How far above the loudest the learned noise gets a band has to be before the block leaves it alone, from 0 to 20 dB. A new block starts at 3 dB."),
            KI18n.i18nc("@info help page of the Denoise block", "Raise it if some noise still comes through around a voice. Lower it if quiet parts of the sound you want, such as the end of a word, are being taken away with the noise.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Denoise block", "A computer fan or an air conditioner can be heard behind your microphone."),
            KI18n.i18nc("@info help page of the Denoise block", "An old recording or a cheap sound card has a steady hiss."),
            KI18n.i18nc("@info help page of the Denoise block", "A sound card buzzes or ticks with what the computer is doing, and you hear it whenever the sound you play stops."),
            KI18n.i18nc("@info help page of the Denoise block", "The noise is the same kind all the time, even if it ticks or comes and goes. For noise that keeps changing, such as traffic or people talking, try the Speech Denoise block instead.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Denoise block", "Learn the noise in the same place, with the same microphone and the same volume as the sound you will clean. Keep quiet while it listens."),
            KI18n.i18nc("@info help page of the Denoise block", "For the noise a sound card picks up from the computer, learn it with the computer doing what it usually does. If you turn the sound card's gain, learn the noise again."),
            KI18n.i18nc("@info help page of the Denoise block", "If the room changes, a different fan, a window opened, press Forget Noise and learn it again."),
            KI18n.i18nc("@info help page of the Denoise block", "If what is left sounds watery or twinkly, lower Noise reduction a little."),
            KI18n.i18nc("@info help page of the Denoise block", "Put the block before anything that turns the sound up or down, so it hears the noise at the level it learned it.")
        ]
    }
}
