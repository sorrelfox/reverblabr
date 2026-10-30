// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Declick block's help page (docs/dev/block-help.md). The facts are the
// block's (engine/blocks/declick.h: the threshold's range, the 20 ms frames
// and the three quarters of one it holds the sound back, the longest click
// of 3 ms, the dropouts) and the panel's (DeclickPanel, DeclickWords).
HelpPage {
    typeId: "advanced.declick"
    summary: KI18n.i18nc("@info help page of the Declick block", "Takes clicks and pops out of the sound, and fills each gap with what most likely was there. Use it on a record's crackle, a crackling cable or a stream that drops out for an instant.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Declick block", "Music and speech change smoothly from one moment to the next, so the block can guess, at every moment, what comes next from what came before. A click cannot be guessed: it appears from nowhere. Where the sound suddenly differs from the guess by much more than usual, the block takes it for a click."),
            KI18n.i18nc("@info help page of the Declick block", "It also finds a dropout: a few moments of complete silence in the middle of sound, as when a digital stream loses a little of itself."),
            KI18n.i18nc("@info help page of the Declick block", "Each click is cut out and the gap filled in from the sound on both sides of it, so it fits in with what is around it. A click up to three thousandths of a second long is mended this way. A longer noise, such as a scratch that rumbles, is not a click to this block."),
            KI18n.i18nc("@info help page of the Declick block", "The left and the right side are mended each on its own. Sound with no clicks in it passes through unchanged."),
            KI18n.i18nc("@info help page of the Declick block", "To have sound on both sides of every click, the block works on a fiftieth of a second at a time, and holds the sound back by about a sixtieth of a second. Reverblabr keeps the other lanes in step with that delay."),
            KI18n.i18nc("@info help page of the Declick block", "While the session runs, the settings show how many clicks the block has taken out since it started.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider", "Threshold")
        paragraphs: [
            // i18n: dB stands for decibels.
            KI18n.i18nc("@info help page of the Declick block", "How much a sound has to stand out from the guess before it counts as a click, in decibels (dB), from 0 to 20 dB. A new block starts at 3 dB."),
            KI18n.i18nc("@info help page of the Declick block", "Lower is more sensitive: the block finds fainter clicks, but now and then it also takes the sharp start of a note for one, and changes it a little. Higher is less sensitive: only the loudest clicks are taken out."),
            KI18n.i18nc("@info help page of the Declick block", "A change takes effect at once, while the sound plays.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Declick block", "You are recording or listening to a vinyl record, and its crackle bothers you."),
            KI18n.i18nc("@info help page of the Declick block", "A loose cable or a dirty connector adds pops to a microphone."),
            KI18n.i18nc("@info help page of the Declick block", "A stream or a call drops out for a moment now and then, and each dropout ticks.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Declick block", "Start at 3 dB. If clicks are still there, lower the threshold a step at a time until they go."),
            KI18n.i18nc("@info help page of the Declick block", "If sharp sounds such as a drum or a plucked string lose a little of their edge, raise the threshold again."),
            KI18n.i18nc("@info help page of the Declick block", "Watch the count of clicks taken out on a quiet passage. If it keeps climbing where you hear no clicks, the threshold is too low."),
            KI18n.i18nc("@info help page of the Declick block", "Put the block early in the lane, before the blocks that change the sound, so that it hears the clicks as they arrived.")
        ]
    }
}
