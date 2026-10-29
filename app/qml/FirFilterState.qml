// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// What a FIR Filter block is using, worked out once for its face and
// its panel: the file it was given, and — while the session runs — what the
// block made of it: how many channels, how long, or that it could not use
// it.
//
// **The length is the block's**, published while it runs: the response at
// the session's rate, after trimming, which is what is being heard. While
// it does not run only the file's name is known.
QtObject {
    id: reading

    /// The block this is about.
    required property string blockId

    /// Which of the block's published values is which (FirFilter::kShapeValue,
    /// kTapsValue, kSecondsValue, kDelayMsValue), and what the shape's
    /// numbers mean: the channels, 0 for no file, -1 for one it cannot use.
    readonly property int shapeValue: 0
    readonly property int tapsValue: 1
    readonly property int secondsValue: 2
    readonly property int delayValue: 3
    /// The latency in samples, which is the same at every rate.
    readonly property int latencySamples: 256

    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(reading.blockId) : ({})
    /// The path as the session holds it, a leading ~ for the home folder.
    readonly property string file: reading.params.file ?? ""
    readonly property string fileName: reading.file.substring(reading.file.lastIndexOf("/") + 1)
    readonly property var levels: Meters.frame >= 0 ? Meters.levels(reading.blockId) : ({})
    readonly property var values: reading.levels.values ?? []
    /// Whether the running block is publishing.
    readonly property bool live: Meters.live && reading.values.length > reading.delayValue

    readonly property int shape: reading.live ? Math.round(reading.values[reading.shapeValue]) : 0
    readonly property bool loaded: reading.shape > 0
    readonly property bool unusable: reading.live && reading.file !== "" && reading.shape < 0
    readonly property int taps: reading.loaded ? Math.round(reading.values[reading.tapsValue]) : 0
    readonly property real seconds: reading.loaded ? reading.values[reading.secondsValue] : 0
    readonly property real delayMs: reading.live ? reading.values[reading.delayValue] : 0

    /// The response's channels in a word.
    readonly property string shapeName: reading.shape === 1
        ? KI18n.i18nc("@info an impulse response with one channel, used on both sides", "Mono")
        : (reading.shape === 2 ? KI18n.i18nc("@info an impulse response with a channel for each side", "Stereo")
                               : KI18n.i18nc("@info an impulse response with four channels, each side to each side",
                                             "True stereo"))

    /// The file's name, or that there is none.
    readonly property string headline: reading.file !== ""
        ? reading.fileName
        : KI18n.i18nc("@info a FIR Filter block with no file chosen", "No impulse response")
    /// The line under it: how long the response is, or why there is none.
    readonly property string detail: {
        if (reading.file === "") {
            return KI18n.i18nc("@info a FIR Filter block with no file passes the sound through", "Passes sound through");
        }
        if (reading.unusable) {
            return KI18n.i18nc("@info a FIR Filter block's file is missing or not an impulse response",
                               "Cannot use this file");
        }
        if (reading.loaded) {
            return KI18n.i18ncp("@info how long a FIR Filter block's impulse response is, '96,000 taps'",
                                "%1 tap", "%1 taps", reading.taps);
        }
        return KI18n.i18nc("@info a FIR Filter block's file, while the session is not running", "Impulse response");
    }
    /// All of it, for the panel: "Stereo, 2.00 s, 96,000 taps".
    readonly property string description: reading.loaded
        ? KI18n.i18ncp("@info a FIR Filter block's impulse response: its channels, its length in seconds and in taps",
                       "%2, %3 s, %1 tap", "%2, %3 s, %1 taps", reading.taps, reading.shapeName,
                       Number(reading.seconds).toLocaleString(Qt.locale(), 'f', 2))
        : (reading.unusable
           ? KI18n.i18nc("@info why a FIR Filter block cannot use a file",
                         "The file is missing, is not audio, or has other than one, two or four channels. The sound passes through unchanged.")
           : "")

    /// \a url, a file the picker chose, as the setting holds it.
    function choose(url: url) {
        SessionModel.setParam(reading.blockId, "file", RecordingStatus.folderFromUrl(url));
    }

    function remove() {
        SessionModel.setParam(reading.blockId, "file", "");
    }
}
