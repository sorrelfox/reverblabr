// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Frequency Raising block's help page (docs/dev/block-help.md). The
// facts are the block's (engine/blocks/frequencyraising.h and .cpp: the
// ranges, the first move, the two frames and the 250 Hz line;
// frequencymoveblock.cpp: the settings, their ranges and defaults, and
// that a move switched off still counts), the mover's
// (engine/dsp/frequencymove.h and .cpp: how each method carries a sound,
// the sixteenth harmonic, Quieter's 12 dB, when "On low sounds" acts),
// the gap when the delay changes (docs/adr/0022) and the panel's
// (FrequencyMovePanel). docs/hearing.md is the user documentation it
// agrees with.
HelpPage {
    typeId: "hearing.frequencyraising"
    summary: KI18n.i18nc("@info help page of the Frequency Raising block", "Moves low or middle sounds that one ear hears poorly up to a higher range where it hears better. Parts of it are experimental.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Raising block", "Some ears hear the low range faintly or not at all, while higher sounds are fine. This block takes the sound of a low or middle range, the range it comes from, and puts it into a higher range the ear hears, the range it goes into. It works like Frequency Lowering, the other way round."),
            KI18n.i18nc("@info help page of the Frequency Raising block", "Hertz (Hz) count how high a sound is; a thousand hertz is a kilohertz (kHz). A deep voice has its lowest tone round 100 Hz."),
            KI18n.i18nc("@info help page of the Frequency Raising block", "Each way of moving sound is called a move. The block can do up to four moves at once, each from a range of its own. A new block has one move ready: sound from 250 to 500 Hz copied two octaves up into 1 to 2 kHz, with the original kept. An octave up is twice as high."),
            KI18n.i18nc("@info help page of the Frequency Raising block", "Every move works on the sound as it comes in, so moves never move each other's sound, and their order does not matter. Where two moves put sound into the same range, it adds up."),
            KI18n.i18nc("@info help page of the Frequency Raising block", "It works on one ear only. The other side passes through unchanged, held back by exactly as long as the block takes, so both ears stay in step. Ear, at the top of the settings, says which ear it is for, and the L (blue) or R (red) on the block's tile shows it."),
            KI18n.i18nc("@info help page of the Frequency Raising block", "Low sounds take longer to tell apart, so this block holds the sound back longer than Frequency Lowering: about 43 thousandths of a second (ms) at the usual rate of 48 kHz while every move starts at 250 Hz or above, and about 85 ms once a move starts lower. The other ear waits as long. Even 43 ms can be noticed with video, where lips move before you hear the voice, so it is better kept for listening.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label which ear a hearing block works on", "Ear")
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Raising block", "Left or Right: the ear the block works on. The other side passes through unchanged. The block's tile shows an L (blue) or an R (red) to match."),
            KI18n.i18nc("@info help page of the Frequency Raising block", "A new block starts on the left ear. When the session already has more blocks of this kind for the left ear than for the right, a new one starts on the right, so the second block you add is ready for the other ear."),
            KI18n.i18nc("@info help page of the Frequency Raising block", "Changing it while the session runs fades across to the other side, without a click. One undo puts it back. Loading a preset or putting the block back to its default settings leaves it as it is, so a preset made for one ear can be tried on the other.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:radio show the moves as arrows along a frequency axis", "Arrows")
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Raising block", "Shows the moves along a line of pitches, low on the left and high on the right. For each move, the shaded range is where the sound comes from and the outlined range is where it goes, with an arrow between them."),
            KI18n.i18nc("@info help page of the Frequency Raising block", "Click a range to pick its move. Drag the ends of the picked move's two ranges to change them."),
            KI18n.i18nc("@info help page of the Frequency Raising block", "While the session runs, the sound before the block and after it is drawn behind, for this block's ear, so you can see it move.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:radio show the moves as a map of frequency in against frequency out", "Map")
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Raising block", "Shows where each pitch ends up: the pitch going in runs across, and the pitch coming out runs up. The straight diagonal is sound left where it was. A squeezed move bends away from it, a copied move adds a line beside it, and Add harmonics adds a line for each harmonic.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check hear only the sound the moves add", "Moved sound only")
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Raising block", "Plays only the sound the moves add, without the rest. Turn it on while you set a move's Level, to hear exactly what it adds, then turn it off again."),
            KI18n.i18nc("@info help page of the Frequency Raising block", "It is a way of listening, not part of the setting: presets leave it alone.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label the moves of a Frequency Lowering or Raising block", "Moves")
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Raising block", "A button for each move in use, Move 1 to Move 4. Click one to pick that move: the settings under the buttons are the picked move's. While the session runs, a move that is moving sound shows a small play sign.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@action:button add a move to a Frequency Lowering or Raising block", "Add a move")
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Raising block", "The plus button adds a move, with ranges of its own to start from, and picks it. Drag its ranges to where you need them. It cannot be pressed once four moves are in use.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@action:button take a Frequency Lowering or Raising block's picked move away", "Remove this move")
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Raising block", "The button at the end of the row takes the picked move away. A move taken away no longer counts towards the delay.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:checkbox whether the chosen move acts", "Move on")
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Raising block", "The switch beside On. Off, the picked move does nothing, but keeps its settings. Switch it off and on to hear what the move does."),
            KI18n.i18nc("@info help page of the Frequency Raising block", "A move that is switched off still counts towards the delay, so comparing never moves the ear in time.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how a Frequency Lowering or Raising move carries sound", "Method")
        covers: [
            KI18n.i18nc("@option:radio squeeze the source range into the destination", "Squeeze"),
            KI18n.i18nc("@option:radio copy the source range up into the destination", "Copy up"),
            KI18n.i18nc("@option:radio give each sound of the source range its harmonics in the destination, keeping its pitch", "Add harmonics")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Raising block", "How the move carries the sound up:")
        ]
        points: [
            KI18n.i18nc("@info help page of the Frequency Raising block, one choice of Method", "Squeeze fits the whole range the sound comes from into the range it goes into: its bottom lands on the bottom, its top on the top. Use it where the ear hears nothing at all in the low range, and empty the range the sound came from."),
            KI18n.i18nc("@info help page of the Frequency Raising block, one choice of Method", "Copy up moves every sound up by the same step, so the bottom of the range it comes from lands on the bottom of the range it goes into, and the tones of a voice keep their spacing. Sound that would land above the top of that range is left out. A new block's first move copies up."),
            KI18n.i18nc("@info help page of the Frequency Raising block, one choice of Method", "Add harmonics is experimental. Moving a voice up raises its pitch. Instead, this leaves each low sound where it is and adds its harmonics, the tones at twice its pitch, three times and so on up to sixteen times, wherever they fall in the range the sound goes into. The ear takes the pitch from the harmonics, so the voice still sounds like itself. Not much is known about how well this helps, so trust your ears.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider", "Range the sound comes from")
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Raising block", "The low range the picked move takes sound from, shown beside From. Drag the two handles of the slider, or the ends of the shaded range on the Arrows view. The ends can be anywhere from 60 Hz to 16 kHz."),
            KI18n.i18nc("@info help page of the Frequency Raising block", "Set it to the range your ear hears poorly. Neither range can be narrower than a sixth of an octave, and the range the sound comes from always stays below the range it goes into."),
            KI18n.i18nc("@info help page of the Frequency Raising block", "When a move starts below 250 Hz, the delay grows from about 43 ms to about 85 ms. If that happens while the session runs, the sound fades out, stays quiet for about as long as the new delay, and fades back in: a short gap, not a click.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider", "Range the sound goes into")
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Raising block", "The higher range the picked move puts sound into, shown beside Into. Set it to a range your ear hears well.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider", "Level of the moved sound")
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Raising block", "How loud the moved sound is, against how loud it was where it came from: from −24 dB to +12 dB, in steps of half a decibel. A new move starts at 0 dB, the same level."),
            KI18n.i18nc("@info help page of the Frequency Raising block", "Moved sound only helps you hear it on its own.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label what becomes of the range a Frequency Lowering or Raising move takes sound from", "Leave the source")
        covers: [
            KI18n.i18nc("@option:radio leave the source range as it was", "As it was"),
            KI18n.i18nc("@option:radio make the source range quieter", "Quieter"),
            KI18n.i18nc("@option:radio take the sound out of the source range", "Empty")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Raising block", "What becomes of the low range once its sound has been carried up:")
        ]
        points: [
            KI18n.i18nc("@info help page of the Frequency Raising block, one choice of Leave the source", "As it was keeps it unchanged, for an ear that still hears some of it. A new block's first move keeps it."),
            KI18n.i18nc("@info help page of the Frequency Raising block, one choice of Leave the source", "Quieter turns it down by 12 dB."),
            KI18n.i18nc("@info help page of the Frequency Raising block, one choice of Leave the source", "Empty takes its sound out. Use it where the ear hears nothing there anyway.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label when a Frequency Lowering or Raising move acts", "Acts")
        covers: [
            KI18n.i18nc("@option:radio a Frequency Lowering or Raising move acts all the time", "Always"),
            KI18n.i18nc("@option:radio a Frequency Raising move acts only while its low range is louder than what is above it, as with a voice or a hum", "On low sounds")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Raising block", "When the move does its work:")
        ]
        points: [
            KI18n.i18nc("@info help page of the Frequency Raising block, one choice of Acts", "Always moves everything in the range, all the time. A new move acts always."),
            KI18n.i18nc("@info help page of the Frequency Raising block, one choice of Acts", "On low sounds moves sound only while the low range is louder than everything above it, as it is with a deep voice or a hum. Sound that is mostly higher passes as it came.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how late a Frequency Lowering or Raising block's sound is", "Delay")
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Raising block", "Not a setting: how long the block holds the sound back with its moves as they are, in thousandths of a second. It grows as soon as a move's range the sound comes from reaches below 250 Hz, even before the session runs, so you know what you will get.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Frequency Raising block", "One ear hears deep voices and low sounds faintly, or not at all, while higher sounds are clear."),
            KI18n.i18nc("@info help page of the Frequency Raising block", "Your hearing has a dip in the middle: use this block together with Frequency Lowering."),
            KI18n.i18nc("@info help page of the Frequency Raising block", "You listen to speech or music, and a small delay does not matter.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Frequency Raising block", "Put it before Hearing Compensation, so the moved sound gets the lift of the range it lands in."),
            KI18n.i18nc("@info help page of the Frequency Raising block", "Keep the ranges at 250 Hz or above if you can: the delay stays at about 43 ms."),
            KI18n.i18nc("@info help page of the Frequency Raising block", "For a dip from 1 to 4 kHz, for example: Frequency Lowering squeezes 1 to 2 kHz into 500 Hz to 1 kHz, and this block squeezes 2 to 4 kHz into 4 to 8 kHz, both for the same ear. Split like this, their order does not matter."),
            KI18n.i18nc("@info help page of the Frequency Raising block", "If the ranges of the two blocks overlap, the second block moves what the first has already moved. Put first the move you want the other to work on."),
            KI18n.i18nc("@info help page of the Frequency Raising block", "For the other ear, add a second block of this kind: it starts on the right ear. Set it up for that ear.")
        ]
    }
}
