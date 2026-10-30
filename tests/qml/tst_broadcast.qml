// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Broadcast block's panel: every control writing the parameter it
// names, the password going to the engine and never into the session, the
// formats this system cannot make said so, the media player a title
// follows chosen from the ones running, and the state line and Status tab
// following what the engine says the stream is doing.

import QtQuick
import QtQuick.Window
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "Broadcast"
    when: windowShown

    property string sessionId: ""
    /// Whether this build has the block: not where QtKeychain was missing.
    property bool available: false

    Component {
        id: panelComponent

        Window {
            property alias panel: inner

            width: 480
            height: 1000
            visible: true

            BroadcastPanel {
                id: inner

                width: 420
            }
        }
    }

    function makePanel() {
        const panel = createTemporaryObject(panelComponent, null).panel;
        panel.blockId = "air";
        return panel;
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("On Air");
        verify(testCase.sessionId !== "", "the mock opened a session");
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
        testCase.available = BlockCatalogue.types.indexOf("output.broadcast") >= 0;
        if (testCase.available) {
            TestEngine.applyEdit(testCase.sessionId,
                                 JSON.stringify({ op: "addBlock", block: { id: "air", type: "output.broadcast" }, index: -1 }));
        }
    }

    function init() {
        if (!testCase.available) {
            skip("this build has no Broadcast block: QtKeychain was not found");
        }
        TestEngine.stop(testCase.sessionId);
        TestEngine.setBroadcastPassword(testCase.sessionId, "air", "");
        SessionModel.setParams("air", [["server", "icecast"], ["format", "mp3"], ["bitrate", 128],
                                       ["channels", "stereo"], ["nowPlaying", "none"], ["nowPlayingPlayer", ""]]);
        TestPlayers.set([]);
    }

    function test_itIsAnOutputWithAPanelOfItsOwn() {
        verify(BlockCatalogue.types.indexOf("output.broadcast") >= 0, "the engine does not offer it");
        compare(BlockPresentation.category("output.broadcast"), "outputs");
        verify(BlockPanels.panelFor("output.broadcast") !== null, "it has no panel");
    }

    function test_theWordsCoverWhatTheEngineOffers() {
        const panel = testCase.makePanel();
        const words = panel.words;
        const format = BlockCatalogue.describeParam("output.broadcast", "format");
        const known = format.choices.concat((format.unavailableChoices ?? []).map(entry => entry.id));
        compare(known.slice().sort(), words.formats.slice().sort());
        for (const id of words.formats) {
            verify(words.formatName(id) !== id, id + " has no name, only its id");
        }
        compare(BlockCatalogue.describeParam("output.broadcast", "server").choices, words.servers);
        compare(BlockCatalogue.describeParam("output.broadcast", "channels").choices, words.channelChoices);
        compare(BlockCatalogue.describeParam("output.broadcast", "nowPlaying").choices, words.nowPlayingChoices);
        compare(BlockCatalogue.describeParam("output.broadcast", "titleTemplate")["default"],
                words.defaultTemplate);
        // Every rate offered is one the block takes.
        const bitrate = BlockCatalogue.describeParam("output.broadcast", "bitrate");
        for (const rate of words.bitrates) {
            verify(rate >= bitrate.minimum && rate <= bitrate.maximum, rate + " kbps is out of the block's range");
        }
        for (const error of ["authRejected", "noPassword", "notConfigured", "encoderMissing", "closed",
                             "connectFailed", "mountInUse", "timedOut", "resolveFailed",
                             "certificateUntrusted", "certificateExpired", "certificateWrongHost",
                             "certificateInvalid", "tlsFailed", "tlsHandshakeFailed",
                             "trustStoreUnavailable"]) {
            verify(words.errorText(error) !== words.errorText("something new"), error + " has no words");
        }
    }

    function test_theSetupTabWritesEveryParameter() {
        const panel = testCase.makePanel();
        compare(panel.section, "setup");
        panel.serverBox.activated(1);
        compare(SessionModel.param("air", "server"), "shoutcast");
        // SHOUTcast has no mount point and no user.
        tryVerify(() => !panel.mountField.visible && !panel.userField.visible, 1000);
        panel.serverBox.activated(0);
        tryVerify(() => panel.mountField.visible && panel.userField.visible, 1000);

        panel.hostField.text = "radio.example.org";
        panel.hostField.editingFinished();
        compare(SessionModel.param("air", "host"), "radio.example.org");
        panel.mountField.text = "/live";
        panel.mountField.editingFinished();
        compare(SessionModel.param("air", "mount"), "/live");
        panel.userField.text = "dj";
        panel.userField.editingFinished();
        compare(SessionModel.param("air", "user"), "dj");
        panel.portBox.value = 8443;
        panel.portBox.valueModified();
        compare(SessionModel.param("air", "port"), 8443);
        // A port is a name: no thousands separator.
        compare(panel.portBox.displayText, "8443");

        panel.formatBox.activated(panel.formats.indexOf("opus"));
        compare(SessionModel.param("air", "format"), "opus");
        panel.bitrateBox.activated(panel.bitrates.indexOf(96));
        compare(SessionModel.param("air", "bitrate"), 96);
        mouseClick(panel.monoButton);
        compare(SessionModel.param("air", "channels"), "mono");
        mouseClick(panel.stereoButton);
        compare(SessionModel.param("air", "channels"), "stereo");
    }

    // **TLS is Icecast's**: a check that writes the block's parameter, a
    // line saying what goes with it, and nothing for SHOUTcast, whose
    // protocol has none.
    function test_aSecureConnectionIsAskedForIcecastOnly() {
        const panel = testCase.makePanel();
        SessionModel.setParams("air", [["server", "icecast"], ["secure", false]]);
        tryVerify(() => panel.secureCheck.visible && !panel.secureCheck.checked, 1000);
        compare(panel.secureCheck.text, "Secure connection (TLS)");
        verify(panel.secureLine.visible);
        verify(panel.secureLine.text.indexOf("unencrypted") >= 0, panel.secureLine.text);

        mouseClick(panel.secureCheck);
        compare(SessionModel.param("air", "secure"), true);
        tryVerify(() => panel.secureLine.text.indexOf("8443") >= 0, 1000, panel.secureLine.text);

        panel.serverBox.activated(1);
        tryVerify(() => !panel.secureCheck.visible && !panel.secureLine.visible, 1000);
        // Kept for a switch back.
        compare(SessionModel.param("air", "secure"), true);
        panel.serverBox.activated(0);
        tryVerify(() => panel.secureCheck.visible && panel.secureCheck.checked, 1000);

        // An undo moves it like any other setting.
        SessionModel.setParam("air", "secure", false);
        tryVerify(() => !panel.secureCheck.checked, 1000);
    }

    function test_thePasswordGoesToTheEngineAndNotIntoTheSession() {
        const panel = testCase.makePanel();
        tryVerify(() => BroadcastStatus.revision > 0, 2000, "the panel did not ask where the stream stands");
        verify(!panel.passwordStored);
        verify(!panel.forgetButton.visible);

        panel.passwordField.text = "hackme";
        panel.passwordField.accepted();
        compare(panel.passwordField.text, "", "the password stayed in the field");
        tryVerify(() => panel.passwordStored, 2000, "the engine did not say it has a password");
        verify(panel.forgetButton.visible);
        verify(panel.passwordField.placeholderText.startsWith("Stored"), panel.passwordField.placeholderText);
        // **Nowhere in the session**: not a parameter, not in its JSON.
        const params = SessionModel.params("air");
        for (const key in params) {
            verify(String(params[key]).indexOf("hackme") < 0, key + " holds the password");
        }

        mouseClick(panel.forgetButton);
        tryVerify(() => !panel.passwordStored, 2000, "the password was not forgotten");
    }

    function test_theMetadataTabWritesTheStationAndTheTitle() {
        const panel = testCase.makePanel();
        panel.section = "metadata";
        verify(!panel.nowPlayingFileField.visible, "a file to read the title from, with no file chosen");
        mouseClick(panel.listedCheck);
        compare(SessionModel.param("air", "listed"), true);
        mouseClick(panel.listedCheck);
        compare(SessionModel.param("air", "listed"), false);

        panel.templateField.text = "%title";
        panel.templateField.editingFinished();
        compare(SessionModel.param("air", "titleTemplate"), "%title");
        panel.nowPlayingBox.activated(1);
        compare(SessionModel.param("air", "nowPlaying"), "file");
        tryVerify(() => panel.nowPlayingFileField.visible, 1000);
        panel.nowPlayingFileField.text = "~/now.txt";
        panel.nowPlayingFileField.editingFinished();
        compare(SessionModel.param("air", "nowPlayingFile"), "~/now.txt");
        // The station's details, each a field of its own.
        const fields = {};
        for (const row of panel.stationFields.children) {
            for (const child of row.children) {
                if (child.paramId !== undefined) {
                    fields[child.paramId] = child;
                }
            }
        }
        compare(Object.keys(fields).sort(), ["description", "genre", "stationName", "url"]);
        fields.stationName.text = "Night Shift";
        fields.stationName.editingFinished();
        compare(SessionModel.param("air", "stationName"), "Night Shift");
        fields.genre.text = "Ambient";
        fields.genre.editingFinished();
        compare(SessionModel.param("air", "genre"), "Ambient");
    }

    // **A player from the ones running**, by the name it gives itself, the
    // block holding its key; the automatic choice first, and a named
    // player that has quit still shown as the block's choice.
    function test_theTitleCanFollowAMediaPlayer() {
        TestPlayers.set([{ id: "vlc", name: "VLC media player", playing: false },
                         { id: "org.kde.elisa", name: "Elisa", playing: true }]);
        const panel = testCase.makePanel();
        panel.section = "metadata";
        verify(!panel.playerBox.visible, "a player to follow, with the title from nowhere");
        panel.nowPlayingBox.activated(panel.words.nowPlayingChoices.indexOf("player"));
        compare(SessionModel.param("air", "nowPlaying"), "player");
        tryVerify(() => panel.playerBox.visible, 1000);
        verify(!panel.nowPlayingFileField.visible);
        compare(panel.playerBox.model, ["The one this session captures", "VLC media player", "Elisa"]);
        compare(panel.playerBox.currentIndex, 0);
        verify(panel.playerLine.visible, "nothing says which player the automatic choice is");

        panel.playerBox.activated(2);
        compare(SessionModel.param("air", "nowPlayingPlayer"), "org.kde.elisa");
        tryVerify(() => !panel.playerLine.visible, 1000);
        compare(panel.playerBox.currentIndex, 2);

        // Elisa quits and another player starts: the box still says Elisa.
        TestPlayers.set([{ id: "vlc", name: "VLC media player", playing: true },
                         { id: "spotify", name: "Spotify", playing: false }]);
        tryCompare(panel.playerBox, "model", ["The one this session captures", "VLC media player", "Spotify",
                                              "org.kde.elisa (not running)"]);
        tryCompare(panel.playerBox, "currentIndex", 3);
        panel.playerBox.activated(0);
        compare(SessionModel.param("air", "nowPlayingPlayer"), "");
        tryCompare(panel.playerBox, "model", ["The one this session captures", "VLC media player", "Spotify"]);

        // A change from elsewhere (an undo, a second window) puts the box
        // back with the block.
        SessionModel.setParams("air", [["nowPlayingPlayer", "vlc"]]);
        tryCompare(panel.playerBox, "currentIndex", 1);
        panel.nowPlayingBox.activated(panel.words.nowPlayingChoices.indexOf("none"));
        tryVerify(() => !panel.playerBox.visible && !panel.playerLine.visible, 1000);
    }

    function test_theStateLineAndTheStatusTabFollowTheStream() {
        const panel = testCase.makePanel();
        compare(panel.stateLabel.text, "Starts when the session runs");
        verify(!panel.timeLabel.visible);

        // Running with no password: it says so, and what to do is the
        // Setup tab's.
        TestEngine.start(testCase.sessionId);
        tryCompare(panel.stateLabel, "text", "No password is set");
        panel.section = "status";
        compare(panel.problemLabel.text, "No password is set");
        TestEngine.stop(testCase.sessionId);
        tryCompare(panel.stateLabel, "text", "Starts when the session runs");

        BroadcastStatus.setPassword(testCase.sessionId, "air", "hackme");
        TestEngine.start(testCase.sessionId);
        tryCompare(panel.stateLabel, "text", "On the air");
        verify(panel.timeLabel.visible);
        compare(panel.timeLabel.text.slice(0, 7), "00:00:0");
        compare(panel.connectedLabel.text.slice(0, 7), "00:00:0");
        compare(panel.reconnectsLabel.text, "0");
        compare(panel.problemLabel.text, "—");
        verify(panel.rateLabel.text.endsWith("kbps"), panel.rateLabel.text);
        verify(panel.sentLabel.text !== "", "nothing says how much was sent");
        compare(panel.securityLabel.text, "Not encrypted");

        TestEngine.stop(testCase.sessionId);
        tryCompare(panel.stateLabel, "text", "Starts when the session runs");
        compare(panel.rateLabel.text, "—");
        compare(panel.securityLabel.text, "—");

        // Over TLS, the Status tab says so.
        SessionModel.setParams("air", [["server", "icecast"], ["secure", true]]);
        TestEngine.start(testCase.sessionId);
        tryCompare(panel.stateLabel, "text", "On the air");
        tryCompare(panel.securityLabel, "text", "Secure (TLS)");
        TestEngine.stop(testCase.sessionId);
        tryCompare(panel.stateLabel, "text", "Starts when the session runs");
        SessionModel.setParam("air", "secure", false);
    }

    function test_theWordsForARetryCountTheSeconds() {
        const panel = testCase.makePanel();
        compare(panel.words.stateText("retrying", "closed", 4000),
                "The server closed the connection. Trying again in 4 seconds…");
        compare(panel.words.stateText("retrying", "connectFailed", 900),
                "The server isn't answering. Trying again in 1 second…");
        compare(panel.words.stateText("failed", "authRejected", 0), "The server refused the user name or password");
        compare(panel.words.stateText("failed", "certificateWrongHost", 0),
                "The server's certificate is for another name than the host");
        compare(panel.words.stateText("connecting", "", 0), "Connecting…");
    }

    // **Only the rates the format takes**: Vorbis wants 48 kbit/s at least
    // for stereo and takes no more than 192 for mono, and a setting outside
    // that is shown as the rate it is streamed at.
    function test_theBitRatesAreTheOnesTheFormatTakes() {
        const panel = testCase.makePanel();
        SessionModel.setParams("air", [["format", "vorbis"], ["channels", "stereo"], ["bitrate", 32]]);
        tryVerify(() => panel.bitrates.indexOf(32) < 0, 1000, "Vorbis stereo offered 32 kbit/s");
        compare(panel.bitrate, 48);
        compare(panel.bitrateBox.displayText, "48 kbps");
        SessionModel.setParams("air", [["channels", "mono"], ["bitrate", 320]]);
        tryVerify(() => panel.bitrates.indexOf(256) < 0 && panel.bitrates.indexOf(192) >= 0, 1000);
        compare(panel.bitrate, 192);
        SessionModel.setParams("air", [["format", "opus"], ["bitrate", 320]]);
        tryCompare(panel, "bitrate", 256);
        SessionModel.setParams("air", [["format", "mp3"], ["channels", "stereo"]]);
        tryCompare(panel, "bitrate", 320);
        compare(panel.bitrates, panel.words.bitrates);
    }
}
