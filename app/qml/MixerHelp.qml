// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Mixer block's help page (docs/dev/block-help.md). The facts are the
// block's (engine/blocks/mixer.h and .cpp: five inputs, volumes up to
// 200 %, solo over mute, the balance turning the other side down, the
// ramps, no limiter) and the panel's (MixerPanel, MixerStrip: the fade
// times, Alt for a solo of one, the scenes and the second they take).
HelpPage {
    typeId: "advanced.mixer"
    summary: KI18n.i18nc("@info help page of the Mixer block", "Brings up to five sources together into one, each with its own volume, mute, solo and balance, and one master volume over them all.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Mixer block", "The block has five inputs on its tile and one output. Each input has a strip of its own in the settings, numbered 1 to 5 from the top, with the name of what is wired into it. A strip with nothing wired in says No source and is drawn faded; it costs nothing."),
            KI18n.i18nc("@info help page of the Mixer block", "What comes out is all the strips added together, each at its own volume, then turned up or down by the master."),
            KI18n.i18nc("@info help page of the Mixer block", "Every change glides rather than jumps, so nothing clicks: a slider in a fiftieth of a second, a fade in half a second or two."),
            KI18n.i18nc("@info help page of the Mixer block", "The block adds no delay.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@title:group the Mixer's five strips, one an input", "Inputs")
        paragraphs: [
            KI18n.i18nc("@info help page of the Mixer block", "Each strip has, from left to right:")
        ]
        points: [
            KI18n.i18nc("@info help page of the Mixer block, a control of a Mixer strip", "↘ 0 %: fades the input out to silence over half a second, or two seconds with Shift held."),
            KI18n.i18nc("@info help page of the Mixer block, a control of a Mixer strip", "The speaker button: mutes the input, and unmutes it when pressed again. The volume stays where it was."),
            KI18n.i18nc("@info help page of the Mixer block, a control of a Mixer strip", "The volume slider: from 0 % to 200 %, as a percentage of how loud the input arrives. A new block starts every strip at 100 %, which leaves the input as it is. The number beside it says where it is."),
            KI18n.i18nc("@info help page of the Mixer block, a control of a Mixer strip", "↗ 100 %: fades the input to 100 % over half a second, or two seconds with Shift held."),
            KI18n.i18nc("@info help page of the Mixer block, a control of a Mixer strip", "Solo: while any strip is soloed, you hear only the soloed ones. Solo several to hear them together. Hold Alt as you click to solo this one alone. A muted strip stays silent even when soloed."),
            KI18n.i18nc("@info help page of the Mixer block, a control of a Mixer strip", "The balance slider, between L and R: towards L turns the right side of the input down, towards R the left. In the middle, where a new block starts, both sides are as they arrive.")
        ]
        closing: [
            KI18n.i18nc("@info help page of the Mixer block", "In a narrow window a strip folds onto three lines, with the same controls.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider", "Master volume")
        paragraphs: [
            KI18n.i18nc("@info help page of the Mixer block", "The volume of everything together, from 0 % to 200 %. A new block starts at 100 %."),
            // i18n: "full scale" is the highest level digital sound can have; the sentence explains it.
            KI18n.i18nc("@info help page of the Mixer block", "Several sources added together can be louder than full scale, the loudest a digital signal can be, and the Mixer does not hold them back. If a meter after the block reaches the red, turn the master down.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@title:group fading every Mixer strip at once", "Fade all")
        covers: [
            KI18n.i18nc("@action:button fade every Mixer strip to silence", "Fade All to 0 %"),
            KI18n.i18nc("@action:button fade every Mixer strip up to its full level", "Fade All to 100 %")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Mixer block", "Fades every strip at once: Fade All to 0 % to silence, Fade All to 100 % to full. Each takes half a second, or two seconds with Shift held. The master stays where it is.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@title:group saved sets of the Mixer's volumes", "Scenes")
        paragraphs: [
            KI18n.i18nc("@info help page of the Mixer block", "A scene keeps the five strips' volumes, to bring them back later with one click. There are five, A to E."),
            KI18n.i18nc("@info help page of the Mixer block", "Save Scene, then a scene in its list, keeps the volumes as they are now. Click the scene's letter to bring them back: every strip glides to its saved volume over a second. A letter nobody has saved yet cannot be clicked."),
            KI18n.i18nc("@info help page of the Mixer block", "A scene keeps the volumes only, not the mute, the solo or the balance. Scenes are saved with the session and with a preset of the block.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Mixer block", "You stream or record several sources at once, such as a microphone, a game, music and a call, and want each at the right level."),
            KI18n.i18nc("@info help page of the Mixer block", "A show has parts, such as an intro, talking and music, each with its own balance of sources: save each as a scene."),
            KI18n.i18nc("@info help page of the Mixer block", "You need to hear one source alone for a moment, with Solo, without changing any volume.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Mixer block", "Give the blocks you wire in names of your own, so each strip says what it carries: select a block on the canvas and press F2."),
            KI18n.i18nc("@info help page of the Mixer block", "Put a Peak/RMS meter after the Mixer: with several sources together, the sum can reach the red even when each source alone does not."),
            KI18n.i18nc("@info help page of the Mixer block", "Turn a source down rather than the others up when you can. It leaves more room before the sum gets too loud."),
            KI18n.i18nc("@info help page of the Mixer block", "Ctrl+Z takes back a fade or a scene in one step.")
        ]
    }
}
