// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Pitch Shift block's help page (docs/dev/block-help.md). The facts
// are the block's (engine/blocks/pitchshift.h and .cpp: the ranges, the
// latency it works out and reports, the crossover to a new pitch; the
// latency is measured by tests/unit/blocks/test_pitchshift.cpp) and the
// panel's words (PitchShiftPanel, PitchShiftWords).
HelpPage {
    typeId: "effect.pitchshift"
    summary: KI18n.i18nc("@info help page of the Pitch Shift block", "Makes a sound higher or lower without making it faster or slower.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Pitch Shift block", "Played faster, a recording gets higher; played slower, it gets lower. This block changes only the pitch: words and music keep their speed."),
            KI18n.i18nc("@info help page of the Pitch Shift block", "It cuts the sound into short overlapping pieces, a few hundredths of a second each, and fits them back together at the new pitch. It is set up for speech. Small shifts sound the most natural; the further you move the pitch, the more the joins can be heard."),
            KI18n.i18nc("@info help page of the Pitch Shift block", "To do that work, it holds the sound back by about six hundredths of a second, whatever the setting."),
            KI18n.i18nc("@info help page of the Pitch Shift block", "A new setting is heard a moment later: the block starts the new pitch beside the old one and crosses over smoothly, so a change never clicks.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider how far the pitch is moved, in whole semitones", "Semitones")
        paragraphs: [
            // i18n: st stands for semitones, as the panel writes it.
            KI18n.i18nc("@info help page of the Pitch Shift block", "Moves the pitch in whole steps. A semitone is the step from one key of a piano to the next, white or black; twelve of them make an octave. The number is written in st, for semitones."),
            KI18n.i18nc("@info help page of the Pitch Shift block", "The slider goes from −12, an octave down, to +12, an octave up. A new block starts at 0, which leaves the pitch alone.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider fine tuning of the pitch, in hundredths of a semitone", "Cents")
        paragraphs: [
            // i18n: ct stands for cents, as the panel writes it.
            KI18n.i18nc("@info help page of the Pitch Shift block", "Fine tuning on top of the semitones. A cent is a hundredth of a semitone, written ct. The slider goes from −100 to +100 in whole cents, and starts at 0."),
            KI18n.i18nc("@info help page of the Pitch Shift block", "The line under the sliders adds the two up: with +3 semitones and +25 cents it says the sound is moved by +3.25 st in all.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Pitch Shift block", "A recording was made on a machine that ran slightly fast or slow, and is out of tune with everything else."),
            KI18n.i18nc("@info help page of the Pitch Shift block", "You want to sing or play along with a song in a key that suits you better."),
            KI18n.i18nc("@info help page of the Pitch Shift block", "You want a voice to sound different, deeper or brighter, for fun or to make it harder to recognise.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Pitch Shift block", "For a voice, a few semitones either way already change it a lot. Try small steps first."),
            KI18n.i18nc("@info help page of the Pitch Shift block", "To tune one recording to another, set Semitones first, then move Cents slowly until the two sound in tune together."),
            KI18n.i18nc("@info help page of the Pitch Shift block", "Because the sound comes out a little late, do not use the block on your own voice while you listen to yourself live: the delay is easy to notice.")
        ]
    }
}
