// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Limiter block's help page (docs/dev/block-help.md). The facts are
// the block's (engine/blocks/limiter.h and .cpp: the ranges, the defaults,
// true peak on), the limiter's own (engine/dsp/limiter.h: the 1.5 ms look
// ahead, the linked sides, the ceiling's glide) and the panel's words
// (LimiterPanel, LimiterWords, LimiterFace).
HelpPage {
    typeId: "effect.limiter"
    summary: KI18n.i18nc("@info help page of the Limiter block", "Keeps the sound under a level you choose, the ceiling, and never lets it past. Use it at the end of a lane to stop the sound from clipping.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Limiter block", "While the sound stays below the ceiling, the Limiter leaves it exactly as it is. When a loud moment would go over, it turns the sound down just enough, and lets it come back up once the moment has passed."),
            // i18n: "full scale" is the highest level digital sound can have; the sentence explains it.
            KI18n.i18nc("@info help page of the Limiter block", "The ceiling is counted down from full scale, the loudest a digital signal can be. Sound pushed past full scale is clipped: the tops of its waves are cut off, and it crackles. A limiter turns those moments down instead, which is far harder to hear."),
            KI18n.i18nc("@info help page of the Limiter block", "It listens a little ahead, one and a half thousandths of a second, so it is already turning down when a sudden peak arrives and nothing slips through. To do that it holds the whole lane back by about that much, far too little to hear."),
            KI18n.i18nc("@info help page of the Limiter block", "It turns both sides down together, so a loud sound on one side does not shift the sound towards the other.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider the level a Limiter never lets the sound past", "Ceiling")
        paragraphs: [
            // i18n: dB stands for decibels; dBTP and dBFS stay as they are, the next paragraph explains them.
            KI18n.i18nc("@info help page of the Limiter block", "The highest the sound may go, in decibels (dB) below full scale, from −12 dB to 0 dB in tenths. A new block starts at −1 dB, which leaves a little room for whatever comes after it."),
            KI18n.i18nc("@info help page of the Limiter block", "The number is written in dBTP while Catch peaks between samples is on, and in dBFS while it is off: the same scale, measured in the two ways the switch describes."),
            KI18n.i18nc("@info help page of the Limiter block", "A new ceiling glides in over a fiftieth of a second, so moving the slider while the sound plays makes no click.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider how quickly a Limiter lets go", "Release")
        paragraphs: [
            // i18n: ms stands for milliseconds, s for seconds.
            KI18n.i18nc("@info help page of the Limiter block", "How quickly the sound comes back up after a loud moment, from 10 milliseconds (ms) to 1 second. A new block uses 100 ms."),
            KI18n.i18nc("@info help page of the Limiter block", "A short release brings the sound back at once, which keeps it loud but can make it pump or crackle when the Limiter works often. A long release is smoother, but quiet sounds right after a loud one stay turned down for a while.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check a Limiter detects true peaks", "Catch peaks between samples")
        paragraphs: [
            KI18n.i18nc("@info help page of the Limiter block", "Digital sound is a row of measurements, called samples. The wave they describe can rise higher between two of them than either of them is. A player, a converter or an encoder that later rebuilds the wave can then clip, although no sample went over."),
            KI18n.i18nc("@info help page of the Limiter block", "With this on, the Limiter works out where the wave goes between the samples and keeps that under the ceiling too. These are called true peaks. It is on for a new block; leave it on unless you need every last bit of level."),
            KI18n.i18nc("@info help page of the Limiter block", "Switching it does not change how late the sound comes out, so you can try both while the session runs.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how much gain a Limiter is taking off now", "Gain reduction")
        paragraphs: [
            KI18n.i18nc("@info help page of the Limiter block", "Not a setting but a reading: how far the Limiter is turning the sound down right now, in decibels. 0.0 dB means it is doing nothing. While the session is stopped, it shows a dash."),
            KI18n.i18nc("@info help page of the Limiter block", "The block's tile shows the same while the session runs, as a number and a bar. While it is stopped, the tile shows the ceiling.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Limiter block", "A recording or a stream must never clip, whatever happens in it."),
            KI18n.i18nc("@info help page of the Limiter block", "Sudden loud sounds, such as a laugh, a cough or a door, jump out of an otherwise even level."),
            KI18n.i18nc("@info help page of the Limiter block", "You have turned a source up and its loudest moments now go too high.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Limiter block", "Put the Limiter last in a lane, after everything that can make the sound louder."),
            KI18n.i18nc("@info help page of the Limiter block", "Watch Gain reduction. A few decibels now and then is fine. If it hardly ever reads 0.0 dB, the sound arriving is too loud: turn it down earlier in the lane rather than letting the Limiter do all the work."),
            KI18n.i18nc("@info help page of the Limiter block", "For a recording or a stream, a ceiling of −1 dB with Catch peaks between samples on is a safe start."),
            KI18n.i18nc("@info help page of the Limiter block", "The Volume block already ends with a limiter at −1 dB. Add this block when you want to choose the ceiling or see how hard it works.")
        ]
    }
}
