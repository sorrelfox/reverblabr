// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Hearing Compensation 16 block's help page (docs/dev/block-help.md).
// The facts are the block's (engine/blocks/hearingcompensation.h and .cpp:
// the ranges, the defaults and the limiter), the gain law's
// (engine/dsp/hearingbands.h: the soft, average and loud levels, the
// speeds, the noise floor, the fade-in), the fit's (engine/dsp/audiogram.h,
// engine/dsp/fittingrule.h, docs/fitting-rules.md) and the panel's
// (HearingCompensationPanel, HearingBands). docs/hearing.md is the
// user documentation it agrees with.
HelpPage {
    typeId: "hearing.compensation16"
    summary: KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "Makes quiet sounds easier to hear for one ear that hears some pitches faintly. It lifts soft sound a lot and loud sound only a little, so speech comes up without loud moments becoming too much.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "Many people with partial hearing loss hear low voices well and high voices poorly, or miss one range in the middle. Turning everything up does not help much: the parts you already hear become too loud before the faint parts are clear."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "This block splits the sound into sixteen bands, from deep sounds at 100 Hz to very high ones at 16 kHz, and gives each band its own amount of help. Hertz (Hz) count how high a sound is: a man's speaking voice is round 100 to 150 Hz, and the hiss of an s is above 4000 Hz, that is 4 kHz."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "In every band the help depends on how loud the sound is at that moment. Soft sound gets the most lift, loud sound the least. Each band also has a ceiling, a level nothing in that band goes above."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "It works on one ear only. The other side of the sound passes through unchanged, held back by exactly as long as the block takes, so both ears stay in step. The L (blue) or R (red) on the block's tile says which ear it is for; a new block is for the left ear."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "The block holds the sound back by about six thousandths of a second, far too little to notice."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "When the block starts, its lift comes in gently over one second, so the first sound is never a sudden jump in volume."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "At the very end a safety limiter is always at work. It keeps the sound under the Output ceiling, including the peaks that fall between the digital samples."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "A new block has every gain at 0 dB, so it changes nothing until you set it. Set it by hand, or let it start from your audiogram, the chart from a hearing test."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "Reverblabr is not a medical device. It shapes sound for listening and does not replace an audiologist or a hearing aid. If your hearing has changed, have it checked.")
        ]
    }

    HelpSection {
        title: BlockHelp.settingsTitle
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:radio a view of gain against frequency", "Gain")
        paragraphs: [
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "Shows how much each band lifts the sound, from low pitches on the left to high ones on the right. Up is more lift, in decibels (dB), from −20 dB to +60 dB. 0 dB leaves the sound as it is; every 6 dB more roughly doubles the strength of the sound."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "The four curves are the Soft, Average and Loud gains and the Ceiling, in the block's colour. The one you picked under the view choice has a handle on each band: drag a handle up or down to change that band. Click a handle to see the band's settings as numbers under the view."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "While the session runs, a dark line shows the lift each band is giving right now, moving between the Soft and Loud curves as the sound gets quieter or louder. The sound itself is drawn faintly behind it."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "When the session has a second block of this kind for the other ear, its Average curve is drawn as a faint dashed line, to compare the two ears.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:radio a view of levels against frequency", "Levels")
        paragraphs: [
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "Shows where the sound sits against this ear's hearing, from very quiet at the bottom to the loudest possible at the top."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "The thick dark line is the quietest level the ear hears, from the audiogram. The dashed line is each band's ceiling. While the session runs, the grey area is the sound coming in, and the coloured line the sound going out."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "The aim is to see the sound going out rise above the dark line, where the ear can hear it, and stay under the dashed one.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:radio the gain for soft sound", "Soft")
        paragraphs: [
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "The lift for soft sound in a band: a quiet voice, a whisper, the end of a word. Soft here means 15 dB quieter than ordinary speech."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "This is usually the largest of the three gains, because soft sound is what a weak ear misses first.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:radio the gain for sound of average loudness", "Average")
        paragraphs: [
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "The lift for sound as loud as ordinary conversation. It is the curve the view starts with, and the one to set first.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:radio the gain for loud sound", "Loud")
        paragraphs: [
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "The lift for loud sound in a band, 15 dB louder than ordinary speech: a raised voice, a door, a loud part of music. Keep it lower than Average, so loud sound does not become uncomfortable."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "Between those three levels the lift changes smoothly. Each gain can be set from −30 dB to +60 dB; a negative gain turns a band down, for a range that sounds too sharp.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:radio the highest level a band may put out", "Ceiling")
        paragraphs: [
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "The loudest a band may become, however much it is lifted. Set it just below the level where sound in that range starts to feel too loud for you."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "As a number it goes from 40 to 140, on the same scale as an audiogram, where ordinary speech is about 45. A new block starts at 100, which holds nothing back. On the Gain view the ceiling is drawn as the most lift a loud sound may have under it.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@action:button hold to hear the sound without the block", "Hold for before")
        paragraphs: [
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "Press and hold it to hear the sound without this block. Let go and the block is back. Use it to judge whether a change really helps.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how quickly the gain follows the sound", "Speed")
        paragraphs: [
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "How quickly the lift follows the sound as it gets louder or quieter.")
        ]
        points: [
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block, one choice of Speed", "Syllabic follows speech syllable by syllable: it reacts within five thousandths of a second and lets go within seven hundredths. It brings up soft consonants best."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block, one choice of Speed", "Slow follows the scene rather than each sound, letting go over a second and a half. It keeps the shape of music better."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block, one choice of Speed", "Adaptive works slowly most of the time, and switches to the quick way when a sound jumps 8 dB or more above what came before, such as a door slamming. It catches the sudden sound at once and is back soon after. A new block uses Adaptive.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label a gain over every band", "Loudness")
        paragraphs: [
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "Turns every band up or down together, from −30 dB to +30 dB, without changing the shape you set. Use it to set the overall level by ear. A new block starts at 0 dB.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label below this level soft sound is not lifted", "Noise floor")
        paragraphs: [
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "Below this level the lift fades away, a decibel less for every decibel quieter. It keeps the block from bringing up the hiss of a recording or the hum of a room in the pauses."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "It goes from −100 dB to −40 dB below full scale, the loudest a digital signal can be, and starts at −75 dB. If you hear hiss rise in the pauses, raise it a little.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label the most gain any band is given", "Most gain")
        paragraphs: [
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "The most lift any band is ever given, whatever the other settings ask for: from 0 dB to 60 dB, and 50 dB for a new block. It is a safety limit. Lower it if a band ever gets far too loud.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label the level nothing goes above", "Output ceiling")
        paragraphs: [
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "The safety limiter at the very end: nothing leaves the block louder than this. It goes from −24 dB to −1 dB below full scale, and starts at −1 dB."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "It cannot be switched off. Lower it to keep sudden sounds further from your ears, especially with headphones.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@action:button take the other ear's block's band settings", "Copy from the Other Ear")
        paragraphs: [
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "Takes every band's settings from the session's other Hearing Compensation 16 block, the one for the other ear, as a starting point. The button can only be pressed when there is such a block. One undo takes the copy back.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@title:group the quietest levels one ear hears, at each frequency", "Audiogram")
        paragraphs: [
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "An audiogram is the chart a hearing test gives you: for each pitch, the quietest level the ear heard. If you have one for this ear, type it in here and the block can set its bands from it. Click the heading to open it."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "It has a line for each of the eleven pitches a hearing test usually checks, from 125 Hz to 8000 Hz. On each line, choose what was found:")
        ]
        points: [
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block, one choice of an audiogram line", "The first choice, a dash, means nothing was measured at this pitch. The block works it out from the pitches either side."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block, one choice of an audiogram line", "Heard: type the quietest level heard into the box next to it."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block, one choice of an audiogram line", "No response: nothing was heard, even at the loudest level tried. Type that level into the box.")
        ]
        closing: [
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "The number boxes take levels from −140 to 140. The levels you type are kept with the block. They draw the dark line of the Levels view, and set the bands when you press Fit from Audiogram.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label what the audiogram's levels are measured in", "Measured in")
        paragraphs: [
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "What the audiogram's numbers mean. dB HL, decibels hearing level, is what a hearing test writes on its chart: 0 is what a young ear with good hearing just hears. Choose it for an audiogram from a test; a new block uses it."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "dBFS is for levels measured as digital levels through the very headphones you listen with. Such numbers mean nothing with other headphones.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check a 'too loud' level was measured at this frequency", "Too loud given")
        paragraphs: [
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "Tick it on a line where the test also found the level at which sound becomes too loud, and type that level in the box beside it. The fit then puts the ceiling there 5 dB below it. Where none is given, the fit takes 100 dB HL, a level most people find uncomfortable.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@action:button", "Fit from Audiogram")
        paragraphs: [
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "Sets every band's Soft, Average and Loud gains and its Ceiling from the audiogram, in one step, following a published fitting rule. One undo takes it all back."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "The lift for ordinary speech is never more than the ear has lost at that pitch, and no band gets more than 50 dB."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "A band may be marked with a warning sign: where the audiogram says no response, or where the ear would need more than 50 dB. More lift is unlikely to help there. Frequency Lowering can move that sound to a range the ear does hear."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "Treat the result as a starting point, and adjust by ear.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "You follow voices on television or in a video call less well than you used to."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "You can hear that someone is talking, but not make out the words."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "One ear hears worse than the other, and you want to help that ear on its own."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "You have an audiogram and want to listen with the sound shaped to it.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "Be careful with loud sound. Start with the volume low, especially with headphones, and raise it slowly."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "Start from your audiogram if you have one, then listen to speech you know well and change one thing at a time."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "Without an audiogram, start with the Average curve: lift the bands where voices sound dull or muffled, a few decibels at a time."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "If everything sounds too loud, turn Loudness down rather than lowering every band."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "For the other ear, add a second block and set it up for that ear. Copy from the Other Ear gives it a starting point."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "Put a Frequency Lowering or Raising block before this one, so the moved sound gets the lift of the range it lands in."),
            KI18n.i18nc("@info help page of the Hearing Compensation 16 block", "When sixteen bands are not fine enough for a steep slope or a narrow dip in your hearing, try Hearing Compensation 64.")
        ]
    }
}
