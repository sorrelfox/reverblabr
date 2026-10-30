// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Volume block's help page (docs/dev/block-help.md). The facts are the
// block's: engine/blocks/volume.h and the limiter it keeps in its path,
// engine/dsp/limiter.h; the settings are VolumePanel's.
HelpPage {
    typeId: "effect.volume"
    summary: KI18n.i18nc("@info help page of the Volume block", "Makes the sound that passes through it quieter, or louder. Use it to match one source to another, without touching the program the sound comes from.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Volume block", "Everything that reaches this block leaves it at the volume you set. At 100 % the sound goes on exactly as it arrived. Lower numbers make it quieter, and 0 % is silence."),
            KI18n.i18nc("@info help page of the Volume block", "At 50 % the signal has half its strength. It sounds clearly quieter, though to the ear it is not half as loud."),
            KI18n.i18nc("@info help page of the Volume block", "A change glides in over a fiftieth of a second, so moving the slider never makes a click."),
            // i18n: "full scale" is the highest level digital sound can have; the sentence after explains it.
            KI18n.i18nc("@info help page of the Volume block", "At the end of the block a safety limiter is always at work. It keeps the sound from going higher than 1 dB below full scale."),
            KI18n.i18nc("@info help page of the Volume block", "Full scale is the loudest a digital signal can be. Sound pushed past it is clipped: the tops of its waves are cut off, and it crackles. The limiter turns the loudest moments down just enough instead."),
            KI18n.i18nc("@info help page of the Volume block", "While the sound stays below that level, the limiter changes nothing. It only holds the sound back by one and a half thousandths of a second, far too little to hear.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider", "Volume")
        paragraphs: [
            KI18n.i18nc("@info help page of the Volume block", "How loud the sound leaves the block, as a percentage of how loud it arrived. Drag the slider here or on the block's tile; the number shows where it is."),
            KI18n.i18nc("@info help page of the Volume block", "A new block starts at 100 %. Without Overdrive the slider stops at 100 %, so it can only turn the sound down.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@action:button silence this block", "Mute")
        paragraphs: [
            KI18n.i18nc("@info help page of the Volume block", "Silences the block without moving the slider. Press it again and the sound comes back at the volume the slider shows."),
            KI18n.i18nc("@info help page of the Volume block", "The sound fades out and in over a hundredth of a second, so muting in the middle of a word makes no click."),
            KI18n.i18nc("@info help page of the Volume block", "The slider still moves while the block is muted. Use it to set the level the sound will come back at.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how far above 100 % the volume slider goes", "Overdrive")
        paragraphs: [
            KI18n.i18nc("@info help page of the Volume block", "Lets the slider go past 100 %, so the block can make the sound louder than it arrived. 2× takes the top of the slider to 200 %, 3× to 300 % and 4× to 400 %. Off keeps it at 100 %."),
            KI18n.i18nc("@info help page of the Volume block", "Choosing an overdrive does not change what you hear. It only gives the slider more room."),
            KI18n.i18nc("@info help page of the Volume block", "If you choose a smaller overdrive while the volume is above its top, the volume comes down to the new top."),
            KI18n.i18nc("@info help page of the Volume block", "Sound made louder can reach the safety limiter. The loudest moments are then held down rather than clipped, which can make loud music sound flatter.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Volume block", "One program is much louder than another in the same session."),
            KI18n.i18nc("@info help page of the Volume block", "A quiet microphone needs to be louder before it reaches a recording or a call. Turn on Overdrive for that."),
            KI18n.i18nc("@info help page of the Volume block", "You want to silence one lane for a moment, with Mute, while the rest of the session carries on.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Volume block", "Put a Peak/RMS meter after the block to see what your change did."),
            KI18n.i18nc("@info help page of the Volume block", "When you turn a quiet source up, go a step at a time. If the meter after it keeps reaching the red part at the top, back off a little."),
            KI18n.i18nc("@info help page of the Volume block", "Ctrl+Z takes back a change of the volume, like any other change.")
        ]
    }
}
