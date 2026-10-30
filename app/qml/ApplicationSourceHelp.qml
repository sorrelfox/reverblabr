// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Application block's help page (docs/dev/block-help.md). The facts
// are the block's (engine/blocks/applicationsource.h and .cpp: the modes,
// the microphone and the split), the session plan's
// (engine/core/sessionplan.cpp: what is matched and left out, the web
// address), the Recorder's gaps (engine/core/recordings.h) and the
// panel's (ApplicationSourcePanel, ApplicationPicker).
HelpPage {
    typeId: "source.application"
    summary: KI18n.i18nc("@info help page of the Application block", "Brings in the sound of one program, such as a browser, a music player or a call, while it plays.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Application block", "Choose a program, and the block takes a copy of what it plays and starts a lane with it. The program goes on playing to its own speakers or headphones as before, and does not notice."),
            KI18n.i18nc("@info help page of the Application block", "The program does not have to be running. The block waits for it, and takes its sound as soon as it plays. If it is closed and started again, the block finds it again."),
            KI18n.i18nc("@info help page of the Application block", "A program that plays several sounds at once, such as a browser with two tabs, is taken whole: the sounds arrive mixed together. Sound in more than two channels, such as a film in surround, is folded into the left and the right side."),
            KI18n.i18nc("@info help page of the Application block", "Alert sounds, the short sounds a program makes for a notification, are always left out."),
            KI18n.i18nc("@info help page of the Application block", "The tile on the canvas shows the program's name, and how many of its sounds are being taken, or Waiting… while it plays nothing.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label which application the block listens to", "Source")
        covers: [KI18n.i18nc("@label:listbox", "Choose which application to capture")]
        paragraphs: [
            KI18n.i18nc("@info help page of the Application block", "The program the block listens to. A new block says Choose an app. Click it to open a list of the programs playing now, and of those seen recently."),
            KI18n.i18nc("@info help page of the Application block", "Type in the box at the top to search the list. To choose a program that is not running, type its name and choose it from the end of the list."),
            KI18n.i18nc("@info help page of the Application block", "While the program is not playing, a line under it says so. That is not a fault: its sound is taken the moment it starts.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check open a web address when the session starts", "Open URL")
        covers: [KI18n.i18nc("@label:textbox", "Web address to open")]
        paragraphs: [
            KI18n.i18nc("@info help page of the Application block", "Opens a web address each time the session starts, such as the page of a radio station or the link to a meeting. Tick the box, then type the address in the URL box under it."),
            KI18n.i18nc("@info help page of the Application block", "The address is opened by your browser or by whatever program your desktop uses for it, not by Reverblabr. If two blocks have the same address, it is opened once.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@title:group which of the application's sounds to take", "Audio device selection")
        covers: [
            KI18n.i18nc("@option:radio capture what the application plays to the usual device", "Capture audio played to the default output device"),
            KI18n.i18nc("@option:radio capture everything the application plays", "Capture audio played to all output devices"),
            KI18n.i18nc("@option:radio capture only what goes to certain devices", "Capture audio played to the specified devices"),
            KI18n.i18nc("@label the output devices whose sound the block takes", "Devices to capture from")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Application block", "Which of the program's sounds to take, by where the program sends them. The default output device is the speakers or headphones your desktop plays to unless told otherwise.")
        ]
        points: [
            KI18n.i18nc("@info help page of the Application block, a choice of Audio device selection", "Default output device: only what the program plays to the default device. This is what a new block takes, and what most people mean by the sound of a program."),
            KI18n.i18nc("@info help page of the Application block, a choice of Audio device selection", "All output devices: everything the program plays, wherever it sends it."),
            KI18n.i18nc("@info help page of the Application block, a choice of Audio device selection", "Specified devices: only what the program plays to the devices you tick in the list that appears under it. Use it for a program that plays a call to your headset and its alerts to the speakers.")
        ]
        closing: [
            KI18n.i18nc("@info help page of the Application block", "With the last choice and no device ticked, the block takes nothing, and a line says so.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@title:group the less usual settings", "Advanced")
        paragraphs: [
            KI18n.i18nc("@info help page of the Application block", "Click the heading to open these. Most people never need them.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check take the microphone the application is recording from too", "Include audio input")
        paragraphs: [
            KI18n.i18nc("@info help page of the Application block", "Also takes the microphone the program is listening to. In a call, that is your side of the conversation, so the lane carries both sides."),
            KI18n.i18nc("@info help page of the Application block", "The block follows the program: if you switch to another microphone in the program, the block takes that one. It is off for a new block.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check put the two sides of a call on one channel each", "Split between channels")
        paragraphs: [
            KI18n.i18nc("@info help page of the Application block", "With Include audio input ticked: puts the microphone on the left channel and the program on the right, each as one channel. In a recording of a call, each side can then be edited apart."),
            KI18n.i18nc("@info help page of the Application block", "It can only be ticked while the input is included, and it is off for a new block.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check put silence where the application plays nothing", "Fill playback gaps with silence")
        paragraphs: [
            KI18n.i18nc("@info help page of the Application block", "Decides what a Recorder after this block does while the program plays nothing. Ticked, as it is for a new block, the pause is recorded as silence, so the recording keeps time with the clock."),
            KI18n.i18nc("@info help page of the Application block", "Unticked, the Recorder leaves the pauses out and the recording is only what the program played. That only happens when everything reaching the Recorder comes from programs set this way: a microphone or another source beside it keeps the recording going."),
            KI18n.i18nc("@info help page of the Application block", "What you hear is the same either way.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check fold a surround stream's low-frequency channel into the mix", "Include the bass channel")
        paragraphs: [
            KI18n.i18nc("@info help page of the Application block", "Sound in surround, such as a film's, has a channel of its own for deep bass effects. It is left out when the sound is folded into two sides, because mixed in it makes everything sound heavy. Tick this to mix it in all the same."),
            KI18n.i18nc("@info help page of the Application block", "It changes nothing for a program that plays in stereo.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Application block", "You want to record a call, a lesson or a podcast from the program that plays it."),
            KI18n.i18nc("@info help page of the Application block", "You want to change how one program sounds, without touching the others."),
            KI18n.i18nc("@info help page of the Application block", "You want to stream the music of one player and nothing else that happens on the computer.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Application block", "The program still plays to its own device. If the lane ends in an Output Device on the same speakers, you hear it twice. Send the program somewhere else in the system's sound settings, or use the lane only to record."),
            KI18n.i18nc("@info help page of the Application block", "Start the program playing before you choose it: it is then at the top of the list, with how many sounds it is playing."),
            KI18n.i18nc("@info help page of the Application block", "To take everything the computer plays, use a System Audio block instead.")
        ]
    }
}
