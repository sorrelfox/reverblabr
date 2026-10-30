// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Input Device block's help page (docs/dev/block-help.md). The facts
// are the block's (engine/blocks/inputdevice.h and .cpp: the default
// channel mapping, and a channel chosen by hand) and the panel's
// (InputDevicePanel with its headset message, DevicePicker,
// DeviceChannelRows).
HelpPage {
    typeId: "source.device"
    summary: KI18n.i18nc("@info help page of the Input Device block", "Brings in the sound of a microphone, an audio interface or anything else the computer can record from.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Input Device block", "Choose a device, and the block starts a lane with what it hears. Put the blocks that clean up or change the sound after it."),
            KI18n.i18nc("@info help page of the Input Device block", "A microphone with one channel is heard on both sides, left and right, at the level it was recorded at. A stereo device keeps its two sides. A device with more channels is folded into two."),
            KI18n.i18nc("@info help page of the Input Device block", "The block has no volume of its own. How loud the device records is set in the system's sound settings, where every program sees the same level."),
            KI18n.i18nc("@info help page of the Input Device block", "If the device is unplugged, the block goes quiet and a warning says so. It comes back by itself when the device does."),
            // i18n: A2DP and HSP/HFP are the names of a wireless headset's profiles, the same in every language.
            KI18n.i18nc("@info help page of the Input Device block", "A wireless headset only has a microphone in its headset profile (HSP/HFP). The system usually switches to it by itself when something records from the headset. If the headset stays on its playback profile (A2DP), the block hears nothing, and after a few seconds a message says so. Switch the profile in the system's sound settings.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:listbox the device a block plays to or records from", "Audio device")
        covers: [KI18n.i18nc("@label:listbox", "Input device")]
        paragraphs: [
            KI18n.i18nc("@info help page of the Input Device block", "What the block records from. The list has the devices plugged in now:")
        ]
        points: [
            KI18n.i18nc("@info help page of the Input Device block, a choice of Audio device", "Default input: whatever your desktop records from. When you change that in the system's settings, or plug in a headset it switches to, the block follows. This is what a new block uses."),
            KI18n.i18nc("@info help page of the Input Device block, a choice of Audio device", "A device by name: that device, whatever the desktop's choice is. Choose one to keep the block on the same microphone."),
            KI18n.i18nc("@info help page of the Input Device block, a choice of Audio device", "Virtual devices, at the end of the list: outputs with no hardware behind them, such as the ones you make in Reverblabr, that other programs play into. Choose one to bring what they play into the session.")
        ]
        closing: [
            KI18n.i18nc("@info help page of the Input Device block", "A device the block was set to that is not plugged in stays in the list, marked unavailable, so opening the settings never moves the block to another device.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@title:group the less usual settings", "Advanced")
        paragraphs: [
            KI18n.i18nc("@info help page of the Input Device block", "Click the heading to choose which of the device's channels make the left and the right side. Most devices never need it.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:listbox the left side of the session", "Left channel")
        covers: [KI18n.i18nc("@label:listbox the right side of the session", "Right channel")]
        paragraphs: [
            KI18n.i18nc("@info help page of the Input Device block", "Which of the device's channels the left side and the right side of the lane come from. The lists show the channels by the names the device gives them, such as FL and FR for front left and front right, or AUX0, AUX1 and so on."),
            KI18n.i18nc("@info help page of the Input Device block", "Default, what a new block uses, leaves it to the block, as described above. A channel you choose goes to its side whole, and nowhere else: microphone 3 of an audio interface on the left, for example, and microphone 4 on the right."),
            KI18n.i18nc("@info help page of the Input Device block", "Once you choose a channel for one side, the other side is silent until you choose one for it too.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Input Device block", "You want to record your voice, or send it through blocks that make it clearer, before it reaches a call or a stream."),
            KI18n.i18nc("@info help page of the Input Device block", "You have an audio interface with several inputs and want two particular ones."),
            KI18n.i18nc("@info help page of the Input Device block", "Other programs play into a virtual output, and you want that sound in the session.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Input Device block", "Put a Peak/RMS meter after the block and speak, to check that the sound arrives and how loud it is."),
            KI18n.i18nc("@info help page of the Input Device block", "If the level is too low or too high, change it in the system's sound settings first, rather than with a Volume block later."),
            KI18n.i18nc("@info help page of the Input Device block", "If you send the lane to your own speakers, use headphones, or the microphone hears the speakers and the sound feeds back.")
        ]
    }
}
