// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Spectrum block's help page (docs/dev/block-help.md). The facts are the
// block's (engine/blocks/spectrummeter.h) and the panel's words.
HelpPage {
    typeId: "meter.spectrum"
    summary: KI18n.i18nc("@info help page of the Spectrum block", "Shows how much of each frequency there is at this point on the lane, as a row of bars. It changes nothing about the sound.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Spectrum block", "The block passes the sound on exactly as it arrived, with no delay. Its tile and its card both show the bars, and it works only while one of them is on the screen."),
            KI18n.i18nc("@info help page of the Spectrum block", "A bar stands at the loudest thing in its range of frequencies, with 0 dBFS at the top: a sine wave at full scale reaches the top in the bar that holds it, however many bars there are. The range goes from 20 Hz to 20 kHz."),
            KI18n.i18nc("@info help page of the Spectrum block", "Bars that stand still are bars you asked to hold. When the session is stopped or overloaded, or the block is off, no bars are shown and a line of words says why.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@action:button keeps the bars shown and takes no new ones", "Hold")
        paragraphs: [
            KI18n.i18nc("@info help page of the Spectrum block", "Keeps the bars on the screen and takes no new ones, so that you can look at a moment. You can change how they are read while they are held. Press it again to go back to the live sound.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@title:group the less usual settings", "Advanced")
        paragraphs: [
            KI18n.i18nc("@info help page of the Spectrum block", "The settings below are folded under this heading, under the bars and Hold. Click the heading to show them, and again to fold them away.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how many bars a Spectrum shows", "Bars")
        paragraphs: [
            KI18n.i18nc("@info help page of the Spectrum block", "How many bars the range is divided into, from 16 to 128. A tile shows fewer when it has no room for them all.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how a Spectrum's bars move between refreshes", "Motion")
        covers: [
            KI18n.i18nc("@item a Spectrum shows each refresh as it is", "Real-time"),
            KI18n.i18nc("@item a Spectrum's bars rise quickly and ease down between refreshes", "Smooth")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Spectrum block", "Real-time shows each refresh's bars as they are, and holds them until the next."),
            KI18n.i18nc("@info help page of the Spectrum block", "Smooth moves the bars at the rate of your screen: they rise quickly and ease down when the sound falls. It makes the picture calmer and uses more of the graphics card.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how many times a second a Spectrum is worked out", "Refresh rate, per second")
        paragraphs: [
            KI18n.i18nc("@info help page of the Spectrum block", "How often the bars are worked out, from 10 to 120 times a second. A slow rate misses nothing: the bars show the loudest moment since the last refresh, as far back as the block keeps sound. It can't show more than arrives, which is one cycle of the sound system at a time, and the card says so when the rate you chose is faster than that.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how a Spectrum spreads the frequencies across its width", "Frequency scale")
        covers: [
            KI18n.i18nc("@item a Spectrum gives each octave the same width", "Logarithmic"),
            KI18n.i18nc("@item a Spectrum gives each hertz the same width", "Linear")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Spectrum block", "Logarithmic gives each octave the same width, which is how we hear pitch. Linear gives each hertz the same width, which leaves most of the width to the highest sounds and is good for looking at harmonics.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how many samples of sound each look of a Spectrum takes in", "FFT size")
        paragraphs: [
            KI18n.i18nc("@info help page of the Spectrum block", "How much sound each look takes in, from 1024 to 8192 samples. A large size tells low notes apart but blurs quick sounds together, and a small size does the opposite. The card says what your choice comes to."),
            KI18n.i18nc("@info help page of the Spectrum block", "At the bottom of a logarithmic scale several bars may share what one size can tell apart. They all show it, so a low sine draws a hump as wide as the size makes it, and you get no empty bars.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label the level at the bottom of a Spectrum's view", "Level range")
        paragraphs: [
            KI18n.i18nc("@info help page of the Spectrum block", "The level at the bottom of the view, from -60 to -120 dBFS. The top is always 0 dBFS. A low floor shows quiet sound and noise, and a high one leaves more height for what is loud.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how much a Spectrum lifts high frequencies against low ones", "Tilt")
        paragraphs: [
            KI18n.i18nc("@info help page of the Spectrum block", "Lifts high frequencies and lowers low ones by this much per octave, turning about 1 kHz, in the drawing and not in the sound. Music is usually strongest at the bottom, and 3 dB or 4.5 dB per octave makes its bars easier to compare. With tilt on, a bar's height is no longer the true level.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label which channels a Spectrum reads", "Channels")
        covers: [
            KI18n.i18nc("@item a Spectrum reads left and right mixed together", "Mix"),
            KI18n.i18nc("@item a Spectrum reads the left channel", "Left"),
            KI18n.i18nc("@item a Spectrum reads the right channel", "Right")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Spectrum block", "Mix is left and right added together and halved, as a mono mix would be. The same sound in both reads as it is, a sound in one channel reads 6 dB lower, and sound in opposite phase cancels out.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how a Spectrum's bars are drawn", "Style")
        covers: [
            KI18n.i18nc("@item how a Spectrum's bars are drawn", "Solid"),
            KI18n.i18nc("@item how a Spectrum's bars are drawn, in lit steps like a row of lamps", "Segmented")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Spectrum block", "Solid draws plain bars. Segmented draws them in lit steps, like a row of lamps. Switching shows at once.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check a mark over each of a Spectrum's bars at its recent peak", "Peak marks")
        paragraphs: [
            KI18n.i18nc("@info help page of the Spectrum block", "A mark over each bar at its highest. It stays for a second and then falls slowly.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check lines and numbers behind a Spectrum's bars", "Grid and labels")
        paragraphs: [
            KI18n.i18nc("@info help page of the Spectrum block", "Lines behind the bars at round frequencies and every 20 dB. The card names them as well, and a tile has no room for the numbers.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Spectrum block", "Put one before and one after an equalizer or a filter to see what it does, or use one to find the frequency of a hum or a whistle.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Spectrum block", "A session can show up to 16 Waveform and Spectrum blocks at once. Each one still passes the sound on with no delay, and one that is off the screen does no work beyond passing it on. Held bars are kept only while the tile or the card is on the screen.")
        ]
    }
}
