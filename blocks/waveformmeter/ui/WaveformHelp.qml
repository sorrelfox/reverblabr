// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Waveform block's help page (docs/dev/block-help.md). The facts are the
// block's (blocks/waveformmeter/waveformmeter.h) and the panel's words.
HelpPage {
    typeId: "meter.waveform"
    summary: KI18n.i18nc("@info help page of the Waveform block", "Draws the sound at this point on the lane as it goes by. It changes nothing about the sound.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Waveform block", "The block passes the sound on exactly as it arrived, with no delay. Its tile and its card both draw what passes, at their own width, and it works only while one of them is on the screen."),
            KI18n.i18nc("@info help page of the Waveform block", "A trace that stands still is one you asked to hold. When the session is stopped or overloaded, or the block is off, no trace is shown and a line of words says why.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@action:button keeps the trace shown and takes no new one", "Hold")
        paragraphs: [
            KI18n.i18nc("@info help page of the Waveform block", "Keeps the trace on the screen and takes no new ones, so that you can look at a moment. You can change how it is drawn while it is held. Press it again to go back to the live sound.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@title:group the less usual settings", "Advanced")
        paragraphs: [
            KI18n.i18nc("@info help page of the Waveform block", "The settings below are folded under this heading, under the trace and Hold. Click the heading to show them, and again to fold them away.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how a Waveform is drawn", "Style")
        covers: [
            KI18n.i18nc("@item how a Waveform is drawn", "Line"),
            KI18n.i18nc("@item how a Waveform is drawn", "Filled"),
            KI18n.i18nc("@item how a Waveform is drawn", "Bars"),
            KI18n.i18nc("@item how a Waveform is drawn", "Dots")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Waveform block", "Line draws the wave itself, or the band it fills when more than one sample falls on a pixel. Filled draws how far the sound reaches either side of the middle, as a solid shape. Bars does the same in separate bars, and Dots marks the highest and lowest points. Switching shows at once.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how a Waveform moves between refreshes", "Motion")
        covers: [
            KI18n.i18nc("@item a Waveform shows each refresh as it is", "Real-time"),
            KI18n.i18nc("@item a Waveform eases and flows between refreshes", "Smooth")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Waveform block", "Real-time shows the newest sound as it is, once each refresh, and holds it until the next."),
            KI18n.i18nc("@info help page of the Waveform block", "Smooth draws at the rate of your screen. From 200 ms up the trace flows steadily across, a little behind the newest sound. Below 200 ms, or with the trigger on, each new trace eases in from the last, lined up on a rising zero crossing so that a steady tone stands still. Smooth makes the picture calmer and uses more of the graphics card.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how many times a second a Waveform is drawn", "Refresh rate, per second")
        paragraphs: [
            KI18n.i18nc("@info help page of the Waveform block", "How often a new look is taken at the sound, from 10 to 120 times a second. It can't show more than arrives, which is one cycle of the sound system at a time, and the card says so when the rate you chose is faster than that. It can't show more than your screen does either.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how much time a Waveform shows across its width", "Time shown")
        paragraphs: [
            KI18n.i18nc("@info help page of the Waveform block", "How much time goes across the width, from 10 ms to 10 s. A short time shows the shape of a wave, and a long time shows how the level moves. A click stays visible even at 10 s.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how much a Waveform's trace is enlarged", "Gain")
        covers: [
            KI18n.i18nc("@option:check a Waveform enlarges its trace to fill the height", "Auto fit")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Waveform block", "Enlarges the drawing and not the sound, from -24 to +48 dB. With Auto fit on, the trace is enlarged to fill the height: at once when it would go off the edge, and over about a second when it gets quieter.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label which channels a Waveform draws", "Channels")
        covers: [
            KI18n.i18nc("@item a Waveform draws left and right over each other", "Both"),
            KI18n.i18nc("@item a Waveform draws the left channel", "Left"),
            KI18n.i18nc("@item a Waveform draws the right channel", "Right"),
            KI18n.i18nc("@item a Waveform draws what left and right have in common", "Mid"),
            KI18n.i18nc("@item a Waveform draws where left and right differ", "Side"),
            KI18n.i18nc("@item a Waveform draws left above right", "Stacked")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Waveform block", "Both draws left and right over each other, and Stacked draws left above right. Mid is what the two have in common, half of their sum, and Side is where they differ, half of their difference. A mono sound has no Side.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check a Waveform starts each trace at the same point of the wave", "Start on a rising zero crossing")
        paragraphs: [
            KI18n.i18nc("@info help page of the Waveform block", "Starts each trace where the wave goes up through the middle, so that a steady tone stands still rather than running across. It helps with a tone or a held note. On speech or music there are many crossings and the trace still moves. It looks at the first channel shown.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check lines behind a Waveform's trace", "Grid")
        paragraphs: [
            KI18n.i18nc("@info help page of the Waveform block", "Lines behind the trace: eight divisions across, and the middle with half height up and down.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label what the height of a Waveform means", "Scale")
        covers: [
            KI18n.i18nc("@item the height of a Waveform is the amplitude", "Linear"),
            KI18n.i18nc("@item the height of a Waveform is in decibels", "Decibels")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Waveform block", "Linear makes the height the amplitude, which is how a wave looks. Decibels spreads the last 60 dB over the height, so that quiet sound can be seen, with a line every 12 dB.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Waveform block", "Put one before and one after a block to see what it does to the shape of the sound. Use a short time with the trigger on to look at a tone, and a long time to see how the level moves.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Waveform block", "A session can show up to 16 at once. Each one still passes the sound on with no delay, and one that is off the screen does no work beyond passing it on. A held trace is kept only while the tile or the card is on the screen.")
        ]
    }
}
