// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Join L/R block's help page (docs/dev/block-help.md). The facts are
// the block's (engine/blocks/joinlr.h and .cpp: which side of which lane,
// no latency of its own, the lanes lined up by the session), the inputs'
// names (laneName() in app/blockpresentation.cpp) and the wiring in
// docs/getting-started.md. It has no settings, so its inputs take their
// place. docs/hearing.md, "One ear at a time", is the user documentation
// it agrees with.
HelpPage {
    typeId: "hearing.joinlr"
    summary: KI18n.i18nc("@info help page of the Join L/R block", "Puts two lanes back together as one stereo pair: the left side from one lane, the right side from the other. With it, each ear can have a lane of blocks of its own.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Join L/R block", "Every lane in Reverblabr carries a stereo pair: a left side for the left ear and a right side for the right ear. The blocks in the Hearing group work on one ear each and pass the other side through."),
            KI18n.i18nc("@info help page of the Join L/R block", "When the two ears need quite different help, it is clearer to give each ear a lane of its own. Wire the source to two lanes, put the left ear's blocks on one and the right ear's on the other, and bring both lanes into Join L/R. What comes out is one stereo pair again, ready for your headphones or another block."),
            KI18n.i18nc("@info help page of the Join L/R block", "Join L/R takes the left side of the lane on its Left input, and the right side of the lane on its Right input. The other two sides, the ones each lane only passed through, are left out."),
            KI18n.i18nc("@info help page of the Join L/R block", "The blocks on the two lanes may hold the sound back by different amounts. Reverblabr makes the quicker lane wait for the slower one where they meet, so both ears hear the sound at the same moment. The block adds no delay of its own, and the whole is only as late as the slower lane."),
            KI18n.i18nc("@info help page of the Join L/R block", "The block has nothing to set.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@title a part of the Join L/R block's help page", "Its two inputs")
        paragraphs: [
            KI18n.i18nc("@info help page of the Join L/R block", "To wire a lane to an input, hold Ctrl and drag from the output connector on the right edge of the lane's last block to the input connector on the left edge of Join L/R. The connectors that would take the wire light up while you drag.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label an input of the Join L/R block: the lane the left ear comes in on", "Left")
        paragraphs: [
            KI18n.i18nc("@info help page of the Join L/R block", "Wire the left ear's lane here. Only its left side is used, so the hearing blocks on that lane should be the ones for the left ear, with an L on their tile.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label an input of the Join L/R block: the lane the right ear comes in on", "Right")
        paragraphs: [
            KI18n.i18nc("@info help page of the Join L/R block", "Wire the right ear's lane here. Only its right side is used, so the hearing blocks on that lane should be the ones for the right ear, with an R on their tile.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Join L/R block", "Each ear needs several blocks of its own, and one lane with all of them for both ears is hard to follow."),
            KI18n.i18nc("@info help page of the Join L/R block", "One ear needs a block that holds the sound back a long time, such as Frequency Raising, and the other does not. On one lane, the delays of all the blocks add up; on two lanes joined here, only the slower lane counts.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Join L/R block", "Name the two lanes' blocks, or look at the L and R on their tiles, so you can tell at a glance which lane is for which ear."),
            KI18n.i18nc("@info help page of the Join L/R block", "If one ear hears nothing changed, check that its lane goes into the right input: a lane of left-ear blocks wired to Right passes on only the side it did not change."),
            KI18n.i18nc("@info help page of the Join L/R block", "Put a meter after Join L/R to check that both sides arrive.")
        ]
    }
}
