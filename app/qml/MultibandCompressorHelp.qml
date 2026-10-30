// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Multiband Compressor block's help page (docs/dev/block-help.md).
// The facts are the block's (engine/blocks/multibandcompressor.h and
// .cpp: the ranges and defaults, each band's own timing, peaks followed,
// no latency), the crossover's (engine/dsp/crossover.h: the bands adding
// back up, the splits 1.5 times apart) and the panel's words
// (MultibandCompressorPanel, MultibandWords).
HelpPage {
    typeId: "effect.multibandcompressor"
    summary: KI18n.i18nc("@info help page of the Multiband Compressor block", "Three compressors side by side: one for the low sounds, one for the middle and one for the high. Each evens out its own part of the sound, so one part cannot pull the others down with it.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Multiband Compressor block", "A compressor turns the sound down when it gets louder than a set level, the threshold, and so brings loud and quiet closer together. An ordinary compressor turns everything down at once: a loud drum or bass note makes the voice above it duck too."),
            KI18n.i18nc("@info help page of the Multiband Compressor block", "This block first splits the sound by frequency, from low to high, into three bands: Low, Mid and High. Each band goes through a compressor of its own, with its own settings, and the three are then added back together."),
            KI18n.i18nc("@info help page of the Multiband Compressor block", "The split itself changes nothing you can hear: with every ratio at 1:1, the sound comes out as it went in. A new block does compress, gently, from −24 dB at 3:1 in every band."),
            KI18n.i18nc("@info help page of the Multiband Compressor block", "Each band is turned down on both sides together, so the stereo picture does not move. The sound is not delayed.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@info accessible name of a multiband compressor's bands", "Bands")
        covers: [
            KI18n.i18nc("@label the lowest of a multiband compressor's three bands", "Low"),
            KI18n.i18nc("@label the middle of a multiband compressor's three bands", "Mid"),
            KI18n.i18nc("@label the highest of a multiband compressor's three bands", "High")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Multiband Compressor block", "The strip at the top shows the three bands across the frequencies, from the lowest on the left to the highest on the right. While the session runs, a bar hangs from the top of each band as far as that band is being turned down, and the band shows how many decibels."),
            KI18n.i18nc("@info help page of the Multiband Compressor block", "The two lines between the bands are the splits. Drag one to move where two bands meet."),
            KI18n.i18nc("@info help page of the Multiband Compressor block", "Click a band in the strip, or choose Low, Mid or High under it, to pick the band whose curve and settings are shown below. The words beside the buttons say which frequencies the band holds. The panel opens on Mid."),
            KI18n.i18nc("@info help page of the Multiband Compressor block", "The curve shows what the picked band does: how loud the sound goes in, along the bottom, against how loud it comes out, up the side. The dashed line is the sound left alone; where the curve bends below it, the band is turning the sound down. While the session runs, a dot shows where the band's sound is on its curve.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label where a band's compressor starts working", "Threshold")
        paragraphs: [
            // i18n: dB stands for decibels.
            KI18n.i18nc("@info help page of the Multiband Compressor block", "The level where the picked band starts to be turned down, in decibels (dB) below full scale, from −60 dB to 0 dB. Lower means it works on more of the sound. Every band starts at −24 dB.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how much of what goes over the threshold comes out", "Ratio")
        paragraphs: [
            KI18n.i18nc("@info help page of the Multiband Compressor block", "How hard the band is held back above the threshold, from 1:1 to 20:1. At 3:1, where every band starts, a sound 3 dB over the threshold comes out only 1 dB over. At 1:1 the band is left alone.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how fast a band's compressor reacts to the band getting louder", "Attack")
        paragraphs: [
            // i18n: ms stands for milliseconds.
            KI18n.i18nc("@info help page of the Multiband Compressor block", "How quickly the band is turned down when it gets loud, from a tenth of a millisecond (ms) to 200 ms. A short attack catches sudden sounds; a longer one lets the start of each sound through, which keeps it lively."),
            KI18n.i18nc("@info help page of the Multiband Compressor block", "The bands start at different times: 20 ms for Low, 10 ms for Mid and 3 ms for High. Low sounds are slow waves, and turning them down too quickly would distort them; high sounds can be caught at once.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how fast a band's compressor lets go once the band is quieter", "Release")
        paragraphs: [
            KI18n.i18nc("@info help page of the Multiband Compressor block", "How quickly the band comes back up once it is quieter, from 10 ms to 3 seconds. The bands start at 250 ms for Low, 150 ms for Mid and 80 ms for High."),
            KI18n.i18nc("@info help page of the Multiband Compressor block", "Too short, and the band can flutter with every beat; too long, and it stays turned down after the loud part has passed.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label gain added to a band after it has been held back", "Makeup")
        paragraphs: [
            KI18n.i18nc("@info help page of the Multiband Compressor block", "Turns the band up or down after it has been compressed, from −20 dB to +20 dB, to give back the level compressing took away or to change the balance between the bands. Every band starts at 0 dB.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how wide the bend around every band's threshold is", "Knee")
        paragraphs: [
            KI18n.i18nc("@info help page of the Multiband Compressor block", "How gradually compressing starts around the threshold, from 0 dB, a sharp corner, to 24 dB, a wide gentle bend. One knee serves all three bands. A new block uses 6 dB."),
            KI18n.i18nc("@info help page of the Multiband Compressor block", "A wider knee eases in, which is harder to hear; a sharp one holds the level more exactly.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label where the low band ends and the middle begins", "Low split")
        paragraphs: [
            // i18n: Hz stands for hertz, kHz for kilohertz.
            KI18n.i18nc("@info help page of the Multiband Compressor block", "Where the Low band ends and the Mid band begins, in hertz (Hz), from 30 Hz to 2 kHz. A new block splits at 200 Hz.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label where the middle band ends and the high begins", "High split")
        paragraphs: [
            KI18n.i18nc("@info help page of the Multiband Compressor block", "Where the Mid band ends and the High band begins, from 500 Hz to 16 kHz. A new block splits at 3 kHz."),
            KI18n.i18nc("@info help page of the Multiband Compressor block", "The high split always stays at least one and a half times the low one, so the Mid band never gets too narrow to hold anything. Dragging a split stops there.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Multiband Compressor block", "Music with heavy bass makes the voices in it duck and swell."),
            KI18n.i18nc("@info help page of the Multiband Compressor block", "A voice has sharp, hissing “s” sounds that stand out."),
            KI18n.i18nc("@info help page of the Multiband Compressor block", "You want a loud, even sound for a stream or a podcast, and a single compressor makes it pump.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Multiband Compressor block", "If you just want an even level, the Simple Compressor is easier. Use this block when one part of the sound is the problem."),
            KI18n.i18nc("@info help page of the Multiband Compressor block", "To work on one band only, set the other two to a ratio of 1:1."),
            KI18n.i18nc("@info help page of the Multiband Compressor block", "To tame sharp “s” sounds, pick High, lower its threshold until its bar moves only on the “s”, and raise its ratio."),
            KI18n.i18nc("@info help page of the Multiband Compressor block", "Watch the bars while the session runs. A band that is always being turned down a lot is set too low: raise its threshold.")
        ]
    }
}
