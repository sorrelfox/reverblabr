// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Balance block's help page (docs/dev/block-help.md). The facts are the
// block's (engine/blocks/balance.h: the range and the glide;
// engine/dsp/stereogains.h: balanceGains(), the law) and the panel's
// (StereoPositionPanel and StereoPositionWords, as BlockPanels sets them
// up for Balance).
HelpPage {
    typeId: "effect.balance"
    summary: KI18n.i18nc("@info help page of the Balance block", "Turns one side of a stereo lane down against the other, so the sound leans towards the left or the right.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Balance block", "Stereo sound has two sides, left and right, one for each speaker or each ear of your headphones. Balance makes one side quieter, and leaves the other exactly as it arrived."),
            KI18n.i18nc("@info help page of the Balance block", "Move it towards the left and the right side gets quieter. Move it towards the right and the left side gets quieter. In the centre both sides pass through unchanged."),
            KI18n.i18nc("@info help page of the Balance block", "Each side keeps its own sound: nothing moves from one side to the other. To place a single sound somewhere between the speakers, use the Pan block instead."),
            KI18n.i18nc("@info help page of the Balance block", "A change glides in over a fiftieth of a second, so moving the slider while sound plays makes no click.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider", "Balance")
        paragraphs: [
            KI18n.i18nc("@info help page of the Balance block", "Where the balance is set, from L 100 at the left end of the slider, through Centre, to R 100 at the right end. Drag the slider here or on the block's tile; the number shows where it is. A new block starts at Centre."),
            KI18n.i18nc("@info help page of the Balance block", "The number says how much quieter the other side is, in per cent of its strength. At L 30 the right side keeps 70 % of its strength; at L 100 it is silent. The side you move towards never changes.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Balance block", "One side of your headphones or speakers sounds louder than the other."),
            KI18n.i18nc("@info help page of the Balance block", "You hear better with one ear and want that side quieter, or the other side louder."),
            KI18n.i18nc("@info help page of the Balance block", "A recording was made with one side louder than the other, and you want it to sound even.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Balance block", "Move the slider a little at a time while you listen. A few steps are often enough."),
            KI18n.i18nc("@info help page of the Balance block", "Balance only turns a side down. To make the whole lane louder again afterwards, put a Volume block after it."),
            KI18n.i18nc("@info help page of the Balance block", "To hear one side alone on both speakers, or to swap the sides, use the Channels block.")
        ]
    }
}
