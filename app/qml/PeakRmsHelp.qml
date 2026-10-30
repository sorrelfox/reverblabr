// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Peak/RMS block's help page (docs/dev/block-help.md). The facts are
// the block's (engine/blocks/peakrmsmeter.h: the 300 ms average), the
// meter hub's (app/meterhub.h: the fall and the clip light) and the
// panel's (PeakRmsPanel: the red zone and the scale).
HelpPage {
    typeId: "meter.peakrms"
    summary: KI18n.i18nc("@info help page of the Peak/RMS block", "Shows how loud the sound is at this point of a lane, for the left side and the right. It only measures: the sound passes through unchanged.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Peak/RMS block", "Each side has a bar that moves with the sound while the session runs. Two things are measured at once."),
            // i18n: RMS stays in Latin letters in most languages; the paragraph explains it.
            KI18n.i18nc("@info help page of the Peak/RMS block", "The solid part of the bar is the RMS level: the average strength of the sound over the last three tenths of a second. RMS is short for root mean square, a way of averaging that follows how loud the sound feels."),
            KI18n.i18nc("@info help page of the Peak/RMS block", "The paler part above it reaches up to the peak: the loudest single moment. Peaks show sudden sounds, such as a drum hit or a plosive, that the average hides."),
            // i18n: dB stands for decibels.
            KI18n.i18nc("@info help page of the Peak/RMS block", "The scale between the bars is in decibels (dB), from 0 at the top down to −60. 0 dB is full scale, the loudest a digital signal can be. Every 6 dB lower is about half the signal's strength."),
            KI18n.i18nc("@info help page of the Peak/RMS block", "The top 6 dB of each bar are red. Sound up there is close to the limit."),
            KI18n.i18nc("@info help page of the Peak/RMS block", "A thin line marks the highest recent peak. It waits there for as long as Hold peaks says, then sinks back at 20 dB a second."),
            KI18n.i18nc("@info help page of the Peak/RMS block", "The box above each side lights when that side reaches full scale. The sound has probably been clipped there: the tops of its waves were cut off, which you may hear as a crackle. The light stays on for a second and a half after the last time, so you do not miss it; click it to put it out sooner."),
            KI18n.i18nc("@info help page of the Peak/RMS block", "Point at a bar to read its held peak and its RMS as numbers, to a tenth of a decibel.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check", "Show peak bar")
        paragraphs: [
            KI18n.i18nc("@info help page of the Peak/RMS block", "Draws the paler part of each bar, the one that jumps up to every peak. Untick it if that jumping distracts you: the bars then show the RMS level alone, here, on the block's tile and in the system tray."),
            KI18n.i18nc("@info help page of the Peak/RMS block", "The peak line, the clip boxes and the numbers stay. A new block shows the peak bar.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check", "Show in system tray")
        paragraphs: [
            KI18n.i18nc("@info help page of the Peak/RMS block", "Adds this meter to what you see when you click Reverblabr's icon in the system tray. You can then keep an eye on the level with the main window closed."),
            KI18n.i18nc("@info help page of the Peak/RMS block", "It is off for a new block.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:listbox how long the peak line waits before falling", "Hold peaks")
        paragraphs: [
            KI18n.i18nc("@info help page of the Peak/RMS block", "How long the peak line waits at the loudest moment before it sinks. Click the value to choose another.")
        ]
        points: [
            KI18n.i18nc("@info help page of the Peak/RMS block, one choice of Hold peaks", "Off: the line does not wait, and starts sinking at once."),
            KI18n.i18nc("@info help page of the Peak/RMS block, the choices of Hold peaks", "1 second, 2 seconds or 5 seconds: the line waits that long. A new block waits 2 seconds."),
            KI18n.i18nc("@info help page of the Peak/RMS block, one choice of Hold peaks", "Until reset: the line stays at the loudest moment and never sinks. Choose another setting to let it sink again.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Peak/RMS block", "To check that a source is reaching Reverblabr at all."),
            KI18n.i18nc("@info help page of the Peak/RMS block", "To set a level before recording or streaming, so that it is loud enough and never clips."),
            KI18n.i18nc("@info help page of the Peak/RMS block", "To see what a block changes: put one meter before it and one after it.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Peak/RMS block", "For speech, aim for peaks that mostly stay out of the red."),
            KI18n.i18nc("@info help page of the Peak/RMS block", "Choose Until reset while you set a level, play the loudest part, and read where the line stopped."),
            KI18n.i18nc("@info help page of the Peak/RMS block", "If a clip box keeps lighting, turn down whatever comes before the meter until it stays dark.")
        ]
    }
}
