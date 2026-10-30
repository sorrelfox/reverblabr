// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Pan block's help page (docs/dev/block-help.md). The facts are the
// block's (engine/blocks/pan.h and .cpp: the fold to one signal, constant
// power, the glide) and the panel's words (StereoPositionPanel,
// StereoPositionWords).
HelpPage {
    typeId: "effect.pan"
    summary: KI18n.i18nc("@info help page of the Pan block", "Puts a sound at one place between the left and the right speaker, at the same loudness wherever it sits.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Pan block", "The block first makes one sound of the two sides, by averaging them. A sound that was in the middle keeps its level; a stereo recording loses its own spread of left and right."),
            KI18n.i18nc("@info help page of the Pan block", "It then sends that one sound to the two speakers in the proportion you set. In the middle both carry it equally; all the way to one side, only that speaker plays it."),
            KI18n.i18nc("@info help page of the Pan block", "The sound keeps the same loudness wherever you put it: in the middle each side plays it a little quieter, so the two together are as loud as one side alone at the end."),
            KI18n.i18nc("@info help page of the Pan block", "A change glides in over a fiftieth of a second, so dragging the slider while the sound plays makes no click. The sound is not delayed.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider", "Pan")
        paragraphs: [
            KI18n.i18nc("@info help page of the Pan block", "Where the sound sits. L under the left end of the slider is the left side, R under the right end is the right."),
            // i18n: keep the examples as the panel writes them: Centre, L 30, R 100.
            KI18n.i18nc("@info help page of the Pan block", "The number above the slider says where it is: Centre in the middle, then L or R and how far, up to 100 at either end. L 30 is a little to the left; R 100 is the right speaker alone."),
            KI18n.i18nc("@info help page of the Pan block", "A new block starts at Centre.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Pan block", "Two people talk in the same recording or call, and you want one on each side so they are easier to tell apart."),
            KI18n.i18nc("@info help page of the Pan block", "A mono source, such as a microphone, should sound as if it comes from one side."),
            KI18n.i18nc("@info help page of the Pan block", "Several sources play at once, and spreading them out keeps them from crowding each other.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Pan block", "To make one side of a stereo recording quieter and keep its stereo, use the Balance block instead. Pan always makes one sound of the two sides first."),
            KI18n.i18nc("@info help page of the Pan block", "Small moves go a long way. Try L 30 or R 30 before going all the way to one end."),
            KI18n.i18nc("@info help page of the Pan block", "A Peak/RMS meter after the block shows the left and the right side apart, so you can see where the sound went.")
        ]
    }
}
