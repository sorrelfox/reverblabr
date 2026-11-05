// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Sound Side block's help page (docs/dev/block-help.md). The facts are
// the block's (blocks/soundside/soundside.h and .cpp), the words' and the
// panel's.
HelpPage {
    typeId: "meter.soundside"
    summary: KI18n.i18nc("@info help page of the Sound Side block", "Shows which channel is louder, with a moving mark and a word. It can't tell what made the sound, and it can't tell front from back.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Sound Side block", "The block compares the left and right channels of this lane over the last 20 milliseconds and passes the sound on exactly as it arrived, with no delay. When one side is louder, a mark jumps there and a word says Left or Right. The mark stays for a quarter of a second and fades over another. When nothing is over the gate there is no mark and the word says Quiet, so silence never reads as a sound straight ahead."),
            KI18n.i18nc("@info help page of the Sound Side block", "Centre means near-equal sides that match, as one sound straight ahead gives. Both means near-equal sides that do not match, as a wide bed of music gives: no one place, not two sounds. Reverberation does this too."),
            KI18n.i18nc("@info help page of the Sound Side block", "A loud bed hides a quiet sound, and no setting changes that. A sound on one side alone leaves Centre only once it comes within about 2 dB of one channel of the bed. A short click is thinned out further. A game's headphone mode may narrow the sides too, since it keeps much of its difference in time, which this block can't see."),
            KI18n.i18nc("@info help page of the Sound Side block", "Sound Side reads this lane's sound and does not touch the game. Some games and tournaments still forbid tools that show direction. Check their rules before you use it. That choice is yours.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label which side of the mix is louder now", "Side now")
        covers: [
            KI18n.i18nc("@label what a Sound Side says now", "Now"),
            KI18n.i18nc("@action:button", "Move Before One Ear Mix")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Sound Side block", "The strip at the card's width, with how far the sound is to one side. 25 % is about 2 dB between the sides, 50 % about 5 dB, 80 % about 10 dB and 90 % about 13 dB. Sides alike means both channels have carried the same sound for 5 seconds. Sound on one side only means it has stayed at one end for 10 seconds, which a One Ear Mix before this block does. With a One Ear Mix before this block, the card names it and Move Before One Ear Mix puts this block ahead of it, as one step of undo.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how loud this lane is", "Level on this lane")
        covers: [
            KI18n.i18nc("@label under this level a Sound Side shows no side", "Level gate")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Sound Side block", "The level of the two channels together, with a line at the gate. Under Level gate there is no side to show. It goes from -70 to -20 dBFS and starts at -50.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label which part of the sound a Sound Side reads", "Band")
        covers: [
            KI18n.i18nc("@item:inlistbox the band a Sound Side reads", "All"),
            KI18n.i18nc("@item:inlistbox the band a Sound Side reads", "Range"),
            KI18n.i18nc("@label the bottom of the band a Sound Side reads", "From"),
            KI18n.i18nc("@label the top of the band a Sound Side reads", "To")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Sound Side block", "All compares everything on the lane. Range compares only what lies between From and To, for the comparison and not for the sound, which is untouched. It can help where a bed is weak in that range, and nothing shows that it does. To stays at least an octave above From: moving one past that pushes the other along.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check", "Strip on screen")
        paragraphs: [
            KI18n.i18nc("@info help page of the Sound Side block", "A thin dark strip over other programs, with L at one end, R at the other and the word in a box beside them. It takes no clicks and no focus, and touches nothing of the game's: no hook, no layer, nothing loaded into it. It goes with this window and the tray icon. There is one strip, so with several blocks the first has it. The tray's popover says when it is on."),
            KI18n.i18nc("@info help page of the Sound Side block", "On a Plasma Wayland session it sits on the desktop's overlay layer. Nobody has tried it above a full-screen game. Elsewhere it is a plain window that may go under a game, and the card says so. With a game that takes over the whole display there is no desktop to draw it. Then detach the card to a second screen: Side now is the same strip at the same speed.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how big the strip on screen is", "Strip size")
        covers: [
            KI18n.i18nc("@item:inlistbox a size of strip", "Small"),
            KI18n.i18nc("@item:inlistbox a size of strip", "Medium"),
            KI18n.i18nc("@item:inlistbox a size of strip", "Large")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Sound Side block", "How big the strip is. This and the rest of the strip's place belong to this desktop, not to a session.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label what the strip on screen draws", "Show")
        covers: [
            KI18n.i18nc("@item:inlistbox what the strip draws", "Mark"),
            KI18n.i18nc("@item:inlistbox what the strip draws", "Bars")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Sound Side block", "Mark moves to the louder side and opens out as the sound loses its one place. Bars grow outward from the middle, one a side, from the gate to full scale: they show the level as well, and need more looking.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@action:button", "Place Strip…")
        covers: [
            KI18n.i18nc("@action:button", "Test Strip"),
            KI18n.i18nc("@title:group a part of the Sound Side card", "Placement"),
            KI18n.i18nc("@label where the strip sits across the screen", "Across"),
            KI18n.i18nc("@label where the strip sits down the screen", "Down"),
            KI18n.i18nc("@label which screen the floating label is on", "Screen")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Sound Side block", "Place Strip gives the strip a frame and the keyboard for as long as it lasts. The arrows move it 10 pixels, Page Up, Page Down, Home and End move it 50, and the mouse works too. Enter keeps the place and Esc puts it back. Do this before playing, since it takes the keyboard from whatever has it."),
            KI18n.i18nc("@info help page of the Sound Side block", "Test Strip runs the mark left, centre and right over 3 seconds, on the strip alone. The card can't know whether the strip is hidden, so this is how to see that it is there. Across and Down are percentages of the room left on the screen, so that a place survives a change of screen: 50 is the middle.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Sound Side block", "You play with the sound in one ear, and a mix in one ear has lost its sides."),
            KI18n.i18nc("@info help page of the Sound Side block", "A program has no display of its own for where a sound is. The program's own display is better where there is one, as it knows what made each sound.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Sound Side block", "Put it after an Application block that holds only the game, and before One Ear Mix. After a fold there is nothing left to see, and the card offers to move it. Voice chat and a music player stay out of the reading on a lane of their own."),
            KI18n.i18nc("@info help page of the Sound Side block", "Turn off the game's own mono option and use One Ear Mix after this block instead. With the option on, both sides are the same and the strip says Same."),
            KI18n.i18nc("@info help page of the Sound Side block", "Set a key for Show or hide Sound Side's strip in System Settings. It starts with none. Each time it shows the strip it runs the test once, so that you see during play whether it is there."),
            KI18n.i18nc("@info help page of the Sound Side block", "Over Bluetooth the mark comes before the sound.")
        ]
    }
}
