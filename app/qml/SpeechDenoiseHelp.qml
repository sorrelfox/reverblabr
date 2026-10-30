// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Speech Denoise block's help page (docs/dev/block-help.md). The facts
// are the block's (engine/blocks/speechdenoise.h: the model at 48 kHz only,
// the 30 ms it holds the sound back, the limit gated by the voice, its hold
// and its close, mono) and the panel's (SpeechDenoisePanel,
// SpeechDenoiseState).
HelpPage {
    typeId: "advanced.speechdenoise"
    summary: KI18n.i18nc("@info help page of the Speech Denoise block", "Clears background noise from a voice, such as keyboard clatter, traffic, a fan or a busy room, with nothing to set up first.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            // i18n: RNNoise is the name of the model, the same in every language.
            KI18n.i18nc("@info help page of the Speech Denoise block", "The block uses RNNoise, a small model that has been trained on many recordings of people speaking. It recognises what a voice sounds like and turns down everything else, moment by moment, so it also copes with noise that keeps changing."),
            KI18n.i18nc("@info help page of the Speech Denoise block", "It also tells when somebody is speaking. Between words and sentences, the whole sound goes down as far as Noise reduction allows. When the voice starts again, it opens at once, and stays open a fifth of a second after the voice stops, so the ends of words are not cut off."),
            KI18n.i18nc("@info help page of the Speech Denoise block", "The line at the top says Speech detected while it hears a voice, and No speech detected while it does not."),
            KI18n.i18nc("@info help page of the Speech Denoise block", "The model works only when the session runs at 48 kHz, the sample rate it was trained at. At any other rate the line says Only works at 48 kHz, and the sound passes through unchanged."),
            KI18n.i18nc("@info help page of the Speech Denoise block", "The block holds the sound back by three hundredths of a second. Reverblabr keeps the other lanes in step with that delay."),
            KI18n.i18nc("@info help page of the Speech Denoise block", "The block is only there when Reverblabr was built with RNNoise, as it is on systems that package it.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider", "Noise reduction")
        paragraphs: [
            // i18n: dB stands for decibels.
            KI18n.i18nc("@info help page of the Speech Denoise block", "The most the block turns anything down, in decibels (dB), from 0 to 60 dB. A new block starts at 40 dB."),
            KI18n.i18nc("@info help page of the Speech Denoise block", "While somebody speaks, no part of the sound is turned down further than this, and between words the whole sound goes down by this much. At 0 dB the block changes nothing. Lower values leave a little of the room in, which can sound more natural.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check a Speech Denoise block folds stereo to one before the model", "Mono signal processing")
        paragraphs: [
            KI18n.i18nc("@info help page of the Speech Denoise block", "Mixes the left and the right side into one before the model, and sends the one result to both sides. The block then does half the work. A single voice is usually mono anyway. It is off for a new block."),
            KI18n.i18nc("@info help page of the Speech Denoise block", "Switching it on or off while sound plays changes over smoothly, with no gap.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Speech Denoise block", "You talk in a call or a stream, and your keyboard, your fan or the street can be heard behind you."),
            KI18n.i18nc("@info help page of the Speech Denoise block", "A recorded interview has a noisy background that changes all the time."),
            KI18n.i18nc("@info help page of the Speech Denoise block", "The noise is not steady enough for the Denoise block to learn it.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Speech Denoise block", "Use it on speech only. Music and singing are not what the model knows, and it can turn them down too."),
            KI18n.i18nc("@info help page of the Speech Denoise block", "If the voice sounds thin or robotic, lower Noise reduction to 20 or 30 dB."),
            KI18n.i18nc("@info help page of the Speech Denoise block", "Turn on Mono signal processing for a single microphone, to spare the computer's work."),
            KI18n.i18nc("@info help page of the Speech Denoise block", "If the line says Only works at 48 kHz, the computer's sound system is running at another rate, which the session follows. The Info tab of the sidebar shows the rate in use.")
        ]
    }
}
