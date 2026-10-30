// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The FIR Filter block's help page (docs/dev/block-help.md). The facts are
// the block's (engine/blocks/firfilter.h and .cpp: the 256-sample delay,
// the ten seconds kept, the shapes of one, two and four channels, the
// loudness matching, the trim at −80 dB, the crossfade to a new file) and
// the panel's (FirFilterPanel, FirFilterState).
HelpPage {
    typeId: "advanced.firfilter"
    summary: KI18n.i18nc("@info help page of the FIR Filter block", "Gives the sound the character recorded in a file: the echo of a room or a church, the sound of a speaker cabinet, or a correction that makes a pair of headphones sound more even.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the FIR Filter block", "An impulse response is a short recording of how a place or a piece of equipment answers a single sharp click: the click itself, then everything it turns into, its echoes and its colour. Many can be found to download, made in real rooms or with real equipment."),
            KI18n.i18nc("@info help page of the FIR Filter block", "The block plays every moment of the sound through that answer, so the sound comes out as if it had been made in that room, or played through that equipment."),
            // i18n: FIR stands for finite impulse response, the kind of filter this is.
            KI18n.i18nc("@info help page of the FIR Filter block", "FIR is short for finite impulse response: a filter built from a recording of that kind."),
            KI18n.i18nc("@info help page of the FIR Filter block", "Without a file, or with one it cannot use, the block passes the sound through unchanged.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider", "Pre-gain")
        paragraphs: [
            // i18n: dB stands for decibels.
            KI18n.i18nc("@info help page of the FIR Filter block", "Turns the sound down before the impulse response is applied, from 0 dB, unchanged, down to −24 dB. A new block starts at 0 dB."),
            KI18n.i18nc("@info help page of the FIR Filter block", "Turn it down when the result is too loud, or the meter after the block reaches the red: some responses make parts of the sound much stronger.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how late a FIR Filter's output is", "Delay")
        paragraphs: [
            KI18n.i18nc("@info help page of the FIR Filter block", "Not a setting: how late the sound leaves the block. It is always 256 samples, about five thousandths of a second at 48 kHz, whatever the file. While the session runs, the line says it in milliseconds too. Reverblabr keeps the other lanes in step with that delay.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@title:group the file a FIR Filter applies", "Impulse Response")
        covers: [
            KI18n.i18nc("@action:button choose an impulse response file", "Import File…"),
            KI18n.i18nc("@action:button stop using the impulse response file", "Remove File")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the FIR Filter block", "Import File… chooses the file: a WAV, FLAC or AIFF file with one, two or four channels. The button beside the name, Remove File, stops using it."),
            KI18n.i18nc("@info help page of the FIR Filter block", "The channels say how the response is used:")
        ]
        points: [
            KI18n.i18nc("@info help page of the FIR Filter block, the kinds of impulse response", "One channel, mono: the same response on the left and on the right."),
            KI18n.i18nc("@info help page of the FIR Filter block, the kinds of impulse response", "Two channels, stereo: one response for the left side and one for the right."),
            KI18n.i18nc("@info help page of the FIR Filter block, the kinds of impulse response", "Four channels, true stereo: how the left reaches the left and the right, and how the right reaches the left and the right. A room recorded this way sounds the most real.")
        ]
        closing: [
            KI18n.i18nc("@info help page of the FIR Filter block", "A file made at another sample rate than the session's is converted to it. Up to ten seconds of a response are used; a longer one is faded out at ten seconds."),
            KI18n.i18nc("@info help page of the FIR Filter block", "While the session runs, the line under the name says what the block made of the file: mono, stereo or true stereo, and how long it is, in seconds and in taps, one tap being one sample of the response. If the file is missing, is not sound, or has another number of channels, the line says so and the sound passes through unchanged."),
            KI18n.i18nc("@info help page of the FIR Filter block", "Choosing another file while the sound plays crossfades from the old response to the new one in about two hundredths of a second, so there is no click.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check scale the impulse response so noise comes out as loud as it went in", "Match the input's loudness")
        paragraphs: [
            KI18n.i18nc("@info help page of the FIR Filter block", "Scales the response so that the sound comes out about as loud as it went in, whatever the file was recorded at. It is on for a new block."),
            KI18n.i18nc("@info help page of the FIR Filter block", "Untick it to use the file at the level it was made at, when its maker set that level on purpose.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check take the silence off the start and the end of the impulse response", "Trim silence at both ends")
        paragraphs: [
            KI18n.i18nc("@info help page of the FIR Filter block", "Cuts the near silence off the start and the end of the response: anything more than 80 dB quieter than its loudest moment. The start of the sound then comes through as early as it can, and a long silent tail costs nothing. Every channel is trimmed alike, so they stay in step."),
            KI18n.i18nc("@info help page of the FIR Filter block", "It is off for a new block, because the silence at the start of a room's response is part of how far away the sound seems.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the FIR Filter block", "You want a voice or an instrument to sound as if it were in a particular hall, studio or church."),
            KI18n.i18nc("@info help page of the FIR Filter block", "Your headphones have a correction file, and you want everything you listen to through them corrected."),
            KI18n.i18nc("@info help page of the FIR Filter block", "A guitar recorded straight into the computer should sound as if it came through a speaker cabinet.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the FIR Filter block", "Put a Peak/RMS meter after the block the first time you try a file, and lower Pre-gain if it reaches the red."),
            KI18n.i18nc("@info help page of the FIR Filter block", "Responses longer than a few seconds take more of the computer's work. If the sound starts to break up, try a shorter one.")
        ]
    }
}
