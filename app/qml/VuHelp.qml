// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The VU block's help page (docs/dev/block-help.md). The facts are the
// block's (engine/blocks/vumeter.h and .cpp: the needle's 300 ms, the
// calibration on a sine, the three references), the scale's
// (app/vuscale.h and .cpp: −20 to +3 VU, the marks, the red above 0), the
// meter hub's (app/meterhub.h: the clip light's second and a half), the
// tray's (app/traystate.cpp) and the panel's (VuPanel, VuGauge).
HelpPage {
    typeId: "meter.vu"
    summary: KI18n.i18nc("@info help page of the VU block", "Shows how loud the sound is at this point of a lane with two needles, one for the left side and one for the right. It only measures: the sound passes through unchanged.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the VU block", "Each needle swings with the average loudness of its side, the way the needle meters on old tape machines and mixing desks did. It does not jump to every short peak. It moves smoothly, more like the way the ear judges loudness."),
            // i18n: VU stands for volume unit; it stays in Latin letters in most languages.
            KI18n.i18nc("@info help page of the VU block", "The scale is in VU, short for volume units. 0 VU is the level you choose with 0 VU is. The scale runs from −20 VU, where the needle rests on its pin, to +3 VU at its end; the part above 0 VU is red."),
            KI18n.i18nc("@info help page of the VU block", "Like a real VU meter's, the scale follows the strength of the sound rather than decibels. So 0 VU sits about seven tenths of the way along, and the quiet end from −20 to −10 VU is crowded together."),
            KI18n.i18nc("@info help page of the VU block", "A needle takes three tenths of a second to reach a new level, and swings a hair past it before it settles, as the standard for these meters asks."),
            KI18n.i18nc("@info help page of the VU block", "The small lamp by each dial lights red when that side reaches full scale, the loudest a digital signal can be. The sound has probably been clipped there: the tops of its waves were cut off, which you may hear as a crackle. The light stays on for a second and a half after the last time; click the dial to put it out sooner."),
            KI18n.i18nc("@info help page of the VU block", "Point at a dial to read its level as a number, to a tenth of a VU."),
            KI18n.i18nc("@info help page of the VU block", "The block adds no delay and changes nothing in the sound.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:listbox the level that reads 0 VU", "Reference level")
        covers: [
            KI18n.i18nc("@label:listbox the level that reads 0 VU", "0 VU is")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the VU block", "Which level the needle shows as 0 VU. Click the value beside 0 VU is to choose another. The number is how far below full scale the peaks of a steady tone are when the needle points at 0 VU:")
        ]
        points: [
            KI18n.i18nc("@info help page of the VU block, one choice of the reference level", "−20 dBFS: the same sound reads 2 VU higher than at −18, which leaves more room below full scale."),
            KI18n.i18nc("@info help page of the VU block, one choice of the reference level", "−18 dBFS: what a new block uses, and the level digital studios most often line 0 VU up with."),
            KI18n.i18nc("@info help page of the VU block, one choice of the reference level", "−14 dBFS: the same sound reads 4 VU lower than at −18, for sound that is meant to be loud.")
        ]
        closing: [
            KI18n.i18nc("@info help page of the VU block", "dBFS means decibels below full scale. Changing the reference moves the needles only; the sound stays exactly as it is.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check", "Show in system tray")
        paragraphs: [
            KI18n.i18nc("@info help page of the VU block", "Adds this meter to what you see when you click Reverblabr's icon in the system tray. You can then keep an eye on the level with the main window closed."),
            KI18n.i18nc("@info help page of the VU block", "It is off for a new block.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the VU block", "You want to judge how loud a voice or a piece of music feels, rather than how high its peaks go."),
            KI18n.i18nc("@info help page of the VU block", "You want two sources to sound about equally loud: set each so its needle swings round the same place."),
            KI18n.i18nc("@info help page of the VU block", "You find a calm needle easier to read than a jumping bar.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the VU block", "For speech, aim for the needle to swing round 0 VU, touching the red now and then."),
            KI18n.i18nc("@info help page of the VU block", "A VU needle is too slow to show short peaks. To be sure nothing clips, put a Peak/RMS meter beside it, or watch the lamps."),
            KI18n.i18nc("@info help page of the VU block", "If a lamp keeps lighting, turn down whatever comes before the meter until it stays dark.")
        ]
    }
}
