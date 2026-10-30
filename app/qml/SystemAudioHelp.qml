// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The System Audio block's help page (docs/dev/block-help.md). The facts
// are the block's (engine/blocks/systemaudio.h: what it leaves out), the
// session plan's (engine/core/sessionplan.cpp: the alert sounds and the
// exclusion list) and the panel's (SystemAudioPanel, ApplicationPicker,
// and SystemAudioFace for the words on the tile).
HelpPage {
    typeId: "source.system"
    summary: KI18n.i18nc("@info help page of the System Audio block", "Brings in everything the computer plays, from every program at once. You can leave some programs out.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the System Audio block", "Each program that plays a sound sends it to your speakers or headphones. This block takes a copy of all of it, mixed together, and starts a lane with it. The programs go on playing to their own device as before."),
            KI18n.i18nc("@info help page of the System Audio block", "A program that starts playing while the session runs is taken in as soon as it does. You do not need to start the programs first."),
            KI18n.i18nc("@info help page of the System Audio block", "Some sounds are always left out. Reverblabr's own sound is one, so that a recording is never made of itself. Sound that another program only passes on, such as the output of an echo canceller, is another, so that nothing is counted twice."),
            KI18n.i18nc("@info help page of the System Audio block", "Sound in more than two channels, such as a film in surround, is folded into the left and the right side."),
            KI18n.i18nc("@info help page of the System Audio block", "The tile on the canvas says All system audio, or how many programs are left out, such as All except 2 apps. While the session runs, point at the tile to see how many sounds it is taking.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check take the machine's notification sounds too", "Include alert sounds")
        paragraphs: [
            KI18n.i18nc("@info help page of the System Audio block", "Alert sounds are the short sounds the desktop and programs make to get your attention: a new message, a reminder, an error."),
            KI18n.i18nc("@info help page of the System Audio block", "They are left out of a new block, because a notification that arrives in the middle of a recording is rarely meant to be in it. Tick the box to take them as well.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@title:group the less usual settings", "Advanced")
        paragraphs: [
            KI18n.i18nc("@info help page of the System Audio block", "Click the heading to open the list of programs the block leaves out. It is empty for a new block, so everything is taken.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@action:button open the application picker", "Exclude source")
        paragraphs: [
            KI18n.i18nc("@info help page of the System Audio block", "Opens a list of the programs that are playing now, and of those seen recently. Choose one to leave its sound out. You can search the list, or type the name of a program that is not running: it is left out from the moment it starts."),
            KI18n.i18nc("@info help page of the System Audio block", "The programs left out are listed above the button. The button at the end of a row takes that program back in."),
            KI18n.i18nc("@info help page of the System Audio block", "The block remembers a program by what it is called, not by the sound it is playing now. A program you leave out stays out when it is closed and started again, and in the session the next time you open it.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the System Audio block", "You want to record or stream whatever the computer plays, whichever program plays it."),
            KI18n.i18nc("@info help page of the System Audio block", "You want everything except one program, such as the call you are in or a chat program that keeps chiming."),
            KI18n.i18nc("@info help page of the System Audio block", "You want to treat all the computer's sound at once, for example to even it out with an Auto Level block.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the System Audio block", "The programs still play to your speakers. If the lane ends in an Output Device on the same speakers, you hear everything twice. Send the lane somewhere else, or use it only to record."),
            KI18n.i18nc("@info help page of the System Audio block", "To take the sound of a single program, use an Application block instead. It is easier than leaving out everything else."),
            KI18n.i18nc("@info help page of the System Audio block", "Put a Peak/RMS meter after the block and play something, to check that the sound arrives.")
        ]
    }
}
