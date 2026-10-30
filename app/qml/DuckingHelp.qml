// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Ducking block's help page (docs/dev/block-help.md). The facts are the
// block's (engine/blocks/ducking.h and .cpp: the two inputs, the louder
// side deciding, the straight-line ramps, the hold, the ranges) and the
// panel's (DuckingPanel, DuckingWords).
HelpPage {
    typeId: "advanced.ducking"
    summary: KI18n.i18nc("@info help page of the Ducking block", "Turns background sound, such as music, down while a main sound, usually a voice, is heard, and brings it back up when the voice stops.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Ducking block", "The block has two inputs on its tile: main and background. Wire the voice into main and the music into background. What comes out is the two mixed together."),
            KI18n.i18nc("@info help page of the Ducking block", "The block listens to the main input. While it is louder than the threshold, the background is turned down to the ducked volume; when it falls quiet, the background comes back up. The main input itself always passes through unchanged."),
            KI18n.i18nc("@info help page of the Ducking block", "Whichever side of the main input is louder decides, so a voice on the left or the right ducks the music as surely as one in the middle."),
            KI18n.i18nc("@info help page of the Ducking block", "The line at the top says Ducking while the main input is heard and for the hold after it, and Not ducking otherwise."),
            KI18n.i18nc("@info help page of the Ducking block", "The block adds no delay.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider", "Main threshold")
        paragraphs: [
            // i18n: dB stands for decibels.
            KI18n.i18nc("@info help page of the Ducking block", "How loud the main input has to be to duck the background, in decibels (dB) below full scale, from −60 dB to 0 dB. Full scale is the loudest a digital signal can be. A new block starts at −30 dB."),
            KI18n.i18nc("@info help page of the Ducking block", "Lower makes the block react to quieter sound on the main input; higher makes it wait for louder sound.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider", "Ducked volume")
        paragraphs: [
            KI18n.i18nc("@info help page of the Ducking block", "How loud the background plays while it is ducked, as a percentage of its full volume, from 0 % to 100 %. The number beside it says the same in decibels. A new block starts at 40 %, about −8 dB."),
            KI18n.i18nc("@info help page of the Ducking block", "At 0 % the background goes silent while the main input is heard. At 100 % it is not turned down at all.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@title:group how quickly the Ducking block works", "Timing")
        paragraphs: [
            KI18n.i18nc("@info help page of the Ducking block", "How quickly the background goes down and comes back. Click the heading to open these settings. A new block starts with times that suit speech over music, so you may never need them.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider how long the background takes to go down", "Attack")
        paragraphs: [
            // i18n: ms stands for milliseconds, thousandths of a second.
            KI18n.i18nc("@info help page of the Ducking block", "How long the background takes to go all the way down once the main input starts, in thousandths of a second (ms), from 5 to 1000 ms. A new block takes 50 ms. Short is quick but can sound abrupt; long is gentle, but the first word may be heard over the music.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider how long the background stays down after the main input stops", "Hold")
        paragraphs: [
            KI18n.i18nc("@info help page of the Ducking block", "How long the background stays down after the main input falls quiet, from 0 to 2000 ms. A new block holds it for 300 ms."),
            KI18n.i18nc("@info help page of the Ducking block", "The hold bridges the short pauses between words, so the music does not jump up and down in every gap.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider how long the background takes to come back up", "Release")
        paragraphs: [
            KI18n.i18nc("@info help page of the Ducking block", "How long the background takes to come all the way back up after the hold, from 10 to 5000 ms. A new block takes 500 ms. Longer brings the music back more gently.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Ducking block", "You talk over music in a stream or a podcast, and want the music to make room for you."),
            KI18n.i18nc("@info help page of the Ducking block", "You want a game or a video to get quieter whenever somebody speaks in a call."),
            KI18n.i18nc("@info help page of the Ducking block", "An announcement should always be heard over the background.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Ducking block", "To set the threshold, speak normally and watch the line at the top. Raise the threshold until breathing and background noise on the microphone no longer say Ducking, while your voice still does."),
            KI18n.i18nc("@info help page of the Ducking block", "If the music pumps up and down while you talk, make Hold longer."),
            KI18n.i18nc("@info help page of the Ducking block", "For background music under a voice, a ducked volume between 20 % and 40 % usually keeps the music present without covering the words."),
            KI18n.i18nc("@info help page of the Ducking block", "Put a Volume block before the main input if the voice is too quiet to reach the threshold.")
        ]
    }
}
