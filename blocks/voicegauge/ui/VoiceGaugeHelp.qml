// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Voice Gauge block's help page (docs/dev/block-help.md). The facts are
// the block's (blocks/voicegauge/voicegauge.h and .cpp) and the panel's
// words.
HelpPage {
    typeId: "meter.voicegauge"
    summary: KI18n.i18nc("@info help page of the Voice Gauge block", "Shows the level of a voice at this point on the lane, in LUFS, against two marks you place. It says where the reading stands against your marks, and nothing about how you sound. It can be wrong.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Voice Gauge block", "The block reads the lane and passes the sound on exactly as it arrived, with no delay. The number is the level over the last few seconds of voice. In a pause the number stays and the word says Held, so it does not fall between sentences."),
            KI18n.i18nc("@info help page of the Voice Gauge block", "The number moves with your voice, your distance from the microphone and the microphone's gain, and the block can't tell them apart. It is a level at this point on the lane, before the call program. The program's own gain and the other side's volume come later."),
            KI18n.i18nc("@info help page of the Voice Gauge block", "No sound here means nothing has reached this point for 2 seconds. It can't say why, and it can't see a mute inside a call program, which sits after this block. To check, say a word and watch the dot: if it stays grey, the voice is not reaching this point. If it lights, the sound got this far, and nothing further is known."),
            KI18n.i18nc("@info help page of the Voice Gauge block", "A voice too soft for the detector is not counted, so the number stays Held at its last value and the dot stays grey. Any voice counts, including one from a loudspeaker or a television.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label what a Voice Gauge is called", "Name")
        covers: [
            KI18n.i18nc("@label what a Voice Gauge says now", "Now")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Voice Gauge block", "What the tile, the tray and the floating bar call this block, up to 24 characters.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label the level of a voice over a few seconds", "Voice level")
        paragraphs: [
            KI18n.i18nc("@info help page of the Voice Gauge block", "The reading on a fixed scale from -60 to 0 LUFS, with a line at each mark. The bar is one colour wherever it stands, and half as strong while the number is held. A first number comes after 1 second of voice.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label a mark placed on a voice level", "Upper mark")
        covers: [
            KI18n.i18nc("@label a mark placed on a voice level", "Lower mark")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Voice Gauge block", "Two levels of your own, from -60 to -6 LUFS, or Not set. A word then says where the reading stands against them, and it changes only half a unit past a mark. The lower mark stays at least 1 LU under the upper one: moving one past the other pushes it along. A block has no marks of its own, as a built-in level would say what a voice should be.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@action:button", "Use This Level")
        covers: [
            KI18n.i18nc("@action:button", "Clear Marks")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Voice Gauge block", "After 20 seconds of voice, this places an upper mark and a lower mark around the level of that voice, Room each side apart from it. Only voice counts towards the 20 seconds. Clear Marks takes both away."),
            KI18n.i18nc("@info help page of the Voice Gauge block", "If someone is on a call with you, ask them to say when the level suits them, and press Use This Level then. Marks hold for this microphone at this gain. The card names the microphone they were placed on. Alone, the marks record what you did for 20 seconds, and the block can't know more than that.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how Voice Gauge listens", "Detector")
        covers: [
            KI18n.i18nc("@item:inlistbox how Speech Now listens", "Voice model"),
            KI18n.i18nc("@item:inlistbox how Speech Now listens", "Level and rhythm")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Voice Gauge block", "Voice model is a small model that runs on this computer. It needs a sound system running at 48 kHz, and a build that has it. The card says so when one is missing. Level and rhythm uses far less of the processor and can be wrong more often.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check", "Floating bar")
        covers: [
            KI18n.i18nc("@label where a Voice Gauge shows", "Show it on")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Voice Gauge block", "A row with the number and the word in the small window that stays over other programs, shared with Speech Now. There is room for four rows. It starts off, and you can turn it off here or in the tray's popover. Drag the window to move it. A full-screen share may show it, name included.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check", "Show in system tray")
        paragraphs: [
            KI18n.i18nc("@info help page of the Voice Gauge block", "Adds this block to what you see when you click Reverblabr's icon in the system tray: a dot, the number and where it stands.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@title:group a part of the Voice Gauge card", "Place the bar")
        covers: [
            KI18n.i18nc("@label how big the floating label is", "Size"),
            KI18n.i18nc("@item:inlistbox a size of label", "Small"),
            KI18n.i18nc("@item:inlistbox a size of label", "Large"),
            KI18n.i18nc("@label which screen the floating label is on", "Screen"),
            KI18n.i18nc("@label how far the floating label sits from the left of the screen", "From the left"),
            KI18n.i18nc("@label how far the floating label sits from the top of the screen", "From the top")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Voice Gauge block", "The same place as Speech Now's label, since they share one window.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how many seconds of voice a reading is the mean of", "Reading over")
        covers: [
            KI18n.i18nc("@title:group a part of the Voice Gauge card", "Reading")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Voice Gauge block", "How many seconds of voice the number is the mean of, from 1 to 10. It starts at 3. A shorter time follows you faster and wanders more. A rise shows sooner than a fall, as a mean of energy favours the loud part.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how long without a voice before a Voice Gauge starts again", "Forget after")
        paragraphs: [
            KI18n.i18nc("@info help page of the Voice Gauge block", "How long without a voice before the block starts clean, from 10 to 600 seconds, so that an old turn does not mix with a new one. It starts at 60 seconds.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how far either side of a level Use This Level puts the marks", "Room each side")
        paragraphs: [
            KI18n.i18nc("@info help page of the Voice Gauge block", "How far Use This Level puts each mark from the level, from 1 to 9 LU. It starts at 3. It does nothing to marks already placed.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Voice Gauge block", "You can't judge your own level on a call, and only find out when someone says so."),
            KI18n.i18nc("@info help page of the Voice Gauge block", "You want one answer from someone kept for later, as two marks for this microphone.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Voice Gauge block", "Put it right after the microphone's Input Device, and name it You. With nothing after it the call program keeps the real microphone, and a stopped session costs the call nothing."),
            KI18n.i18nc("@info help page of the Voice Gauge block", "Put it before Voice Evener. After Voice Evener the reading sits near that block's target, and hides what the voice did. After Speech Denoise, a pause can read as No sound here."),
            KI18n.i18nc("@info help page of the Voice Gauge block", "A Bluetooth headset drops to hands-free sound while anything records from it, and a session left running holds it there outside calls too."),
            KI18n.i18nc("@info help page of the Voice Gauge block", "With a screen reader, set a key for Say Voice Level in System Settings. One press says one sentence, such as Minus 24 LUFS, between marks. Nothing is said unasked.")
        ]
    }
}
