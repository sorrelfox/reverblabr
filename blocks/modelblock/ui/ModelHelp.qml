// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Model block's help page (docs/dev/block-help.md). The facts are the
// block's (blocks/modelblock/modelblock.h and .cpp: the spare time's choices,
// the mono choice, the silence in every state but running) and the panel's
// (ModelPanel, ModelState). The Beta note is the help window's, from the
// presentation table's flag.
HelpPage {
    typeId: "effect.model"
    summary: KI18n.i18nc("@info help page of the Model block", "Sends the sound through a trained model from a folder you choose, and plays what the model makes of it: for example with the noise taken out, or spoken in another voice.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Model block", "A model is a file made by training a program on many examples. This block runs one while the sound plays. The model runs in a separate process, so a model that is slow or crashes cannot stop the session or the other lanes."),
            KI18n.i18nc("@info help page of the Model block", "Reverblabr includes no models and never downloads one. You choose a folder that you already have, and the terms of that model are yours to read."),
            KI18n.i18nc("@info help page of the Model block", "Sound only leaves this block after the model has processed it. In every other case the block is silent, and the sound does not reach the next block either. That holds when no model is chosen, while one loads, when one cannot be loaded, when it falls behind, when its process stops and restarts, when it gives back something that is not sound, and when the block is switched off. The card says why it is silent.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@title:group the folder a Model block runs", "Model Folder")
        covers: [
            KI18n.i18nc("@action:button choose the folder that holds a model", "Choose Folder…"),
            KI18n.i18nc("@action:button stop using the model folder", "Remove Folder"),
            KI18n.i18nc("@action:button read a model's licence file", "Open Licence")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Model block", "Choose Folder… picks the folder that holds the model. The line under it says what kind of model it found, or why it cannot use the folder. Remove Folder stops using it."),
            KI18n.i18nc("@info help page of the Model block", "This version runs a streaming DeepFilterNet model, for noise cleanup, a voice conversion model, and any ONNX model whose folder has a file called reverblabr-model.json that says how to call it. The Model section of the blocks guide lists what that file holds."),
            KI18n.i18nc("@info help page of the Model block", "A voice conversion model is three ONNX files in one folder: the voice, a content encoder that hears what is said, and a pitch estimator that hears how high it is. The last two are the same for many voices, and the card says which one is missing."),
            KI18n.i18nc("@info help page of the Model block", "If the folder has a licence file, the card names it and Open Licence shows it. If it has none, the card says so."),
            KI18n.i18nc("@info help page of the Model block", "Files that can run code when they are opened, such as .pt, .pth, .ckpt and .pkl, are never opened. Use an ONNX version of the model.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:spinbox how far a voice is moved up or down, in semitones", "Pitch shift")
        covers: [
            KI18n.i18nc("@title:group the settings of a voice in a Model block", "Voice")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Model block", "Shown for a voice. How many semitones higher or lower the voice speaks than you do, from 24 down to 24 up. A voice sounds most like itself near its own pitch, so a man using a woman's voice may want about 12 up, and the other way round about 12 down. The change takes effect within a moment and the sound does not stop."),
            KI18n.i18nc("@info help page of the Model block", "A voice always runs once, on the two sides mixed, and both sides play it."),
            KI18n.i18nc("@info help page of the Model block", "Use only voices you have the right to use.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:listbox the time a Model block allows a model for each run", "Spare time")
        covers: [
            KI18n.i18nc("@title:group how a Model block runs its model", "Running")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Model block", "How much time the model is given, from 20 to 250 thousandths of a second. A new block starts at 50. It is part of the delay: less is a shorter delay, and more gives a slow model room. Reverblabr keeps the other lanes in step with that delay."),
            KI18n.i18nc("@info help page of the Model block", "A voice takes far longer than a cleanup model, so its delay holds time for that as well, and Delay on the card says what the whole of it comes to, usually about 0.4 seconds."),
            KI18n.i18nc("@info help page of the Model block", "If the block says it cannot keep up, choose a longer time. If the model's Time used is above 100 %, it is too slow for this computer at any setting.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:listbox what a mono model does with a stereo lane", "Mono models")
        paragraphs: [
            KI18n.i18nc("@info help page of the Model block", "Most models take one channel. Once a side runs the model twice, which keeps the stereo picture and takes twice the work. On the sides mixed runs it once on the two sides together, and both sides then play the result.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label what a Model block is doing", "State")
        covers: [
            KI18n.i18nc("@label how late a Model block's output is", "Delay"),
            KI18n.i18nc("@label how much of the available time a Model block's model uses", "Time used"),
            KI18n.i18nc("@action:button start a Model block's model again after it failed", "Try Again")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Model block", "Not a setting: what the block is doing, how late the sound is, and how much of the time it has the model is using."),
            KI18n.i18nc("@info help page of the Model block", "A process that crashes is started again by itself. After three crashes in a minute the block stays silent and says Failed, and Try Again starts it once more.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Model block", "You have a noise cleanup model, and want it on your microphone or on a call."),
            KI18n.i18nc("@info help page of the Model block", "You have a voice conversion model, and want to speak in that voice. The delay is too long to hear yourself comfortably while you talk."),
            KI18n.i18nc("@info help page of the Model block", "You have an ONNX model that takes sound and gives sound, and want to hear it live.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Model block", "If the sound drops out, look at the card first: it says why."),
            KI18n.i18nc("@info help page of the Model block", "Keep a second lane around the block if you need to hear the original while you try a model.")
        ]
    }
}
