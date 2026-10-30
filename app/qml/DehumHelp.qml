// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Dehum block's help page (docs/dev/block-help.md). The facts are the
// block's (engine/blocks/dehum.h: the ranges, the notches' widths, 6 dB a
// step of Amount, the 30 ms glide; engine/dsp/humtracker.h: what Auto
// follows) and the panel's (DehumPanel, DehumWords).
HelpPage {
    typeId: "advanced.dehum"
    summary: KI18n.i18nc("@info help page of the Dehum block", "Takes out the low hum that mains electricity leaves in a recording, and the buzz of its overtones above it, while the rest of the sound stays as it was.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            // i18n: Hz stands for hertz; the sentence explains it.
            KI18n.i18nc("@info help page of the Dehum block", "The electricity in the wall changes direction 50 or 60 times a second, depending on the country. A badly earthed cable, a guitar amplifier or a cheap power supply can let that into the sound as a deep, steady hum at 50 or 60 hertz (Hz), which means vibrations a second."),
            KI18n.i18nc("@info help page of the Dehum block", "A hum is rarely alone. It comes with overtones, called harmonics, at two, three, four times its frequency and more, which make it sound like a buzz."),
            KI18n.i18nc("@info help page of the Dehum block", "The block cuts a very narrow notch out of the sound at the hum's frequency and at each harmonic, up to the tenth. The lowest notch is only 2 Hz wide, and the tenth about 6 Hz, so a voice or an instrument between them is left almost exactly as it was."),
            KI18n.i18nc("@info help page of the Dehum block", "A change of any setting glides in over three hundredths of a second, so nothing clicks. The block adds no delay.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label which mains frequency a Dehum block takes out", "Mains")
        paragraphs: [
            KI18n.i18nc("@info help page of the Dehum block", "Sets the base frequency to 50 Hz or 60 Hz in one press. Most of the world uses 50 Hz; North America and some other countries use 60 Hz. A new block starts at 50 Hz.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider a Dehum block's base frequency", "Base frequency")
        paragraphs: [
            KI18n.i18nc("@info help page of the Dehum block", "The hum's own frequency, from 48 to 62 Hz in tenths of a hertz. Every notch is placed from it: the harmonics are at whole multiples of it."),
            KI18n.i18nc("@info help page of the Dehum block", "Use it when the hum is a little off 50 or 60 Hz, as with a generator, or with a tape that plays a little fast or slow.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check", "Follow a drifting hum automatically")
        paragraphs: [
            KI18n.i18nc("@info help page of the Dehum block", "With Auto on, the block listens for the hum while the session runs and moves the notches with it when its frequency wanders. It starts looking at the base frequency, and checks what it finds against the second and third harmonics, so a note in the music does not lead it away."),
            KI18n.i18nc("@info help page of the Dehum block", "The line under the switch says what it is doing: which frequency it is following, to a hundredth of a hertz, or that it hears no hum and holds the notches where they were."),
            KI18n.i18nc("@info help page of the Dehum block", "With Auto on, the notches are a little wider, to keep hold of a hum on the move. It is off for a new block.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider how much a Dehum block takes out", "Amount")
        paragraphs: [
            KI18n.i18nc("@info help page of the Dehum block", "How deep the notches go, from 0 to 10. Each step is 6 dB deeper, and the line under the slider says how far the hum is turned down: 30 dB at 5, where a new block starts, and 60 dB at 10."),
            KI18n.i18nc("@info help page of the Dehum block", "At 0 the line says Off, and the sound passes through untouched.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:spinbox", "Harmonics")
        paragraphs: [
            KI18n.i18nc("@info help page of the Dehum block", "How many notches there are, counting the base frequency as the first: from 1, the hum alone, to 10, the hum and nine harmonics above it. A new block takes out all ten.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Dehum block", "A recording or a microphone has a low hum you can hear in the quiet parts."),
            KI18n.i18nc("@info help page of the Dehum block", "An electric guitar or a turntable buzzes through its amplifier."),
            KI18n.i18nc("@info help page of the Dehum block", "You are digitising old tapes, and their hum moves up and down with the speed of the tape.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Dehum block", "Try 50 Hz first, then 60 Hz. The right one makes the hum disappear; the wrong one leaves it."),
            KI18n.i18nc("@info help page of the Dehum block", "Turn Amount only as high as you need. Deep notches are harmless to most sound, but a low bass note that falls exactly on one can lose a little."),
            KI18n.i18nc("@info help page of the Dehum block", "If the deep hum is gone but some of the buzz above it is left, the hum is probably a little off its frequency, and the higher harmonics miss their notches. Turn on Auto, or move the base frequency slowly until the buzz goes."),
            KI18n.i18nc("@info help page of the Dehum block", "Fewer harmonics leave more of the sound alone. If the buzz is gone at 4 or 5, you do not need the rest.")
        ]
    }
}
