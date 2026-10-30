// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Frequency Lowering block's help page (docs/dev/block-help.md). The
// facts are the block's (engine/blocks/frequencylowering.cpp: the ranges
// and the first move; frequencymoveblock.cpp: the settings, their ranges
// and defaults), the mover's (engine/dsp/frequencymove.h and .cpp: how
// each method carries a sound, Quieter's 12 dB, when "On s, sh, f" acts,
// the 20 ms frame) and the panel's (FrequencyMovePanel: the views, the
// narrowest range, which side the destination stays on). docs/hearing.md
// is the user documentation it agrees with.
HelpPage {
    typeId: "hearing.frequencylowering"
    summary: KI18n.i18nc("@info help page of the Frequency Lowering block", "Moves high sounds that one ear cannot hear down to a lower range where it can. It brings back the hiss of s, sh and f, which tells \"sit\" from \"fit\" and \"cat\" from \"cats\".")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Lowering block", "Some ears hear nothing in a high range, however loud it is made. Turning that range up does not help. But an important part of speech lives up there: the hissing sounds that tell many words apart."),
            KI18n.i18nc("@info help page of the Frequency Lowering block", "This block takes the sound of a high range, the range it comes from, and puts it into a lower range the ear still hears, the range it goes into. The moved sound is not the same as the original, but it lets the ear notice that the hiss was there, which makes words easier to tell apart."),
            KI18n.i18nc("@info help page of the Frequency Lowering block", "Hertz (Hz) count how high a sound is; a thousand hertz is a kilohertz (kHz). The hiss of an s sits above 4 kHz. Ordinary voices sit much lower."),
            KI18n.i18nc("@info help page of the Frequency Lowering block", "Each way of moving sound is called a move. The block can do up to four moves at once, each from a range of its own. A new block has one move ready: sound from 4 to 8 kHz squeezed into 2.5 to 4 kHz, with the high range emptied."),
            KI18n.i18nc("@info help page of the Frequency Lowering block", "Every move works on the sound as it comes in, so moves never move each other's sound, and their order does not matter. Where two moves put sound into the same range, it adds up."),
            KI18n.i18nc("@info help page of the Frequency Lowering block", "It works on one ear only. The other side passes through unchanged, held back by exactly as long as the block takes, so both ears stay in step. Ear, at the top of the settings, says which ear it is for, and the L (blue) or R (red) on the block's tile shows it."),
            KI18n.i18nc("@info help page of the Frequency Lowering block", "The block holds the sound back by about 21 thousandths of a second (ms) at the usual rate of 48 kHz, and the other ear waits as long. Delay, at the bottom of the settings, shows the exact figure.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label which ear a hearing block works on", "Ear")
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Lowering block", "Left or Right: the ear the block works on. The other side passes through unchanged. The block's tile shows an L (blue) or an R (red) to match."),
            KI18n.i18nc("@info help page of the Frequency Lowering block", "A new block starts on the left ear. When the session already has more blocks of this kind for the left ear than for the right, a new one starts on the right, so the second block you add is ready for the other ear."),
            KI18n.i18nc("@info help page of the Frequency Lowering block", "Changing it while the session runs fades across to the other side, without a click. One undo puts it back. Loading a preset or putting the block back to its default settings leaves it as it is, so a preset made for one ear can be tried on the other.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:radio show the moves as arrows along a frequency axis", "Arrows")
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Lowering block", "Shows the moves along a line of pitches, low on the left and high on the right. For each move, the shaded range is where the sound comes from and the outlined range is where it goes, with an arrow between them."),
            KI18n.i18nc("@info help page of the Frequency Lowering block", "Click a range to pick its move. Drag the ends of the picked move's two ranges to change them."),
            KI18n.i18nc("@info help page of the Frequency Lowering block", "While the session runs, the sound before the block and after it is drawn behind, for this block's ear, so you can see it move.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:radio show the moves as a map of frequency in against frequency out", "Map")
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Lowering block", "Shows where each pitch ends up: the pitch going in runs across, and the pitch coming out runs up. The straight diagonal is sound left where it was. A squeezed move bends away from it, and a copied move adds a line beside it.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check hear only the sound the moves add", "Moved sound only")
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Lowering block", "Plays only the sound the moves add, without the rest. Turn it on while you set a move's Level, to hear exactly what it adds, then turn it off again."),
            KI18n.i18nc("@info help page of the Frequency Lowering block", "It is a way of listening, not part of the setting: presets leave it alone.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label the moves of a Frequency Lowering or Raising block", "Moves")
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Lowering block", "A button for each move in use, Move 1 to Move 4. Click one to pick that move: the settings under the buttons are the picked move's. While the session runs, a move that is moving sound shows a small play sign.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@action:button add a move to a Frequency Lowering or Raising block", "Add a move")
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Lowering block", "The plus button adds a move, with ranges of its own to start from, and picks it. Drag its ranges to where you need them. It cannot be pressed once four moves are in use.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@action:button take a Frequency Lowering or Raising block's picked move away", "Remove this move")
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Lowering block", "The button at the end of the row takes the picked move away. To stop a move only for a moment, switch it off instead.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:checkbox whether the chosen move acts", "Move on")
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Lowering block", "The switch beside On. Off, the picked move does nothing, but keeps its settings. Switch it off and on to hear what the move does.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how a Frequency Lowering or Raising move carries sound", "Method")
        covers: [
            KI18n.i18nc("@option:radio squeeze the source range into the destination", "Squeeze"),
            KI18n.i18nc("@option:radio copy the source range down into the destination", "Copy down")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Lowering block", "How the move carries the sound down:")
        ]
        points: [
            KI18n.i18nc("@info help page of the Frequency Lowering block, one choice of Method", "Squeeze fits the whole range the sound comes from into the smaller range it goes into: its bottom lands on the bottom, its top on the top, and everything in between is pressed closer together. Use it where the ear hears nothing at all, and empty the range the sound came from. A new block's first move squeezes."),
            KI18n.i18nc("@info help page of the Frequency Lowering block, one choice of Method", "Copy down moves every sound down by the same step, so the bottom of the range it comes from lands on the bottom of the range it goes into, and the tones of a voice keep their spacing. Sound that would land above the top of that range is left out. Use it where the ear still hears a little of the high range, and keep the original.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider", "Range the sound comes from")
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Lowering block", "The high range the picked move takes sound from, shown beside From. Drag the two handles of the slider, or the ends of the shaded range on the Arrows view. The ends can be anywhere from 125 Hz to 16 kHz."),
            KI18n.i18nc("@info help page of the Frequency Lowering block", "Set it to the range your ear does not hear. Neither range can be narrower than a sixth of an octave, and the range the sound comes from always stays above the range it goes into.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider", "Range the sound goes into")
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Lowering block", "The lower range the picked move puts sound into, shown beside Into. Set it to a range your ear still hears well, just below the range the sound comes from.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider", "Level of the moved sound")
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Lowering block", "How loud the moved sound is, against how loud it was where it came from: from −24 dB to +12 dB, in steps of half a decibel. A new move starts at 0 dB, the same level."),
            KI18n.i18nc("@info help page of the Frequency Lowering block", "Set it so an s is clear but not sharp. Moved sound only helps you hear it on its own.")
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
            KI18n.i18nc("@info help page of the Frequency Lowering block", "What becomes of the high range once its sound has been copied lower:")
        ]
        points: [
            KI18n.i18nc("@info help page of the Frequency Lowering block, one choice of Leave the source", "As it was keeps it unchanged, for an ear that still hears some of it."),
            KI18n.i18nc("@info help page of the Frequency Lowering block, one choice of Leave the source", "Quieter turns it down by 12 dB."),
            KI18n.i18nc("@info help page of the Frequency Lowering block, one choice of Leave the source", "Empty takes its sound out. Use it where the ear hears nothing there anyway. A new block's first move empties it.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label when a Frequency Lowering or Raising move acts", "Acts")
        covers: [
            KI18n.i18nc("@option:radio a Frequency Lowering or Raising move acts all the time", "Always"),
            KI18n.i18nc("@option:radio a Frequency Lowering move acts only on hissing sounds such as s and f", "On s, sh, f")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Lowering block", "When the move does its work:")
        ]
        points: [
            KI18n.i18nc("@info help page of the Frequency Lowering block, one choice of Acts", "Always moves everything in the range, all the time. A new move acts always."),
            KI18n.i18nc("@info help page of the Frequency Lowering block, one choice of Acts", "On s, sh, f moves sound only while the high range is louder than everything below it, as it is in a hissing sound. Vowels and most music pass as they came.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how late a Frequency Lowering or Raising block's sound is", "Delay")
        paragraphs: [
            KI18n.i18nc("@info help page of the Frequency Lowering block", "Not a setting: how long the block holds the sound back, in thousandths of a second. It depends only on the session's rate, about 21 ms at 48 kHz. The other ear waits as long, so the two stay in step.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Frequency Lowering block", "You mix up words that differ only in an s, an f or a sh, such as \"sit\" and \"fit\"."),
            KI18n.i18nc("@info help page of the Frequency Lowering block", "You miss plurals: \"cat\" and \"cats\" sound the same to you."),
            KI18n.i18nc("@info help page of the Frequency Lowering block", "An audiogram shows no response at high pitches, or the Hearing Compensation fit marked high bands where more lift is unlikely to help.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Frequency Lowering block", "Put it before Hearing Compensation, so the moved sound gets the lift of the range it lands in."),
            KI18n.i18nc("@info help page of the Frequency Lowering block", "Start with the first move as it is, listen to speech for a while, then change one thing at a time."),
            KI18n.i18nc("@info help page of the Frequency Lowering block", "If voices sound harsh or lisped, try a lower Level, or choose On s, sh, f."),
            KI18n.i18nc("@info help page of the Frequency Lowering block", "For the other ear, add a second block of this kind: it starts on the right ear. Set it up for that ear."),
            KI18n.i18nc("@info help page of the Frequency Lowering block", "For a dip in the middle of your hearing, use this block for the lower half of the dip and Frequency Raising for the upper half, both for the same ear.")
        ]
    }
}
