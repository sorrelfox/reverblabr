// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Input Switch block's help page (docs/dev/block-help.md). The facts
// are the block's (engine/blocks/inputswitch.h: the two inputs, the
// equal-power crossfade of half a second or two, turning round half way),
// the panel's and the tile's (InputSwitchPanel, InputSwitchControl), and
// the shortcuts and the command line (docs/desktop.md, docs/ctl.md).
HelpPage {
    typeId: "advanced.inputswitch"
    summary: KI18n.i18nc("@info help page of the Input Switch block", "Lets one of two sources through, A or B, and fades smoothly from one to the other when you switch.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Input Switch block", "The block has two inputs on its tile, A and B, and one output. Wire a source into each. Only one of them is heard at a time; a new block lets A through."),
            KI18n.i18nc("@info help page of the Input Switch block", "When you switch, the block does not cut from one to the other. It fades the one out while it fades the other in, over half a second, or two seconds with Shift held. The two are balanced so that the sound stays as loud all the way across, with no dip in the middle."),
            KI18n.i18nc("@info help page of the Input Switch block", "Switch back before a fade has finished, and it turns round where it is and goes back the way it came."),
            KI18n.i18nc("@info help page of the Input Switch block", "The block adds no delay.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@title:group the Input Switch's two inputs and the switch between them", "Which input passes")
        paragraphs: [
            KI18n.i18nc("@info help page of the Input Switch block", "The two inputs are named one above the other, and the knob of the switch beside them sits next to the one that passes. The line beside them says which one you hear."),
            KI18n.i18nc("@info help page of the Input Switch block", "Click the switch, or press Space while it has the focus, to fade over to the other input in half a second. Hold Shift as you do to fade over two seconds instead. While the session runs, the knob moves along with the fade."),
            KI18n.i18nc("@info help page of the Input Switch block", "The block's tile has an A and a B button too: click one to fade over to it, with Shift for two seconds.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Input Switch block", "You want to go back and forth between two microphones, or between a microphone and music."),
            KI18n.i18nc("@info help page of the Input Switch block", "You compare two versions of the same sound, such as before and after an effect, and want to hear the difference at a click."),
            KI18n.i18nc("@info help page of the Input Switch block", "A show needs a smooth change from one source to another, without stopping either.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Input Switch block", "Give it a global shortcut to switch without looking at the window: Input Switch: play A and play B, in System Settings, under Keyboard, Shortcuts, Reverblabr. They switch every Input Switch in the session the main window shows."),
            KI18n.i18nc("@info help page of the Input Switch block", "To switch from a script, use the command line: reverblabr-ctl switch, then the session's name and A or B."),
            KI18n.i18nc("@info help page of the Input Switch block", "For a fair comparison of two sources, make them about as loud as each other first, with a Volume block before one of them."),
            KI18n.i18nc("@info help page of the Input Switch block", "Ctrl+Z takes a switch back, like any other change.")
        ]
    }
}
