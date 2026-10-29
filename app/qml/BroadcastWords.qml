// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The words the Broadcast block's panel uses: its servers, formats and
// title sources, and what its stream is doing and why.
//
// **Not from the engine**: the block holds stable ids, and the words are
// the interface's, translated and keyed by them. tst_broadcast.qml holds the
// lists here against the engine's, so the two cannot drift.
QtObject {
    id: words

    readonly property var servers: ["icecast", "shoutcast"]
    /// Every format the block knows, in the order the block lists them.
    /// Which of them this system offers is the catalogue's to say.
    readonly property var formats: ["mp3", "opus", "vorbis", "aac"]
    /// The rates a stream can be asked for, in kilobits a second.
    readonly property var bitrates: [32, 48, 64, 96, 128, 160, 192, 256, 320]
    readonly property var channelChoices: ["stereo", "mono"]
    readonly property var nowPlayingChoices: ["none", "file", "player"]
    /// The title template a block is given until somebody changes it.
    readonly property string defaultTemplate: "%artist - %title"

    function serverName(id: string): string {
        switch (id) {
        case "icecast":
            return KI18n.i18nc("@item a kind of internet radio server", "Icecast");
        case "shoutcast":
            return KI18n.i18nc("@item a kind of internet radio server", "SHOUTcast");
        }
        return id;
    }

    function formatName(id: string): string {
        switch (id) {
        case "mp3":
            return KI18n.i18nc("@item a stream's format", "MP3");
        case "opus":
            return KI18n.i18nc("@item a stream's format", "Opus");
        case "vorbis":
            return KI18n.i18nc("@item a stream's format", "Ogg Vorbis");
        case "aac":
            return KI18n.i18nc("@item a stream's format", "AAC");
        }
        return id;
    }

    function nowPlayingName(id: string): string {
        switch (id) {
        case "none":
            return KI18n.i18nc("@item where a stream's title comes from", "Only the template");
        case "file":
            return KI18n.i18nc("@item where a stream's title comes from", "A text file");
        case "player":
            return KI18n.i18nc("@item where a stream's title comes from", "A media player");
        }
        return id;
    }

    /// The media players a title can follow, for the list: the automatic
    /// choice first, then \a running as MediaPlayers has them, then \a chosen
    /// when it is a player that is not running now, so that what the block
    /// holds is always one of them. `[{id, name}]`.
    function playerChoices(running: var, chosen: string): var {
        const choices = [{ id: "", name: KI18n.i18nc("@item the media player a stream's title follows",
                                                      "The one this session captures") }];
        for (const player of running) {
            choices.push({ id: player.id, name: player.name });
        }
        if (chosen !== "" && !running.some(player => player.id === chosen)) {
            choices.push({ id: chosen, name: KI18n.i18nc("@item %1 is a media player that is not running now",
                                                         "%1 (not running)", chosen) });
        }
        return choices;
    }

    /// The state line above the tabs.
    function stateText(state: string, error: string, retryIn: int): string {
        switch (state) {
        case "connected":
            return KI18n.i18nc("@info:status a stream", "On the air");
        case "connecting":
            return KI18n.i18nc("@info:status a stream", "Connecting…");
        case "waitingForPassword":
            return KI18n.i18nc("@info:status a stream", "Getting the password…");
        case "retrying":
            return KI18n.i18ncp("@info:status a stream; %2 says why",
                                "%2. Trying again in %1 second…",
                                "%2. Trying again in %1 seconds…",
                                Math.max(1, Math.ceil(retryIn / 1000)), words.errorText(error));
        case "failed":
            return words.errorText(error);
        }
        return KI18n.i18nc("@info:status a stream", "Starts when the session runs");
    }

    /// What went wrong, as a sentence of its own.
    function errorText(error: string): string {
        switch (error) {
        case "authRejected":
            return KI18n.i18nc("@info:status a stream", "The server refused the user name or password");
        case "noPassword":
            return KI18n.i18nc("@info:status a stream", "No password is set");
        case "notConfigured":
            return KI18n.i18nc("@info:status a stream", "No server is set");
        case "encoderMissing":
            return KI18n.i18nc("@info:status a stream", "This system can't make this format");
        case "encoderFailed":
            return KI18n.i18nc("@info:status a stream", "The encoder stopped");
        case "resolveFailed":
            return KI18n.i18nc("@info:status a stream", "The server's name wasn't found");
        case "connectFailed":
            return KI18n.i18nc("@info:status a stream", "The server isn't answering");
        case "timedOut":
            return KI18n.i18nc("@info:status a stream", "The server took too long");
        case "closed":
            return KI18n.i18nc("@info:status a stream", "The server closed the connection");
        case "mountInUse":
            return KI18n.i18nc("@info:status a stream", "The mount point is in use");
        case "rejected":
        case "badResponse":
            return KI18n.i18nc("@info:status a stream", "The server refused the stream");
        }
        return KI18n.i18nc("@info:status a stream", "The stream stopped");
    }
}
