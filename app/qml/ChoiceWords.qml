// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma Singleton

import QtQuick
import org.kde.ki18n

// **The words for every choice a built-in block offers**, keyed by the
// block's type, the parameter and the choice's stable id: what a ParamRow
// shows in its list, in the generic panel and in a block's own panel alike.
//
// The catalogue holds ids, which a session file keeps for ever; the words
// are the interface's, translated. A block's own words object is asked
// first where it has one (CompressorWords, FilterWords, RecorderWords…), so
// a choice reads the same in its list as on its face. A plugin's choices
// come with the plugin's own words and do not come here.
//
// A choice with no words here comes back as its id, and tst_choicewords
// fails for it: every choice in the catalogue has words (hasWords()).
QtObject {
    id: words

    readonly property CompressorWords compressor: CompressorWords {}
    readonly property ChannelsWords channels: ChannelsWords {}
    readonly property FilterWords filter: FilterWords {}
    readonly property WaveWords wave: WaveWords {}
    readonly property RecorderWords recorder: RecorderWords {}
    readonly property BroadcastWords broadcast: BroadcastWords {}
    readonly property LiveStreamWords liveStream: LiveStreamWords {}

    /// The words for each of \a descriptor's choices, in its order: what a
    /// list of them shows. \a typeId is the block's type.
    function labels(typeId: string, descriptor: var): var {
        const paramId = descriptor?.id ?? "";
        return (descriptor?.choices ?? []).map(choice => words.label(typeId, paramId, String(choice)));
    }

    /// The words for choice \a choice of \a typeId's parameter \a paramId,
    /// or the id where there are none.
    function label(typeId: string, paramId: string, choice: string): string {
        const named = words.named(typeId, words.kindOf(paramId), choice);
        return named !== "" ? named : choice;
    }

    /// Whether there are words for that choice. In English a few are the
    /// id itself ("minutes"), so this is what says none are missing.
    function hasWords(typeId: string, paramId: string, choice: string): bool {
        return words.named(typeId, words.kindOf(paramId), choice) !== "";
    }

    /// A numbered parameter is one of a row of the same kind: a band's
    /// type, a move's method, an audiogram's mark at a frequency.
    function kindOf(paramId: string): string {
        return paramId.replace(/[0-9]+/g, "");
    }

    /// Which ear: the same two words on every hearing block.
    function earName(choice: string): string {
        switch (choice) {
        case "left":
            return KI18n.i18nc("@option:radio a hearing block works on the left ear", "Left");
        case "right":
            return KI18n.i18nc("@option:radio a hearing block works on the right ear", "Right");
        }
        return "";
    }

    function named(typeId: string, kind: string, choice: string): string {
        let found = "";
        switch (kind) {
        case "ear":
            return words.earName(choice);
        case "speed":
            return words.hearingSpeedName(choice);
        case "audiogramUnit":
            return words.audiogramUnitName(choice);
        case "audiogram":
            return words.audiogramMarkName(choice);
        case "moveMethod":
            return words.moveMethodName(typeId, choice);
        case "moveSource":
            return words.moveSourceName(choice);
        case "moveWhen":
            return words.moveWhenName(typeId, choice);
        case "mode":
            found = words.compressor.modeName(choice);
            break;
        case "detector":
            found = words.compressor.detectorName(choice);
            break;
        case "action":
            found = words.channels.actionName(choice);
            break;
        case "slope":
            found = words.filter.slopeName(choice);
            break;
        case "bandType":
            found = words.filter.bandTypeName(choice);
            break;
        case "waveform":
            found = words.wave.shapeName(choice);
            break;
        case "format":
            found = typeId === "output.broadcast" ? words.broadcast.formatName(choice) : words.recorder.formatName(choice);
            break;
        case "sampleFormat":
            found = words.recorder.sampleFormatName(choice);
            break;
        case "sampleRate":
            found = words.recorder.sampleRateName(choice, AppShell.sampleRate);
            break;
        case "channels":
            found = typeId === "output.wave" ? words.wave.sideName(choice) : words.recorder.channelsName(choice);
            break;
        case "server":
            found = words.broadcast.serverName(choice);
            break;
        case "nowPlaying":
            found = words.broadcast.nowPlayingName(choice);
            break;
        case "quality":
            found = words.liveStream.qualityName(choice);
            break;
        case "encoder":
            found = words.liveStream.encoderName(choice);
            break;
        case "visualisation":
            found = words.liveStream.visualisationName(choice);
            break;
        default:
            return words.otherName(kind, choice);
        }
        return found === choice ? "" : found;
    }

    function hearingSpeedName(choice: string): string {
        switch (choice) {
        case "syllabic":
            return KI18n.i18nc("@item the gain follows speech syllable by syllable", "Syllabic");
        case "slow":
            return KI18n.i18nc("@item the gain follows the sound slowly, keeping the shape of music", "Slow");
        case "adaptive":
            return KI18n.i18nc("@item the gain follows slowly but catches a sudden loud sound at once", "Adaptive");
        }
        return "";
    }

    function audiogramUnitName(choice: string): string {
        switch (choice) {
        case "dbHl":
            return KI18n.i18nc("@item decibels hearing level, the unit of a hearing test's chart", "dB HL");
        case "dbFs":
            return KI18n.i18nc("@item decibels below full scale, levels measured in this program", "dBFS");
        }
        return "";
    }

    /// What was found at one frequency of an audiogram.
    function audiogramMarkName(choice: string): string {
        switch (choice) {
        case "none":
            return KI18n.i18nc("@item:inlistbox nothing measured at this frequency", "—");
        case "heard":
            return KI18n.i18nc("@item:inlistbox heard at the level given", "Heard");
        case "noResponse":
            return KI18n.i18nc("@item:inlistbox nothing heard even at the loudest level tried", "No response");
        }
        return "";
    }

    function moveMethodName(typeId: string, choice: string): string {
        switch (choice) {
        case "compress":
            return KI18n.i18nc("@option:radio squeeze the source range into the destination", "Squeeze");
        case "transpose":
            return typeId === "hearing.frequencyraising"
                ? KI18n.i18nc("@option:radio copy the source range up into the destination", "Copy up")
                : KI18n.i18nc("@option:radio copy the source range down into the destination", "Copy down");
        case "harmonics":
            return KI18n.i18nc("@option:radio give each sound of the source range its harmonics in the destination, keeping its pitch",
                               "Add harmonics");
        }
        return "";
    }

    function moveSourceName(choice: string): string {
        switch (choice) {
        case "keep":
            return KI18n.i18nc("@option:radio leave the source range as it was", "As it was");
        case "lower":
            return KI18n.i18nc("@option:radio make the source range quieter", "Quieter");
        case "remove":
            return KI18n.i18nc("@option:radio take the sound out of the source range", "Empty");
        }
        return "";
    }

    function moveWhenName(typeId: string, choice: string): string {
        switch (choice) {
        case "always":
            return KI18n.i18nc("@option:radio a Frequency Lowering or Raising move acts all the time", "Always");
        case "needed":
            return typeId === "hearing.frequencyraising"
                ? KI18n.i18nc("@option:radio a Frequency Raising move acts only while its low range is louder than what is above it, as with a voice or a hum",
                              "On low sounds")
                : KI18n.i18nc("@option:radio a Frequency Lowering move acts only on hissing sounds such as s and f",
                              "On s, sh, f");
        }
        return "";
    }

    /// The choices no words object of a block's has words for.
    function otherName(kind: string, choice: string): string {
        switch (kind + "/" + choice) {
        case "input/a":
            return KI18n.i18nc("@item the Input Switch's first input", "A");
        case "input/b":
            return KI18n.i18nc("@item the Input Switch's second input", "B");
        case "overdrive/off":
            return KI18n.i18nc("@option:check the volume slider stops at 100 %", "Off");
        case "overdrive/2x":
            return KI18n.i18nc("@option:check the volume slider goes up to 200 %", "2×");
        case "overdrive/3x":
            return KI18n.i18nc("@option:check the volume slider goes up to 300 %", "3×");
        case "overdrive/4x":
            return KI18n.i18nc("@option:check the volume slider goes up to 400 %", "4×");
        case "peakHold/off":
            return KI18n.i18nc("@item:inlistbox the peak line does not wait at all", "Off");
        case "peakHold/1s":
            return KI18n.i18nc("@item:inlistbox how long the peak line waits", "1 second");
        case "peakHold/2s":
            return KI18n.i18nc("@item:inlistbox how long the peak line waits", "2 seconds");
        case "peakHold/5s":
            return KI18n.i18nc("@item:inlistbox how long the peak line waits", "5 seconds");
        case "peakHold/forever":
            return KI18n.i18nc("@item:inlistbox the peak line never comes down", "Until reset");
        case "reference/-20":
        case "reference/-18":
        case "reference/-14":
            return KI18n.i18nc("@item the level in decibels below full scale that reads 0 VU", "%1 dBFS",
                               Number(VuScale.referenceDbOf(choice)).toLocaleString(Qt.locale(), 'f', 0));
        case "splitUnit/minutes":
        case "endUnit/minutes":
            return KI18n.i18nc("@item a unit", "minutes");
        case "splitUnit/hours":
        case "endUnit/hours":
            return KI18n.i18nc("@item a unit", "hours");
        case "splitUnit/megabytes":
            return KI18n.i18nc("@item a unit", "MB");
        case "silenceAction/remove":
            return KI18n.i18nc("@item what silence does to a recording", "Remove silence");
        case "silenceAction/split":
            return KI18n.i18nc("@item what silence does to a recording", "Split on silence");
        case "silenceAction/stop":
            return KI18n.i18nc("@item what silence does to a recording", "Stop on silence");
        case "bitrateMode/vbr":
            return KI18n.i18nc("@action:button", "Variable bit rate");
        case "bitrateMode/cbr":
            return KI18n.i18nc("@action:button", "Constant bit rate");
        case "monoMode/dualMono":
            return KI18n.i18nc("@item:inlistbox a mono plugin on a stereo lane runs once a side", "Once a side (dual mono)");
        case "monoMode/downmix":
            return KI18n.i18nc("@item:inlistbox a mono plugin on a stereo lane hears the two sides mixed",
                               "On the sides mixed (downmix)");
        case "outputMode/default":
            return KI18n.i18nc("@option:radio capture what the application plays to the usual device",
                               "Capture audio played to the default output device");
        case "outputMode/any":
            return KI18n.i18nc("@option:radio capture everything the application plays",
                               "Capture audio played to all output devices");
        case "outputMode/chosen":
            return KI18n.i18nc("@option:radio capture only what goes to certain devices",
                               "Capture audio played to the specified devices");
        }
        return "";
    }
}
