// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Output Device block's help page (docs/dev/block-help.md). The facts
// are the block's (engine/blocks/outputdevice.h and .cpp: the volume and
// its glide, the default channel mapping, and a channel chosen by hand),
// the channel policy's (engine/dsp/channelmap.h) and the panel's
// (OutputDevicePanel, DevicePicker, DeviceChannelRows). The loop it
// refuses is the handbook's, guarded by engine/pw/feedbackguard.h.
HelpPage {
    typeId: "output.device"
    summary: KI18n.i18nc("@info help page of the Output Device block", "Plays the sound that reaches it on your speakers, your headphones or another device. It is where a lane is heard.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Output Device block", "Put it at the end of a lane and choose a device. Everything that reaches the block plays there, at the volume you set."),
            KI18n.i18nc("@info help page of the Output Device block", "On a stereo device, the left side of the lane plays on the left and the right on the right. A device with one channel gets both sides mixed together. On a device with more channels, such as surround speakers, the sound plays on the front pair and the others stay silent."),
            KI18n.i18nc("@info help page of the Output Device block", "If the device is unplugged, the block goes quiet and a warning says so. It plays again by itself when the device comes back."),
            KI18n.i18nc("@info help page of the Output Device block", "Several lanes can end in Output Devices on the same speakers. Their sound is mixed there.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:listbox the device a block plays to or records from", "Audio device")
        covers: [KI18n.i18nc("@label:listbox", "Output device")]
        paragraphs: [
            KI18n.i18nc("@info help page of the Output Device block", "Where the block plays. The list has the devices plugged in now:")
        ]
        points: [
            KI18n.i18nc("@info help page of the Output Device block, a choice of Audio device", "Default output: whatever your desktop plays to. When you change that in the system's settings, or plug in headphones it switches to, the block follows. This is what a new block uses."),
            KI18n.i18nc("@info help page of the Output Device block, a choice of Audio device", "A device by name: that device, whatever the desktop's choice is."),
            KI18n.i18nc("@info help page of the Output Device block, a choice of Audio device", "Virtual devices, at the end of the list: microphones with no hardware behind them, such as the ones you make in Reverblabr. Choose one, and other programs, such as a call, can pick the lane up as if it were a microphone.")
        ]
        closing: [
            KI18n.i18nc("@info help page of the Output Device block", "A device the block was set to that is not plugged in stays in the list, marked unavailable, so opening the settings never moves the block to another device.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider how loud the block plays to its device", "Output volume")
        paragraphs: [
            KI18n.i18nc("@info help page of the Output Device block", "How loud the block plays, from 0 %, silence, to 100 %, the sound as it reached the block. A new block starts at 100 %. It can only turn the sound down; to make a lane louder, put a Volume block before it and turn on its Overdrive."),
            KI18n.i18nc("@info help page of the Output Device block", "It changes only this block. The device's own volume, and every other program on it, stay as they are. A change glides in over a fiftieth of a second, so moving the slider never makes a click.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@title:group the less usual settings", "Advanced")
        paragraphs: [
            KI18n.i18nc("@info help page of the Output Device block", "Click the heading to choose which of the device's channels the left and the right side play on. Most devices never need it.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:listbox the left side of the session", "Left channel")
        covers: [KI18n.i18nc("@label:listbox the right side of the session", "Right channel")]
        paragraphs: [
            KI18n.i18nc("@info help page of the Output Device block", "Which of the device's channels the left side and the right side of the lane play on. The lists show the channels by the names the device gives them, such as FL and FR for front left and front right, or AUX0, AUX1 and so on."),
            KI18n.i18nc("@info help page of the Output Device block", "Default, what a new block uses, leaves it to the block, as described above. A channel you choose gets that side whole, and nothing else: outputs 3 and 4 of an audio interface for a second pair of speakers, for example."),
            KI18n.i18nc("@info help page of the Output Device block", "Once you choose a channel for one side, the other side is not heard until you choose one for it too.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Output Device block", "You want to hear what a lane does, on your speakers or in your headphones."),
            KI18n.i18nc("@info help page of the Output Device block", "You want a call or another program to hear the lane as a microphone: choose a virtual device."),
            KI18n.i18nc("@info help page of the Output Device block", "You want one lane on the speakers and another in the headphones, each at its own volume.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Output Device block", "When the session records from a virtual device, choose your real speakers here by name. Left on Default output, the block could follow the default to the very device the session records from; Reverblabr refuses that connection, because the sound would go round in a loop."),
            KI18n.i18nc("@info help page of the Output Device block", "If you hear a program twice, it is playing to the speakers itself as well as through the lane. Move the program to another device in the system's sound settings, or end the lane somewhere else."),
            KI18n.i18nc("@info help page of the Output Device block", "To check a speaker or a cable, wire a Wave block to an Output Device.")
        ]
    }
}
