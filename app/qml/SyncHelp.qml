// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Sync block's help page (docs/dev/block-help.md). The facts are the
// block's (engine/blocks/sync.h: up to two seconds, to the sample, a change
// faded over 20 ms, a delay meant and not lined up against) and the
// panel's (SyncPanel and SyncScale: the box's hundredths and whole steps,
// the slider's square law, the samples line).
HelpPage {
    typeId: "advanced.sync"
    summary: KI18n.i18nc("@info help page of the Sync block", "Holds the sound back by exactly the time you set, up to two seconds, to line it up with a picture or with another sound.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Sync block", "Sound and picture often reach you a little apart: a video call's voice before the lips move, a camera's sound after the picture, one microphone's sound a moment behind another's. The block delays the lane it is in, so the early one can wait for the late one."),
            KI18n.i18nc("@info help page of the Sync block", "The sound comes out exactly as it went in, only later. Changing the delay while sound plays fades from the old delay to the new one in a fiftieth of a second, so there is no click and no wobble in pitch."),
            KI18n.i18nc("@info help page of the Sync block", "This delay is the point of the block, so Reverblabr does not hold the other lanes back to match it, as it does for blocks that are late only because of how they work. Switch the block off and the delay is gone.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider how long a Sync block holds the sound back", "Delay")
        covers: [
            KI18n.i18nc("@label:spinbox", "Delay in milliseconds")
        ]
        paragraphs: [
            // i18n: ms stands for milliseconds, thousandths of a second.
            KI18n.i18nc("@info help page of the Sync block", "How long the sound is held back, in milliseconds (ms), thousandths of a second: from 0 to 2000 ms. A new block starts at 0, so it changes nothing until you set it."),
            KI18n.i18nc("@info help page of the Sync block", "Type the exact number in the box, to a hundredth of a millisecond; its arrows go up or down a whole millisecond at a time. The slider under it gets you near quickly. It gives the short delays more room: a quarter of the way along is 120 ms, half way 500 ms."),
            KI18n.i18nc("@info help page of the Sync block", "While the session runs, the line under the slider says the delay in samples, the single steps digital sound is made of, at the session's rate. That is the number to compare with a video editor's.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Sync block", "In a stream or a recording, your voice is heard before your lips move on the camera."),
            KI18n.i18nc("@info help page of the Sync block", "Two microphones pick up the same sound at slightly different times, and together they sound hollow."),
            KI18n.i18nc("@info help page of the Sync block", "Wireless headphones or a TV are late, and another sound has to wait for them.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Sync block", "Put the block in the lane that is early. It can only make a sound later, never earlier."),
            KI18n.i18nc("@info help page of the Sync block", "To line up a picture, watch something with a clear sound, such as a clap or a word that starts with p or b, and change the delay a few milliseconds at a time until sight and sound meet."),
            KI18n.i18nc("@info help page of the Sync block", "Sound travels about a third of a metre in a millisecond. Two microphones a metre apart are about 3 ms apart.")
        ]
    }
}
