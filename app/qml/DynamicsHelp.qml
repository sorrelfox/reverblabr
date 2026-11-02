// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Dynamics Processor's help page (docs/dev/block-help.md). The facts
// are the block's parameters (engine/blocks/dynamics.cpp), the curve and
// the timing (engine/dsp/dynamics.h and .cpp: the four points, the floor, the
// detectors' windows, the make-up glide), the panel's points, keys and
// readings (DynamicsPanel) and the factory presets
// (engine/presets/effect.dynamics, named in app/presets.cpp).
HelpPage {
    typeId: "effect.dynamics"
    summary: KI18n.i18nc("@info help page of the Dynamics Processor block", "Holds loud sound back and pushes quiet sound down, following a curve you shape by dragging four points. Use it to even out a voice, keep peaks in check, or cut the noise between words.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            // i18n: dB stands for decibels.
            KI18n.i18nc("@info help page of the Dynamics Processor block", "The graph at the top of the settings is the block. Along the bottom is how loud the sound comes in, up the side how loud it goes out, both in decibels (dB). The dashed diagonal line is the main line: on it, sound comes out as it went in. The curve is never above that line, so this block only ever turns sound down."),
            KI18n.i18nc("@info help page of the Dynamics Processor block", "At the loud end the curve bends away from the diagonal, so loud moments come out less loud. At the quiet end it can drop away from the diagonal, so noise in the pauses gets quieter or disappears. Between the two the sound is left alone."),
            KI18n.i18nc("@info help page of the Dynamics Processor block", "While the session runs, a dot on the curve shows where the sound is right now. The bar beside the curve and the Gain reduction number under it show how much the block is turning the sound down, and In / out shows the level going in and the level coming out."),
            KI18n.i18nc("@info help page of the Dynamics Processor block", "Both sides of a stereo lane are turned down together, by the louder of the two, so the sound never shifts to one side."),
            KI18n.i18nc("@info help page of the Dynamics Processor block", "A new block holds anything above −26 dB back firmly, and leaves the quiet end alone. The presets at the bottom of the settings give other starting points: Gentle leveling, Voice, Noise gate and Hard limiting, and Default to go back.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        title: KI18n.i18nc("@title a part of the Dynamics Processor's help page", "The curve")
        paragraphs: [
            KI18n.i18nc("@info help page of the Dynamics Processor block", "Four points shape the curve, and each moves along one line only. Press near one and drag. While you drag, the point lights up and a box beside it shows the settings it is changing. Point at one to read what it is set to.")
        ]
        points: [
            KI18n.i18nc("@info help page of the Dynamics Processor block, one point of the curve", "Cut-off, the point on the bottom edge: moves left and right along the bottom. Anything quieter than this point comes out as nothing. Between here and the expander threshold the curve climbs in a straight line, so moving this point to the right makes that line steeper. It sets the Expander ratio, and it cannot reach the next point."),
            KI18n.i18nc("@info help page of the Dynamics Processor block, one point of the curve", "Expander threshold, the lower point on the diagonal: moves along the diagonal. It sets where the quiet end starts to drop away, and the Cut-off moves with it, so the line stays as steep as it was. It stops at the Compressor threshold."),
            KI18n.i18nc("@info help page of the Dynamics Processor block, one point of the curve", "Compressor threshold, the upper point on the diagonal: moves along the diagonal, as far as +20 dB. It sets where the curve starts to bend, and the Height point moves with it. It stops at the Expander threshold."),
            KI18n.i18nc("@info help page of the Dynamics Processor block, one point of the curve", "Height, the point on the right edge: moves up and down the edge. The curve always bends smoothly from the Compressor threshold to this point, and the point cannot go lower than that threshold. Right down, the curve is flat and nothing louder gets through.")
        ]
        closing: [
            KI18n.i18nc("@info help page of the Dynamics Processor block", "Each point can also be reached with the Tab key. Any arrow key then moves it along its line, 2 dB a press, or half a decibel with Shift held: right and up towards the loud end, left and down away from it."),
            KI18n.i18nc("@info help page of the Dynamics Processor block", "The Make-up gain is not part of the curve: it changes the sound without moving the line. Every setting the points change is also under Details, as a number.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@action:button every setting written out as a number", "Details")
        paragraphs: [
            KI18n.i18nc("@info help page of the Dynamics Processor block", "Click Details to show every setting of the block as a slider with its number, including the ones the curve does not show. It is closed when the settings open.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label where the curve starts bending away from the diagonal", "Threshold")
        paragraphs: [
            KI18n.i18nc("@info help page of the Dynamics Processor block", "The level above which the block holds the sound back, from −120 to +20 dB. 0 dB is full scale, the loudest a digital signal can be. A new block starts at −26 dB."),
            KI18n.i18nc("@info help page of the Dynamics Processor block", "The lower the threshold, the more of the sound is held back. It cannot go below the Expander threshold.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how high the curve can rise above the threshold", "Height")
        paragraphs: [
            KI18n.i18nc("@info help page of the Dynamics Processor block", "How high the curve can rise above the threshold, however loud the sound gets, from 0 to 40 dB. At 0 dB the block is a hard limiter: nothing comes out louder than the threshold. At a large height the curve bends slowly and stays near the diagonal for longer. A new block starts at 7 dB, which is firm."),
            KI18n.i18nc("@info help page of the Dynamics Processor block", "The curve only ever approaches this height, so it reaches it near the right edge only when the threshold is far below there, and it stays under the diagonal whatever the height.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label where the expander starts working", "Expander threshold")
        paragraphs: [
            KI18n.i18nc("@info help page of the Dynamics Processor block", "The level below which the quiet end of the curve drops away from the diagonal, from −120 dB up to the Threshold. A new block starts at −80 dB, with an Expander ratio that leaves it alone.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how steeply the quiet end is pushed down", "Expander ratio")
        paragraphs: [
            KI18n.i18nc("@info help page of the Dynamics Processor block", "How steeply the sound below the Expander threshold falls away, from 1:1 to 50:1. At 2:1, sound 1 dB below that threshold comes out 2 dB below it. At 1:1 nothing falls away, and a new block starts there. A high ratio is a noise gate: the sound stops almost at once below the threshold."),
            KI18n.i18nc("@info help page of the Dynamics Processor block", "Where the line reaches the bottom of the graph, the sound comes out as nothing. Dragging the Cut-off point changes this number.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how long the expander stays open", "Expander hold")
        paragraphs: [
            KI18n.i18nc("@info help page of the Dynamics Processor block", "How long the block waits, after the sound drops below the Expander threshold, before it starts pushing it down: from 0 to 500 milliseconds (ms). It keeps the ends of words from being cut off, and stops sound that hovers around the threshold from fluttering. A new block starts at 0 ms.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how fast the block reacts to a sound getting louder", "Attack")
        paragraphs: [
            KI18n.i18nc("@info help page of the Dynamics Processor block", "How quickly the block turns the sound down when it needs to, from 0.1 to 200 ms. A short attack catches sudden peaks; a longer one lets the start of each sound through, which keeps drums and speech crisp. A new block starts at 1 ms.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how fast the block lets go again", "Release")
        paragraphs: [
            KI18n.i18nc("@info help page of the Dynamics Processor block", "How quickly the block lets the sound come back up afterwards, from 10 to 3000 ms. A short release can make the level pump audibly; a long one sounds smoother but keeps quiet parts after a loud one too low for a while. A new block starts at 50 ms.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label gain added after the curve", "Make-up gain")
        paragraphs: [
            KI18n.i18nc("@info help page of the Dynamics Processor block", "Turns the whole sound up or down after the curve, from −40 to +40 dB. Holding loud sound back makes the sound quieter overall, and this brings it back up. It does not move the line on the graph. A new block starts at 0 dB.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label what the block follows: the loudest sample or a short average", "Detector")
        paragraphs: [
            KI18n.i18nc("@info help page of the Dynamics Processor block", "How the block measures the level it compares with the thresholds. A new block uses peak.")
        ]
        points: [
            KI18n.i18nc("@info help page of the Dynamics Processor block, one choice of Detector", "peak: the highest point of the sound wave over the last few thousandths of a second. It reacts to every peak, so nothing slips past."),
            // i18n: rms is short for root mean square; the sentence explains it.
            KI18n.i18nc("@info help page of the Dynamics Processor block, one choice of Detector", "rms: an average over the last hundredth of a second, which follows how loud the sound feels more closely. RMS is short for root mean square, a way of averaging.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Dynamics Processor block", "A voice is sometimes too loud and sometimes too quiet, and you want it steadier."),
            KI18n.i18nc("@info help page of the Dynamics Processor block", "Sudden loud sounds, such as laughter or a door, keep making listeners reach for the volume."),
            KI18n.i18nc("@info help page of the Dynamics Processor block", "A microphone picks up a fan or traffic between sentences, and you want the pauses quieter."),
            KI18n.i18nc("@info help page of the Dynamics Processor block", "You want a firm ceiling that the sound never goes past, before a recording or a stream.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Dynamics Processor block", "Start from a preset close to what you want, Voice for speech or Noise gate for a noisy microphone, and adjust from there."),
            KI18n.i18nc("@info help page of the Dynamics Processor block", "Watch Gain reduction while the sound plays. A few decibels is gentle; much more than 10 dB, most of the time, is heavy and can be heard."),
            KI18n.i18nc("@info help page of the Dynamics Processor block", "After setting the loud end, raise the Make-up gain until the sound is as loud as it was before, so you hear the change in evenness rather than in level."),
            KI18n.i18nc("@info help page of the Dynamics Processor block", "For the pauses, play them only, drag the Cut-off point to the right until the noise goes, and move the Expander threshold just above the noise. If word endings get cut off, lower the threshold a little or lengthen the hold."),
            KI18n.i18nc("@info help page of the Dynamics Processor block", "Ctrl+Z takes back a whole drag of a point in one step.")
        ]
    }
}
