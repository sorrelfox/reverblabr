// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The words the Live Stream block's panel uses: its qualities, encoders and
// pictures, and what its stream is doing. **What a stream does is said as a
// Broadcast block's is** (BroadcastWords), but for its key, which is not a
// password. tst_livestream.qml holds the lists here against the engine's.
QtObject {
    id: words

    readonly property var qualities: ["good", "higher", "best"]
    readonly property var encoders: ["auto", "vaapi", "nvenc", "x264", "openh264"]
    readonly property var visualisations: ["none", "waveform", "spectrum", "meter"]
    readonly property string defaultTitle: "%session"

    readonly property BroadcastWords stream: BroadcastWords {}

    function qualityName(id: string): string {
        switch (id) {
        case "good":
            return KI18n.i18nc("@item a live stream's quality", "Good: 1024 × 576, 15 frames a second");
        case "higher":
            return KI18n.i18nc("@item a live stream's quality", "Higher: 1280 × 720, 30 frames a second");
        case "best":
            return KI18n.i18nc("@item a live stream's quality", "Best: 1920 × 1080, 30 frames a second");
        }
        return id;
    }

    function encoderName(id: string): string {
        switch (id) {
        case "auto":
            return KI18n.i18nc("@item a video encoder", "The best one here");
        case "vaapi":
            return KI18n.i18nc("@item a video encoder", "Graphics card (VA-API)");
        case "nvenc":
            return KI18n.i18nc("@item a video encoder", "NVIDIA graphics card (NVENC)");
        case "x264":
            return KI18n.i18nc("@item a video encoder", "x264 (software)");
        case "openh264":
            return KI18n.i18nc("@item a video encoder", "OpenH264 (software)");
        }
        return id;
    }

    function visualisationName(id: string): string {
        switch (id) {
        case "none":
            return KI18n.i18nc("@item what moves with the sound in a live stream's picture", "Nothing");
        case "waveform":
            return KI18n.i18nc("@item what moves with the sound in a live stream's picture", "Waveform");
        case "spectrum":
            return KI18n.i18nc("@item what moves with the sound in a live stream's picture", "Spectrum");
        case "meter":
            return KI18n.i18nc("@item what moves with the sound in a live stream's picture", "Level meter");
        }
        return id;
    }

    /// The state line above the tabs.
    function stateText(state: string, error: string, retryIn: int): string {
        if (state === "waitingForPassword") {
            return KI18n.i18nc("@info:status a live stream", "Getting the stream key…");
        }
        if (state === "failed" || state === "retrying") {
            const why = words.errorText(error);
            if (state === "failed") {
                return why;
            }
            return KI18n.i18ncp("@info:status a live stream; %2 says why",
                                "%2. Trying again in %1 second…",
                                "%2. Trying again in %1 seconds…",
                                Math.max(1, Math.ceil(retryIn / 1000)), why);
        }
        return words.stream.stateText(state, error, retryIn);
    }

    function errorText(error: string): string {
        switch (error) {
        case "noPassword":
            return KI18n.i18nc("@info:status a live stream", "No stream key is set");
        case "notConfigured":
            return KI18n.i18nc("@info:status a live stream", "No rtmp:// or rtmps:// address is set");
        case "encoderMissing":
            return KI18n.i18nc("@info:status a live stream", "This system has no H.264 encoder that works");
        case "rejected":
        case "badResponse":
            return KI18n.i18nc("@info:status a live stream", "The service refused the stream, or its key");
        }
        return words.stream.errorText(error);
    }
}
