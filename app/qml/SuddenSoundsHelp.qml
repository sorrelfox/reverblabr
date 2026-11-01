// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Sudden Sounds block's help page (docs/dev/block-help.md). The facts are
// the block's (engine/blocks/suddensounds.h and .cpp, engine/dsp/transientcut.h)
// and the panel's (SuddenSoundsPanel, SuddenSoundsWords).
HelpPage {
    typeId: "hearing.suddensounds"
    summary: KI18n.i18nc("@info help page of the Sudden Sounds block", "Softens sudden sharp sounds without turning everything down. It watches five ranges and acts for moments at a time. A sound that stays loud is left alone after about a quarter of a second.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Sudden Sounds block", "A new block is set to Off and changes nothing until you choose a Strength. Off passes the sound as it came, only slightly later, so choosing a Strength never moves anything in time."),
            KI18n.i18nc("@info help page of the Sudden Sounds block", "The block compares each range of the sound with what that range held just before. It does not listen for how loud a sound is. A knock far below full scale can count, and a loud film scene that stays loud does not."),
            KI18n.i18nc("@info help page of the Sudden Sounds block", "In the first tenth of a second of sound it does nothing. A sound must stand far above the sound before it to count, so plenty of sudden sounds pass. A door's low thump goes through nearly as it came. It does not make speech clearer, and it does not take noise away."),
            KI18n.i18nc("@info help page of the Sudden Sounds block", "It works on one ear and passes the other through, lined up. Add a second block for the other ear.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label which ear a hearing block works on", "Ear")
        paragraphs: [
            KI18n.i18nc("@info help page of the Sudden Sounds block", "Which ear the block works on.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how much Sudden Sounds softens", "Strength")
        covers: [
            KI18n.i18nc("@option:radio Sudden Sounds' Strength", "Off"),
            KI18n.i18nc("@option:radio Sudden Sounds' Strength", "Gentle"),
            KI18n.i18nc("@option:radio Sudden Sounds' Strength", "Medium"),
            KI18n.i18nc("@option:radio Sudden Sounds' Strength", "Strong")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Sudden Sounds block", "Off changes nothing. Gentle takes the top off a sudden sound. Medium takes off more. Strong takes off most, up to 15 dB. The louder the sudden sound stands above what came before it, the more it is softened.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@label how much Sudden Sounds is taking off", "Softened now")
        paragraphs: [
            KI18n.i18nc("@info help page of the Sudden Sounds block", "The most taken off any one range of the sound, kept on screen for a second. The whole sound falls by less.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@label the count on a Sudden Sounds card", "Softened in the last minute")
        paragraphs: [
            KI18n.i18nc("@info help page of the Sudden Sounds block", "How many times it softened something in the last minute. If you see it acting while nothing sudden happens, it is touching speech. Gentle takes less off each time, and Off stops it.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Sudden Sounds block", "A cup or a pen knocks on a call, and the sharp edge of it bothers you."),
            KI18n.i18nc("@info help page of the Sudden Sounds block", "Keys or clicks on somebody else's line stand out.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Sudden Sounds block", "Put it before any block that moves or lifts sound, such as Frequency Lowering and Hearing Compensation, so the sound is softened before it is lifted."),
            KI18n.i18nc("@info help page of the Sudden Sounds block", "Leave it off for music: it softens drums and plucked notes too."),
            KI18n.i18nc("@info help page of the Sudden Sounds block", "Its thresholds are provisional: they have not yet been set on recordings of real sounds.")
        ]
    }
}
