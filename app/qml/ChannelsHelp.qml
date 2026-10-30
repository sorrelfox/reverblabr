// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Channels block's help page (docs/dev/block-help.md). The facts are
// the block's (engine/blocks/channels.h and .cpp: the actions, their order,
// the averages and the crossfade) and the panel's words (ChannelsPanel,
// ChannelsWords: the actions' names and the routing on the tile).
HelpPage {
    typeId: "effect.channels"
    summary: KI18n.i18nc("@info help page of the Channels block", "Rearranges the two sides of a stereo lane: swaps them, folds them into one, copies one side to both, or silences or flips one of them.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Channels block", "Stereo sound has two sides, left and right, one for each speaker or each ear of your headphones. This block decides what each side plays: its own sound, the other side's, a mix of both, or nothing."),
            KI18n.i18nc("@info help page of the Channels block", "You choose one action at a time. The block's tile shows the action, and under it where each side now comes from: L→R means the left side's sound is played on the right."),
            KI18n.i18nc("@info help page of the Channels block", "A change of action fades across in a hundredth of a second, so switching while sound plays makes no click.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:listbox what the Channels block does to the two sides", "Action")
        paragraphs: [
            KI18n.i18nc("@info help page of the Channels block", "What the block does to the two sides. Click the list to choose another action; the line under it says what the chosen one does. A new block starts at No change.")
        ]
        points: [
            KI18n.i18nc("@info help page of the Channels block, one choice of Action", "No change: both sides pass through as they arrive."),
            KI18n.i18nc("@info help page of the Channels block, one choice of Action", "Swap sides: the left side plays on the right and the right on the left."),
            KI18n.i18nc("@info help page of the Channels block, one choice of Action", "Mono: both sides play the average of the two. A sound in the middle keeps its level, and nothing gets louder than the louder side."),
            KI18n.i18nc("@info help page of the Channels block, the choices of Action", "Left to both, or Right to both: one side plays on both, and the other side's sound is left out. For a source that arrives on one side only."),
            KI18n.i18nc("@info help page of the Channels block, the choices of Action", "Mute left, or Mute right: that side is silent, and the other plays as it is."),
            KI18n.i18nc("@info help page of the Channels block, the choices of Action", "Invert left, or Invert right: that side is flipped upside down. It sounds the same on its own, but it puts right a microphone or a cable wired the wrong way round, whose side cancels the other out when the two meet."),
            KI18n.i18nc("@info help page of the Channels block, one choice of Action", "Difference: both sides play what differs between them. Whatever sits in the middle, often a voice, drops out, and what is spread to the sides stays.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Channels block", "A microphone or an instrument arrives on one side only, and you want to hear it on both."),
            KI18n.i18nc("@info help page of the Channels block", "Left and right are the wrong way round."),
            KI18n.i18nc("@info help page of the Channels block", "You hear with one ear only, and want all of the sound on both sides."),
            KI18n.i18nc("@info help page of the Channels block", "You want to check how something sounds on a single speaker, such as a phone's."),
            KI18n.i18nc("@info help page of the Channels block", "A stereo mix sounds hollow and thin in mono, as if part of it were cancelling out.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Channels block", "If one side of a stereo source is only noise or silence, choose Left to both or Right to both rather than Mono. Mono would mix the noise in, or halve the level."),
            KI18n.i18nc("@info help page of the Channels block", "If a stereo source goes thin and quiet in Mono, one of its sides is probably flipped. Put a second Channels block, set to Invert left, before the one set to Mono, and listen whether the sound comes back."),
            KI18n.i18nc("@info help page of the Channels block", "Difference can take a singer out of many songs, for a karaoke track, but anything else in the middle goes with it, often the bass and the drums.")
        ]
    }
}
