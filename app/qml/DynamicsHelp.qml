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
// the timing (engine/dsp/dynamics.h and .cpp: the knee, the expander's
// range, the detectors' windows, the make-up glide), the panel's handles,
// keys and readings (DynamicsPanel) and the factory presets
// (engine/presets/effect.dynamics, named in app/presets.cpp).
HelpPage {
    typeId: "effect.dynamics"
    summary: KI18n.i18nc("@info help page of the Dynamics Processor block", "Holds loud sound back and pushes quiet sound further down, following a curve you shape by dragging it. Use it to even out a voice, keep peaks in check, or quieten the noise between words.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Dynamics Processor block", "The block has two parts that work on different levels of sound. A compressor works on loud sound: above a level you choose, the threshold, it lets only part of each rise through, so loud moments come out less loud."),
            KI18n.i18nc("@info help page of the Dynamics Processor block", "An expander works on very quiet sound: below a threshold of its own, it pushes the sound further down, so background noise in the pauses gets quieter still. Between the two thresholds the sound is left alone."),
            // i18n: dB stands for decibels.
            KI18n.i18nc("@info help page of the Dynamics Processor block", "The curve at the top of the settings shows all of this at once. Along the bottom is how loud the sound comes in, up the side how loud it goes out, both in decibels (dB). The dashed diagonal line is where nothing changes. Where the curve is below that line, the sound is turned down."),
            KI18n.i18nc("@info help page of the Dynamics Processor block", "While the session runs, a dot on the curve shows where the sound is right now. The bar beside the curve and the Gain reduction number under it show how much the block is turning the sound down, and In / out shows the level going in and the level coming out."),
            KI18n.i18nc("@info help page of the Dynamics Processor block", "Both sides of a stereo lane are turned down together, by the louder of the two, so the sound never shifts to one side."),
            KI18n.i18nc("@info help page of the Dynamics Processor block", "A new block holds anything above −20 dB back firmly, and its expander is off. The presets at the bottom of the settings give other starting points: Gentle leveling, Voice, Noise gate and Hard limiting, and Default to go back.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        title: KI18n.i18nc("@title a part of the Dynamics Processor's help page", "The curve")
        paragraphs: [
            KI18n.i18nc("@info help page of the Dynamics Processor block", "Four handles on the curve shape it. Press near one and drag; while you drag, a box beside it shows the settings it is changing. Point at a handle to read what it is set to.")
        ]
        points: [
            KI18n.i18nc("@info help page of the Dynamics Processor block, one handle of the curve", "Compressor threshold, the upper corner: drag it sideways to choose where the compressor starts, and up or down to change the Make-up gain."),
            KI18n.i18nc("@info help page of the Dynamics Processor block, one handle of the curve", "Ratio above the threshold, further up the line: drag it down for a flatter line, which holds loud sound back more, or up to let more through. Sideways it moves the threshold with it."),
            KI18n.i18nc("@info help page of the Dynamics Processor block, one handle of the curve", "Knee width, between the two: drag it sideways to make the bend around the threshold gentler or sharper."),
            KI18n.i18nc("@info help page of the Dynamics Processor block, one handle of the curve", "Expander threshold, the lower corner: drag it sideways to choose where the expander starts, and down to push the quiet sound below it further down.")
        ]
        closing: [
            KI18n.i18nc("@info help page of the Dynamics Processor block", "Each handle can also be reached with the Tab key. The left and right arrow keys then move it 2 dB a press, or half a decibel with Shift held, and the up and down arrow keys change its other setting, where it has one."),
            KI18n.i18nc("@info help page of the Dynamics Processor block", "Every setting the handles change is also under Details, as a number.")
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
        title: KI18n.i18nc("@label where the compressor starts working", "Threshold")
        paragraphs: [
            KI18n.i18nc("@info help page of the Dynamics Processor block", "The level above which the compressor holds the sound back, from −80 to 0 dB. 0 dB is full scale, the loudest a digital signal can be. A new block starts at −20 dB."),
            KI18n.i18nc("@info help page of the Dynamics Processor block", "The lower the threshold, the more of the sound is held back.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how much of what goes over the threshold comes out", "Ratio")
        paragraphs: [
            KI18n.i18nc("@info help page of the Dynamics Processor block", "How strongly the sound above the threshold is held back, from 1:1 to 20:1. At 4:1, sound that rises 4 dB above the threshold comes out only 1 dB above it. At 1:1 nothing is held back. A new block starts at 20:1, which is very firm.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:checkbox a ratio of infinity: nothing gets past the threshold", "Limit at the threshold")
        paragraphs: [
            KI18n.i18nc("@info help page of the Dynamics Processor block", "When it is on, nothing comes out louder than the threshold, however loud it goes in: the block works as a limiter, and the Ratio is set aside. It is off for a new block."),
            KI18n.i18nc("@info help page of the Dynamics Processor block", "Dragging the ratio handle on the curve turns it off again.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how wide the bend around the threshold is", "Knee")
        paragraphs: [
            KI18n.i18nc("@info help page of the Dynamics Processor block", "How gradually the compressor starts, from 0 to 24 dB. At 0 dB it starts all at once at the threshold. A wider knee starts holding back gently half its width below the threshold and reaches the full ratio half its width above, which is harder to hear. A new block starts at 12 dB.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label where the expander starts working", "Expander threshold")
        paragraphs: [
            KI18n.i18nc("@info help page of the Dynamics Processor block", "The level below which the expander pushes the sound further down, from −120 to −20 dB. A new block starts at −120 dB, below almost anything you will hear.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how steeply the quiet end is pushed down", "Expander ratio")
        paragraphs: [
            KI18n.i18nc("@info help page of the Dynamics Processor block", "How steeply the sound below the expander threshold falls away, from 1:1 to 50:1. At 2:1, sound 1 dB below the threshold comes out 2 dB below it. At 1:1 the expander does nothing, and a new block starts there.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label the most the expander may take off", "Expander range")
        paragraphs: [
            KI18n.i18nc("@info help page of the Dynamics Processor block", "The most the expander may turn the sound down, from 0 to 80 dB. It keeps the pauses quieter rather than completely silent, which sounds more natural."),
            KI18n.i18nc("@info help page of the Dynamics Processor block", "A new block starts at 0 dB, so the expander does nothing until both this and the Expander ratio are raised.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how long the expander stays open", "Expander hold")
        paragraphs: [
            KI18n.i18nc("@info help page of the Dynamics Processor block", "How long the expander waits, after the sound drops below its threshold, before it starts pushing it down: from 0 to 500 milliseconds (ms). It keeps the ends of words from being cut off, and stops sound that hovers around the threshold from fluttering. A new block starts at 0 ms.")
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
            KI18n.i18nc("@info help page of the Dynamics Processor block", "Turns the whole sound up or down after the compressor and the expander, from −40 to +40 dB. Holding loud sound back makes the sound quieter overall, and this brings it back up. A new block starts at 0 dB.")
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
            KI18n.i18nc("@info help page of the Dynamics Processor block", "After setting the compressor, raise the Make-up gain until the sound is as loud as it was before, so you hear the change in evenness rather than in level."),
            KI18n.i18nc("@info help page of the Dynamics Processor block", "For the expander, play the pauses only, and raise the Expander threshold until it sits just above the noise. If word endings get cut off, lower the threshold a little or lengthen the hold."),
            KI18n.i18nc("@info help page of the Dynamics Processor block", "Ctrl+Z takes back a whole drag of a handle in one step.")
        ]
    }
}
