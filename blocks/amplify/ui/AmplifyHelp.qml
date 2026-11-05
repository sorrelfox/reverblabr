// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Amplify block's help page (docs/dev/block-help.md). The facts are the
// block's (blocks/amplify/amplify.h: a gain up to ten times, smoothed over
// 20 ms, no limiter, no delay) and AmplifyPanel's.
HelpPage {
    typeId: "effect.amplify"
    summary: KI18n.i18nc("@info help page of the Amplify block", "Makes the sound louder, up to ten times, or quieter. It does that and nothing else.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Amplify block", "Everything that reaches this block leaves it multiplied by the gain you set. At 100 % the sound goes on exactly as it arrived; 200 % doubles its strength, 1000 % makes it ten times as strong, and 0 % is silence."),
            KI18n.i18nc("@info help page of the Amplify block", "A change glides in over a fiftieth of a second, so moving the slider never makes a click. The block adds no delay."),
            // i18n: "full scale" is the highest level digital sound can have.
            KI18n.i18nc("@info help page of the Amplify block", "Unlike Volume, it has no limiter. Sound made louder than full scale, the loudest a digital signal can be, goes on that loud, and wherever it is played or recorded its tops are cut off and it crackles. Use it for sound that is too quiet, and watch a meter after it.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider how much an Amplify block multiplies the sound by", "Gain")
        paragraphs: [
            KI18n.i18nc("@info help page of the Amplify block", "How strong the sound leaves the block, as a percentage of how it arrived, from 0 to 1000 %. Drag the slider here or on the block's tile. A new block starts at 100 %.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Amplify block", "A microphone, a record player or an old recording is far too quiet, even at Volume's 400 %."),
            KI18n.i18nc("@info help page of the Amplify block", "You want a plain gain with no limiter shaping the loudest moments. For turning a source up safely, use Volume.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Amplify block", "Put a Peak/RMS meter after the block. If it reaches the top, turn the gain down until it no longer does."),
            KI18n.i18nc("@info help page of the Amplify block", "To amplify and still be safe from clipping, put a Limiter after it.")
        ]
    }
}
