// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The words the Recorder's face and panel both use: its formats, how
// it is set, what it is doing, and the qualities a person picks from.
//
// **Not from the engine**: the block holds stable ids, and the words are
// the interface's, translated and keyed by them. tst_recorder.qml holds the
// lists here against the engine's, so the two cannot drift.
QtObject {
    id: words

    /// Every format the block knows, in the order the block lists them.
    /// Which of them this system offers is the catalogue's to say.
    readonly property var formats: ["wav", "aiff", "flac", "mp3", "opus", "vorbis", "aac", "alac"]
    /// The rates a lossy format can be asked for, in kilobits a second.
    readonly property var bitrates: [32, 48, 64, 96, 128, 160, 192, 256, 320]
    readonly property var sampleRates: ["session", "44100", "48000", "88200", "96000"]
    readonly property var sampleFormats: ["int16", "int24", "float32"]
    readonly property var channelChoices: ["stereo", "mono"]
    /// The file name a Recorder is given until somebody changes it.
    readonly property string defaultPattern: "%session %date %time"

    /// **The qualities**: a format and its options in one choice, the
    /// way a person thinks of them. High-quality MP3 is what the block
    /// starts as. Picking one sets these parameters and nothing else, so the
    /// folder, the name and the limits are left as they were.
    readonly property var qualities: [
        {
            id: "voice",
            params: { format: "opus", bitrate: 48, bitrateMode: "vbr", channels: "mono" }
        },
        {
            id: "standardMp3",
            params: { format: "mp3", bitrate: 192, bitrateMode: "vbr", channels: "stereo" }
        },
        {
            id: "highMp3",
            params: { format: "mp3", bitrate: 256, bitrateMode: "vbr", channels: "stereo" }
        },
        {
            id: "highAac",
            params: { format: "aac", bitrate: 256, channels: "stereo" }
        },
        {
            id: "lossless",
            params: { format: "flac", sampleFormat: "int24", channels: "stereo" }
        },
        {
            id: "uncompressed",
            params: { format: "wav", sampleFormat: "int24", channels: "stereo" }
        }
    ]

    function isLossy(format: string): bool {
        return format === "mp3" || format === "opus" || format === "vorbis" || format === "aac";
    }

    /// What the tile, the list and the status line call a format.
    function formatName(id: string): string {
        switch (id) {
        case "wav":
            return KI18n.i18nc("@item a recording format", "WAV");
        case "aiff":
            return KI18n.i18nc("@item a recording format", "AIFF");
        case "flac":
            return KI18n.i18nc("@item a recording format", "FLAC");
        case "mp3":
            return KI18n.i18nc("@item a recording format", "MP3");
        case "opus":
            return KI18n.i18nc("@item a recording format", "Opus");
        case "vorbis":
            return KI18n.i18nc("@item a recording format", "Ogg Vorbis");
        case "aac":
            return KI18n.i18nc("@item a recording format", "AAC");
        case "alac":
            return KI18n.i18nc("@item a recording format, Apple's lossless codec", "ALAC");
        }
        return id;
    }

    function sampleFormatName(id: string): string {
        switch (id) {
        case "int16":
            return KI18n.i18nc("@item bits a sample in a recording", "16-bit");
        case "int24":
            return KI18n.i18nc("@item bits a sample in a recording", "24-bit");
        case "float32":
            return KI18n.i18nc("@item a recording's samples as 32-bit floating point", "32-bit float");
        }
        return id;
    }

    function channelsName(id: string): string {
        return id === "mono" ? KI18n.i18nc("@item one channel in a recording", "Mono")
                             : KI18n.i18nc("@item two channels in a recording", "Stereo");
    }

    function bitrateText(kbps: int, mode: string, format: string): string {
        // AAC here is always the same rate throughout, so it says nothing of
        // variable rates.
        if (format === "aac" || mode === "cbr") {
            return KI18n.i18nc("@item a recording's bit rate", "%1 kbps", kbps);
        }
        return KI18n.i18nc("@item a recording's bit rate, varying with the sound", "%1 kbps · VBR", kbps);
    }

    function sampleRateName(id: string, sessionHz: int): string {
        if (id === "session") {
            return sessionHz > 0
                ? KI18n.i18nc("@item the session's own sample rate, %1 in kHz", "Follow session (%1 kHz)",
                              words.kilohertz(sessionHz))
                : KI18n.i18nc("@item the session's own sample rate", "Follow session");
        }
        return KI18n.i18nc("@item a sample rate, %1 in kHz", "%1 kHz", words.kilohertz(Number(id)));
    }

    function kilohertz(hz: int): string {
        return Number(hz / 1000).toLocaleString(Qt.locale(), "f", hz % 1000 === 0 ? 0 : 1);
    }

    /// **The line under the format on the tile**: how much of the sound it
    /// keeps and on how many channels — "256 kbps · Stereo" for a lossy
    /// one, "24-bit · Stereo" for the rest.
    function caption(params: var): string {
        const format = params.format ?? "mp3";
        const how = words.isLossy(format) ? KI18n.i18nc("@item a recording's bit rate", "%1 kbps", params.bitrate ?? 256)
                                          : words.sampleFormatName(words.depthOf(format, params.sampleFormat ?? "int24"));
        return KI18n.i18nc("@info how a Recorder records, '256 kbps · Stereo'", "%1 · %2", how,
                           words.channelsName(params.channels ?? "stereo"));
    }

    /// The bits a lossless format keeps: FLAC and ALAC have no floating
    /// point, and take 24 bits for it.
    function depthOf(format: string, sampleFormat: string): string {
        return (format === "flac" || format === "alac") && sampleFormat === "float32" ? "int24" : sampleFormat;
    }

    function qualityName(id: string): string {
        switch (id) {
        case "voice":
            return KI18n.i18nc("@item a recording quality", "Voice");
        case "standardMp3":
            return KI18n.i18nc("@item a recording quality", "Standard MP3");
        case "highMp3":
            return KI18n.i18nc("@item a recording quality", "High-quality MP3");
        case "highAac":
            return KI18n.i18nc("@item a recording quality", "High-quality AAC");
        case "lossless":
            return KI18n.i18nc("@item a recording quality", "Lossless");
        case "uncompressed":
            return KI18n.i18nc("@item a recording quality", "Uncompressed");
        }
        return KI18n.i18nc("@item a recording quality set by hand", "Custom");
    }

    /// One line on what a quality makes, under the list.
    function qualitySummary(id: string): string {
        switch (id) {
        case "voice":
            return KI18n.i18nc("@info a recording quality", "Opus, 48 kbps, mono: speech, and small files.");
        case "standardMp3":
            return KI18n.i18nc("@info a recording quality", "MP3, 192 kbps variable: good for most listening.");
        case "highMp3":
            return KI18n.i18nc("@info a recording quality",
                               "MP3, 256 kbps variable: as good as MP3 gets for most ears, and plays anywhere.");
        case "highAac":
            return KI18n.i18nc("@info a recording quality", "AAC in M4A, 256 kbps: smaller than MP3 for the same sound.");
        case "lossless":
            return KI18n.i18nc("@info a recording quality", "FLAC, 24-bit: every sample kept, at about half the size.");
        case "uncompressed":
            return KI18n.i18nc("@info a recording quality", "WAV, 24-bit: every sample kept, for editing afterwards.");
        }
        return KI18n.i18nc("@info a recording quality set by hand", "Set by hand, below.");
    }

    /// **Which quality these settings are**, or "custom". A quality only
    /// holds the parameters it names, so a folder or a limit never makes
    /// a quality custom.
    function qualityOf(params: var): string {
        const defaults = { format: "mp3", bitrate: 256, bitrateMode: "vbr", channels: "stereo", sampleFormat: "int24" };
        for (const quality of words.qualities) {
            let matches = true;
            for (const key in quality.params) {
                if (String(params[key] ?? defaults[key]) !== String(quality.params[key])) {
                    matches = false;
                    break;
                }
            }
            if (matches) {
                return quality.id;
            }
        }
        return "custom";
    }

    /// What the status line says.
    function statusText(state: string, format: string, stopReason: string): string {
        switch (state) {
        case "recording":
            return KI18n.i18nc("@info:status %1 is a format, 'MP3'", "Recording %1", words.formatName(format));
        case "paused":
            return KI18n.i18nc("@info:status a recording", "Paused");
        case "stopped":
            return stopReason === "silence"
                ? KI18n.i18nc("@info:status a recording that stopped by itself", "Stopped on silence")
                : KI18n.i18nc("@info:status a recording that stopped by itself", "Stopped at its time limit");
        }
        return KI18n.i18nc("@info:status %1 is a format, 'MP3'", "Ready to record %1", words.formatName(format));
    }

    /// The tokens the name editor offers, and what each is called on its
    /// chip.
    readonly property var tokens: ["%session", "%date", "%time", "%counter", "%block"]

    function tokenName(token: string): string {
        switch (token) {
        case "%session":
            return KI18n.i18nc("@action:button a file-name token, the session's name", "Session");
        case "%date":
            return KI18n.i18nc("@action:button a file-name token, today's date", "Date");
        case "%time":
            return KI18n.i18nc("@action:button a file-name token, the time of day", "Time");
        case "%counter":
            return KI18n.i18nc("@action:button a file-name token, a number counting up", "Counter");
        case "%block":
            return KI18n.i18nc("@action:button a file-name token, the Recorder's own name", "Block");
        }
        return token;
    }
}
