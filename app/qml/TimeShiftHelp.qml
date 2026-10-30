// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Time Shift block's help page (docs/dev/block-help.md). The facts are
// the block's (engine/blocks/timeshift.h: what is kept, ten minutes in
// memory and the rest on disk, the 10 ms fades, live passing straight
// through) and the panel's (TimeShiftPanel, TimeShiftState), with the
// shortcuts of docs/desktop.md.
HelpPage {
    typeId: "advanced.timeshift"
    summary: KI18n.i18nc("@info help page of the Time Shift block", "Lets you pause and rewind sound that is still arriving, such as a radio stream or a call, and catch up with it again.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Time Shift block", "The block keeps everything that reaches it while the session runs, up to the number of minutes you choose. What you hear after it is played from what it has kept, so you can pause, go back and listen again, while the sound goes on arriving and being kept."),
            KI18n.i18nc("@info help page of the Time Shift block", "At live, the sound goes straight through, with no delay at all. Once you pause or go back, you hear it later than it arrives, and everything after the block hears it that late too."),
            KI18n.i18nc("@info help page of the Time Shift block", "Every pause, jump and return to live fades over a hundredth of a second, so nothing clicks."),
            KI18n.i18nc("@info help page of the Time Shift block", "Up to ten minutes are kept in memory. A longer history is kept in a file in your cache folder instead, with only its last minute in memory as well. The file is gone when the session stops, and nothing kept is saved with the session."),
            KI18n.i18nc("@info help page of the Time Shift block", "The controls need the session running, since there is nothing kept until it starts.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider", "Position")
        paragraphs: [
            KI18n.i18nc("@info help page of the Time Shift block", "Where you are in what has been kept: the oldest sound at the left, live at the right. The number beside it says how far behind live you are, or Live."),
            KI18n.i18nc("@info help page of the Time Shift block", "Drag it and let go to jump there. While paused, it moves left by itself, as live moves away from you.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@title:group the Time Shift's play, pause, jump and live buttons", "Playback")
        covers: [
            KI18n.i18nc("@action:button pause a Time Shift", "Pause"),
            KI18n.i18nc("@action:button play a paused Time Shift", "Play"),
            KI18n.i18nc("@action:button back to what is arriving now, and playing", "Live")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Time Shift block", "The row of buttons under the slider:")
        ]
        points: [
            KI18n.i18nc("@info help page of the Time Shift block, a button of the Time Shift", "30 s, 10 s and 3 s on the left jump back that many seconds, as far as the oldest sound kept."),
            KI18n.i18nc("@info help page of the Time Shift block, a button of the Time Shift", "The button in the middle pauses: the sound fades out and waits while the arrival goes on being kept. Press it again to play on from where you paused."),
            KI18n.i18nc("@info help page of the Time Shift block, a button of the Time Shift", "3 s, 10 s and 30 s on the right jump forward that many seconds. A jump past live lands at live. They are unavailable while you are at live."),
            KI18n.i18nc("@info help page of the Time Shift block, a button of the Time Shift", "Live goes straight back to what is arriving now, and plays it.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:spinbox", "Minutes to keep")
        paragraphs: [
            KI18n.i18nc("@info help page of the Time Shift block", "How far back you can go, from 1 to 60 minutes. A new block keeps 5. Up to 10 minutes stay in memory; more uses a file on disk, which needs room: a stereo hour at 48 kHz is about 1.4 GB.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Time Shift block", "The doorbell rings during a radio programme, and you want to hear the rest when you come back."),
            KI18n.i18nc("@info help page of the Time Shift block", "You missed what somebody said in a call or a live stream, and want to hear it again."),
            KI18n.i18nc("@info help page of the Time Shift block", "You are writing down what you hear, and need to stop and go back now and then.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Time Shift block", "Put the block just before your speakers, so that a Recorder before it keeps everything as it arrived, whatever you pause."),
            KI18n.i18nc("@info help page of the Time Shift block", "Give it global shortcuts to pause and jump without looking at the window: Time Shift: pause or play, back 10 seconds, forward 10 seconds and back to live, in System Settings, under Keyboard, Shortcuts, Reverblabr."),
            KI18n.i18nc("@info help page of the Time Shift block", "Keep only as many minutes as you need: a short history stays in memory and never touches the disk.")
        ]
    }
}
