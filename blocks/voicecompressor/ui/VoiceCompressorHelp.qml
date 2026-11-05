// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import org.kde.ki18n

// The Voice Compressor block's help page (docs/dev/block-help.md). The
// facts are the block's (blocks/voicecompressor/voicecompressor.h and .cpp:
// the law, the gate before the compressor, the two release followers, the
// ceiling, the delay) and the panel's words (VoiceCompressorPanel,
// VoiceCompressorWords).
HelpPage {
    typeId: "effect.voicecompressor"
    summary: KI18n.i18nc("@info help page of the Voice Compressor block", "Holds a voice steady with three controls: how much to compress, how far to lower the quiet between words, and the highest level that may leave the block.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Voice Compressor block", "The sound goes through a gate, then a compressor, then a limiter. The gate gently lowers what is under its level. The compressor takes off some of what is loud and brings the whole sound back up. The limiter stops any peak at the ceiling."),
            KI18n.i18nc("@info help page of the Voice Compressor block", "Compression makes up the level by itself, so lowering it makes the voice louder as well as steadier. There is no attack or release to set: a syllable is let go quickly, and a long loud line slowly."),
            KI18n.i18nc("@info help page of the Voice Compressor block", "A new block is set to Compression -12 dB, Gate off and Ceiling -6 dBFS. It works from the start: a sound at -18 dBFS comes out about 3.5 dB higher. The sound is delayed by about 2 milliseconds."),
            KI18n.i18nc("@info help page of the Voice Compressor block", "The tile shows what the compressor and the gate take off together, what the block is doing, and its settings. Both sides of a stereo sound get one gain, so the picture does not move.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label the Voice Compressor's choice of voice types", "Voice")
        covers: [
            KI18n.i18nc("@item:inlistbox a voice type for the Voice Compressor", "Soft voice"),
            KI18n.i18nc("@item:inlistbox a voice type for the Voice Compressor", "Speech"),
            KI18n.i18nc("@item:inlistbox a voice type for the Voice Compressor", "Strong voice"),
            KI18n.i18nc("@item:inlistbox a voice type for the Voice Compressor", "Singing")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Voice Compressor block", "A place to start from. Each choice sets all three controls, and moving one takes the choice away.")
        ]
        points: [
            KI18n.i18nc("@info help page of the Voice Compressor block, one voice type", "Soft voice: more compression, for someone who speaks quietly."),
            KI18n.i18nc("@info help page of the Voice Compressor block, one voice type", "Speech: for an ordinary voice at a microphone."),
            KI18n.i18nc("@info help page of the Voice Compressor block, one voice type", "Strong voice: less compression, for someone who speaks loudly and close."),
            KI18n.i18nc("@info help page of the Voice Compressor block, one voice type", "Singing: no gate, so held notes and their ends are left alone.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@title:group the Voice Compressor's graph", "Level in and out")
        paragraphs: [
            KI18n.i18nc("@info help page of the Voice Compressor block", "What comes out for a steady level going in, with a dashed line where nothing changes and a line at the ceiling. While sound plays, a dot shows where it is now. The dot leaves the curve for a moment after the level changes, which is the block on its way to a new gain.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider the Voice Compressor's one compression control", "Compression")
        paragraphs: [
            // i18n: dB stands for decibels.
            KI18n.i18nc("@info help page of the Voice Compressor block", "How much to compress, from 0 to -36 decibels (dB). Lower is more: it starts sooner, holds back harder, from a ratio of 2:1 to 6:1, and brings the rest up further. The line under it says the ratio and the level made up.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider the level under which the Voice Compressor lowers the sound", "Gate")
        paragraphs: [
            KI18n.i18nc("@info help page of the Voice Compressor block", "The level under which the sound is lowered, from Off to 0 dB. Higher lowers more. It eases in, each decibel under the gate comes out two under, and it never takes off more than 24 dB, so a room falls away without ever cutting to silence. The bar under it shows the level coming in, with a mark where the Gate stands.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider the highest level the Voice Compressor lets out", "Ceiling")
        paragraphs: [
            // i18n: dBFS stands for decibels below full scale.
            KI18n.i18nc("@info help page of the Voice Compressor block", "The highest level that may leave the block, from -36 to 0 decibels below full scale (dBFS). The whole sound moves with it, so it does the work of a level control too."),
            KI18n.i18nc("@info help page of the Voice Compressor block", "In the top 12 dB it also drives the limiter, more the higher it goes, which makes the sound denser and the level steadier. Below that the limiter takes only the peaks that get past the compressor.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check keeps the gap between two of the Voice Compressor's controls", "Link Compression and Ceiling")
        paragraphs: [
            KI18n.i18nc("@info help page of the Voice Compressor block", "Moves both by the same amount, to hear what the compressor does with little change of level. Find a gap that sounds level to you, then move them. It is a way of listening: it is not saved with the block.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label the Voice Compressor's total meter", "Taken off by compression and gate")
        covers: [
            KI18n.i18nc("@action:button", "Reset the most taken off")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Voice Compressor block", "The two added together, as the tile shows it. The line under the bars holds the most taken off, and Reset forgets it. The limiter has a bar of its own.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Voice Compressor block, when to use it", "A voice at a microphone, for a call, a stream or a recording, when you want it even with little setting up."),
            KI18n.i18nc("@info help page of the Voice Compressor block, when to use it", "For one number and no more, use Simple Compressor. To set every number, use Dynamics Processor.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Voice Compressor block, a tip", "Set Compression first, then Ceiling, then Gate: the other two change the level the Ceiling works on."),
            KI18n.i18nc("@info help page of the Voice Compressor block, a tip", "If the voice sounds rough, the limiter is working too hard. Lower the Ceiling, or the sound coming in."),
            KI18n.i18nc("@info help page of the Voice Compressor block, a tip", "If word endings are cut, lower the Gate."),
            KI18n.i18nc("@info help page of the Voice Compressor block, a tip", "For a hard limit downstream of everything, use a Limiter or the output limit: this ceiling can be raised with a drag.")
        ]
    }
}
