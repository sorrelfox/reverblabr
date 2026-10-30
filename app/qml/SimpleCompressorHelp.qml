// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Simple Compressor block's help page (docs/dev/block-help.md). The
// facts are the block's (engine/blocks/simplecompressor.h and .cpp: the
// modes and what each sets, Gentle to start, Off as a flat curve, no mode
// above −6 dBFS from a full-scale input) and the panel's words
// (SimpleCompressorPanel, CompressorWords).
HelpPage {
    typeId: "effect.simplecompressor"
    summary: KI18n.i18nc("@info help page of the Simple Compressor block", "Evens out the level, so the loud moments are not so loud and the quiet ones are easier to hear. You choose one mode instead of setting six numbers.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Simple Compressor block", "A compressor listens to how loud the sound is. When it rises above a set level, the threshold, the compressor turns it down, and the further it goes over, the more it turns it down. Below the threshold, it leaves the sound alone."),
            KI18n.i18nc("@info help page of the Simple Compressor block", "With the loud moments held back, the whole sound is then turned up again a little. The gap between loud and quiet is smaller, and quiet details come forward."),
            KI18n.i18nc("@info help page of the Simple Compressor block", "Each mode is a set of numbers chosen to work well together, from barely touching the sound to holding it very firmly. No mode makes a clean sound clip: even the loudest input comes out at least 6 dB below full scale."),
            KI18n.i18nc("@info help page of the Simple Compressor block", "The block's tile shows the mode, how hard it compresses and from which level. The sound is not delayed.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:listbox how firmly the Simple Compressor works", "Compressor mode")
        paragraphs: [
            KI18n.i18nc("@info help page of the Simple Compressor block", "How firmly the block works. The line under the list says what the chosen mode is for. A new block starts in Gentle.")
        ]
        points: [
            KI18n.i18nc("@info help page of the Simple Compressor block, one choice of Compressor mode", "Off: the sound passes through exactly as it arrives. Switching between Off and a mode glides, so it makes no jump."),
            KI18n.i18nc("@info help page of the Simple Compressor block, one choice of Compressor mode", "Gentle: takes the edge off the loudest moments and is hard to hear working. Good for music."),
            KI18n.i18nc("@info help page of the Simple Compressor block, one choice of Compressor mode", "Moderate: brings loud and quiet passages clearly closer together, for films and games played at low volume."),
            KI18n.i18nc("@info help page of the Simple Compressor block, one choice of Compressor mode", "Heavy: holds the level firmly, so quiet details come up close to the loud ones."),
            KI18n.i18nc("@info help page of the Simple Compressor block, one choice of Compressor mode", "Voice: keeps a voice steady and close. It reacts quickly enough to catch each loud syllable, and lets go in time for the next."),
            KI18n.i18nc("@info help page of the Simple Compressor block, one choice of Compressor mode", "Broadcast: dense and even, like a radio station. It works almost like a limiter above its threshold.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@title:group the numbers a Simple Compressor mode sets", "Mode settings")
        paragraphs: [
            KI18n.i18nc("@info help page of the Simple Compressor block", "Click the heading to see the numbers the chosen mode sets. They are there to read, not to change:")
        ]
        points: [
            // i18n: dB stands for decibels.
            KI18n.i18nc("@info help page of the Simple Compressor block, one of the mode's numbers", "Threshold: the level, in decibels (dB) below full scale, where compressing starts."),
            KI18n.i18nc("@info help page of the Simple Compressor block, one of the mode's numbers", "Ratio: how hard the sound above the threshold is held back. At 4:1, a sound 4 dB over comes out only 1 dB over."),
            KI18n.i18nc("@info help page of the Simple Compressor block, one of the mode's numbers", "Knee: how gradually compressing starts around the threshold. A wide knee eases in, which is harder to hear."),
            // i18n: ms stands for milliseconds.
            KI18n.i18nc("@info help page of the Simple Compressor block, one of the mode's numbers", "Attack: how quickly it turns the sound down when it gets loud, in milliseconds (ms)."),
            KI18n.i18nc("@info help page of the Simple Compressor block, one of the mode's numbers", "Release: how quickly it lets the sound come back up once it is quieter."),
            KI18n.i18nc("@info help page of the Simple Compressor block, one of the mode's numbers", "Makeup gain: how much the whole sound is turned up again afterwards."),
            KI18n.i18nc("@info help page of the Simple Compressor block, one of the mode's numbers", "Follows: what it listens to. Average (RMS) follows how loud the sound feels, and is what Gentle and Moderate use. Peaks follows every sudden loud moment, and is what the firmer modes use.")
        ]
        closing: [
            KI18n.i18nc("@info help page of the Simple Compressor block", "To set these numbers yourself, use the Dynamics Processor block: the same numbers there sound the same as the mode here.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Simple Compressor block", "In a film, the voices are too quiet and the explosions too loud."),
            KI18n.i18nc("@info help page of the Simple Compressor block", "Somebody on a call or in a recording keeps getting louder and quieter."),
            KI18n.i18nc("@info help page of the Simple Compressor block", "You listen in a noisy place, and the quiet parts get lost.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Simple Compressor block", "Start with Gentle and go firmer one mode at a time, until the level is as even as you like."),
            KI18n.i18nc("@info help page of the Simple Compressor block", "If the sound starts to breathe or pump, rising and falling with the loud moments, choose a gentler mode."),
            KI18n.i18nc("@info help page of the Simple Compressor block", "For speech, try Voice first."),
            KI18n.i18nc("@info help page of the Simple Compressor block", "Switch between Off and a mode to hear what the block is doing.")
        ]
    }
}
