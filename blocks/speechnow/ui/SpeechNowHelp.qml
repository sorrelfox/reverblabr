// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Speech Now block's help page (docs/dev/block-help.md). The facts are
// the block's (blocks/speechnow/speechnow.h and .cpp) and the panel's words.
HelpPage {
    typeId: "meter.speechnow"
    summary: KI18n.i18nc("@info help page of the Speech Now block", "Shows Speaking while a voice is on this lane, and Quiet when not. For a moment after a voice stops it shows Quiet again. It can't tell who is speaking or what was said.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Speech Now block", "The block listens to the lane and passes the sound on exactly as it arrived, with no delay. Put it right after a program or a microphone, before anything that changes the level."),
            KI18n.i18nc("@info help page of the Speech Now block", "Quiet again says what the sound did. A pause for thought and the end of a turn look the same, so it does not mean the talker has finished. Someone may be drawing breath."),
            KI18n.i18nc("@info help page of the Speech Now block", "It shows Not watching whenever it can't hear the lane: when the session has stopped, when the session is overloaded, or when the engine has stopped answering. Music, singing and film can read as Speaking too.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label what a Speech Now is called", "Name")
        covers: [
            KI18n.i18nc("@label what a Speech Now says now", "Now")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Speech Now block", "What the tile, the tray and the floating label call this block, up to 24 characters. Two blocks named Call and You read as two talkers, each on its own lane.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how likely a voice is on the lane", "Voice on this lane")
        paragraphs: [
            KI18n.i18nc("@info help page of the Speech Now block", "How likely a voice is right now, from 0 to 100 %. From the line at 50 % a moment of sound counts as voiced, and three in a row show Speaking.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how Speech Now listens", "Detector")
        covers: [
            KI18n.i18nc("@item:inlistbox how Speech Now listens", "Voice model"),
            KI18n.i18nc("@item:inlistbox how Speech Now listens", "Level and rhythm")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Speech Now block", "Voice model is a small model that runs on this computer, and is the better judge. It needs a sound system running at 48 kHz, and a build that has it. The card says so when one is missing."),
            KI18n.i18nc("@info help page of the Speech Now block", "Level and rhythm uses far less of the processor. It looks for sound in the band speech uses, with a level that moves as syllables do. It can be wrong more often: music with a beat can pass, and a voice in a noisy room can be missed.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check", "Floating label")
        covers: [
            KI18n.i18nc("@label where a Speech Now shows", "Show it on")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Speech Now block", "A small window that stays over other programs, with a row for each Speech Now that has this on, up to four. It stays when you close the main window, while there is a system tray. It starts off, so nothing appears unasked."),
            KI18n.i18nc("@info help page of the Speech Now block", "Drag it to move it. Turn it off here or in the tray's popover. On a desktop that may not keep it on top, the card says so. A full-screen share may show it, name included.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check", "Show in system tray")
        paragraphs: [
            KI18n.i18nc("@info help page of the Speech Now block", "Adds this block to what you see when you click Reverblabr's icon in the system tray: a dot and a word.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@title:group a part of the Speech Now card", "Place the label")
        covers: [
            KI18n.i18nc("@label how big the floating label is", "Size"),
            KI18n.i18nc("@item:inlistbox a size of label", "Small"),
            KI18n.i18nc("@item:inlistbox a size of label", "Large"),
            KI18n.i18nc("@label which screen the floating label is on", "Screen"),
            KI18n.i18nc("@label how far the floating label sits from the left of the screen", "From the left"),
            KI18n.i18nc("@label how far the floating label sits from the top of the screen", "From the top")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Speech Now block", "The size of the label, the screen it is on, and how far it sits from that screen's top left corner. There is one label for all your blocks, and it stays where you left it on this desktop, whichever session you open.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how long a voice may pause and still count as speaking", "Hold")
        covers: [
            KI18n.i18nc("@title:group a part of the Speech Now card", "Timing")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Speech Now block", "How long a voice may pause and still count as Speaking, from 100 to 2000 ms. It starts at 200 ms. A longer hold shows fewer false stops inside a sentence, and shows the true stop later.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how long Quiet again stays up", "Quiet again for")
        paragraphs: [
            KI18n.i18nc("@info help page of the Speech Now block", "How long Quiet again stays up, from 0 to 10 seconds. It starts at 2 seconds. At 0 the block goes straight to Quiet.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label a voice shorter than this earns no Quiet again", "Shortest talk")
        paragraphs: [
            KI18n.i18nc("@info help page of the Speech Now block", "A voice shorter than this earns no Quiet again, so a cough or an \"mm\" goes back to Quiet. From 0 to 2000 ms, and it starts at 300 ms.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Speech Now block", "You follow a call by captions, and they come too late to tell you when someone has stopped."),
            KI18n.i18nc("@info help page of the Speech Now block", "A voice call has no picture to look at."),
            KI18n.i18nc("@info help page of the Speech Now block", "You can't tell whether a video has speech in it.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Speech Now block", "Put one after the call's Application block and name it Call, and one after your microphone's Input Device and name it You."),
            KI18n.i18nc("@info help page of the Speech Now block", "If Quiet again keeps showing in the middle of sentences, raise Hold, or try the Steady preset."),
            KI18n.i18nc("@info help page of the Speech Now block", "With a screen reader, set a key for Say Who Is Speaking in System Settings. One press says one sentence, such as Call speaking, You quiet. It tells you who has a voice now, not when to come in.")
        ]
    }
}
