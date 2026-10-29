// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Live Stream block's panel: every control writing the parameter it
// names, the key going to the engine and never into the session, the
// encoders this system has not got left out, the same now-playing rows as
// Broadcast's, and the state line and Status tab following the stream.

import QtQuick
import QtQuick.Window
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "LiveStream"
    when: windowShown

    property string sessionId: ""
    /// Whether this build has the block.
    property bool available: false

    Component {
        id: panelComponent

        Window {
            property alias panel: inner

            width: 480
            height: 1100
            visible: true

            LiveStreamPanel {
                id: inner

                width: 420
            }
        }
    }

    function makePanel() {
        const panel = createTemporaryObject(panelComponent, null).panel;
        panel.blockId = "live";
        return panel;
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Live");
        verify(testCase.sessionId !== "", "the mock opened a session");
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
        testCase.available = BlockCatalogue.types.indexOf("output.livestream") >= 0;
        if (testCase.available) {
            TestEngine.applyEdit(testCase.sessionId,
                                 JSON.stringify({ op: "addBlock", block: { id: "live", type: "output.livestream" }, index: -1 }));
        }
    }

    function init() {
        if (!testCase.available) {
            skip("this build has no Live Stream block");
        }
        TestEngine.stop(testCase.sessionId);
        TestEngine.setBroadcastPassword(testCase.sessionId, "live", "");
        SessionModel.setParams("live", [["url", ""], ["quality", "higher"], ["encoder", "auto"],
                                        ["visualisation", "waveform"], ["nowPlaying", "none"],
                                        ["nowPlayingPlayer", ""]]);
        TestPlayers.set([]);
    }

    function test_itIsAnOutputWithAPanelOfItsOwn() {
        compare(BlockPresentation.category("output.livestream"), "outputs");
        verify(BlockPanels.panelFor("output.livestream") !== null, "it has no panel");
    }

    function test_theWordsCoverWhatTheEngineOffers() {
        const panel = testCase.makePanel();
        const words = panel.words;
        compare(BlockCatalogue.describeParam("output.livestream", "quality").choices, words.qualities);
        compare(BlockCatalogue.describeParam("output.livestream", "visualisation").choices, words.visualisations);
        const encoder = BlockCatalogue.describeParam("output.livestream", "encoder");
        const known = encoder.choices.concat((encoder.unavailableChoices ?? []).map(entry => entry.id));
        compare(known.slice().sort(), words.encoders.slice().sort());
        for (const id of words.qualities.concat(words.encoders, words.visualisations)) {
            verify(words.qualityName(id) !== id || words.encoderName(id) !== id || words.visualisationName(id) !== id,
                   id + " has no name, only its id");
        }
        compare(BlockCatalogue.describeParam("output.livestream", "title")["default"], words.defaultTitle);
        compare(panel.defaultBackground, BlockCatalogue.describeParam("output.livestream", "background")["default"]);
        // **The same title sources as Broadcast's**, in the same words.
        compare(BlockCatalogue.describeParam("output.livestream", "nowPlaying").choices,
                words.stream.nowPlayingChoices);
    }

    function test_theSetupTabWritesEveryParameter() {
        const panel = testCase.makePanel();
        compare(panel.section, "setup");
        panel.urlField.text = "rtmps://live.example.com/app";
        panel.urlField.editingFinished();
        compare(SessionModel.param("live", "url"), "rtmps://live.example.com/app");
        panel.qualityBox.activated(panel.words.qualities.indexOf("best"));
        compare(SessionModel.param("live", "quality"), "best");
        compare(panel.encoderBox.model[0], "The best one here");
        panel.encoderBox.activated(0);
        compare(SessionModel.param("live", "encoder"), "auto");
    }

    // **Only the encoders this system has**, and the one the block holds
    // even when it is not one of them, so that a session from another
    // computer shows what it asks for.
    function test_theEncodersOfferedAreTheOnesHere() {
        const panel = testCase.makePanel();
        const missing = panel.unavailable.map(entry => entry.id);
        for (const id of missing) {
            verify(panel.encoderBox.choices.indexOf(id) < 0 || id === panel.encoder, id + " is offered");
        }
        if (missing.length > 0 && missing[0] !== "auto") {
            SessionModel.setParams("live", [["encoder", missing[0]]]);
            tryVerify(() => panel.encoderBox.choices.indexOf(missing[0]) >= 0, 1000);
            compare(panel.encoderBox.currentIndex, panel.encoderBox.choices.indexOf(missing[0]));
        }
        compare(panel.noEncoderLine.visible, missing.indexOf("auto") >= 0);
    }

    function test_theKeyGoesToTheEngineAndNotIntoTheSession() {
        const panel = testCase.makePanel();
        tryVerify(() => BroadcastStatus.revision > 0, 2000, "the panel did not ask where the stream stands");
        verify(!panel.keyStored);
        panel.keyField.text = "live_0123456789";
        panel.keyField.accepted();
        compare(panel.keyField.text, "", "the key stayed in the field");
        tryVerify(() => panel.keyStored, 2000, "the engine did not say it has a key");
        verify(panel.forgetButton.visible);
        const params = SessionModel.params("live");
        for (const key in params) {
            verify(String(params[key]).indexOf("live_0123456789") < 0, key + " holds the key");
        }
        mouseClick(panel.forgetButton);
        tryVerify(() => !panel.keyStored, 2000, "the key was not forgotten");
    }

    function test_thePictureTabWritesEveryParameter() {
        const panel = testCase.makePanel();
        panel.section = "picture";
        panel.titleField.text = "Late Show";
        panel.titleField.editingFinished();
        compare(SessionModel.param("live", "title"), "Late Show");
        panel.subtitleField.text = "Live from %session";
        panel.subtitleField.editingFinished();
        compare(SessionModel.param("live", "subtitle"), "Live from %session");
        panel.templateField.text = "%title";
        panel.templateField.editingFinished();
        compare(SessionModel.param("live", "titleTemplate"), "%title");
        panel.visualisationBox.activated(panel.words.visualisations.indexOf("spectrum"));
        compare(SessionModel.param("live", "visualisation"), "spectrum");
        panel.backgroundField.text = "#ffffff";
        panel.backgroundField.editingFinished();
        compare(SessionModel.param("live", "background"), "#ffffff");
        panel.backgroundImageField.text = "~/back.png";
        panel.backgroundImageField.editingFinished();
        compare(SessionModel.param("live", "backgroundImage"), "~/back.png");
        panel.logoField.text = "~/logo.png";
        panel.logoField.editingFinished();
        compare(SessionModel.param("live", "logo"), "~/logo.png");
    }

    // **The rows Broadcast's panel has**, over the same parameters: a file,
    // or a media player from the ones running.
    function test_whatIsPlayingComesFromWhereBroadcastsDoes() {
        TestPlayers.set([{ id: "org.kde.elisa", name: "Elisa", playing: true }]);
        const panel = testCase.makePanel();
        panel.section = "picture";
        const rows = panel.nowPlaying;
        verify(!rows.fileField.visible && !rows.playerBox.visible);
        rows.sourceBox.activated(1);
        compare(SessionModel.param("live", "nowPlaying"), "file");
        tryVerify(() => rows.fileField.visible, 1000);
        rows.fileField.text = "~/now.txt";
        rows.fileField.editingFinished();
        compare(SessionModel.param("live", "nowPlayingFile"), "~/now.txt");
        rows.sourceBox.activated(2);
        compare(SessionModel.param("live", "nowPlaying"), "player");
        tryVerify(() => rows.playerBox.visible, 1000);
        compare(rows.playerBox.model, ["The one this session captures", "Elisa"]);
        rows.playerBox.activated(1);
        compare(SessionModel.param("live", "nowPlayingPlayer"), "org.kde.elisa");
    }

    function test_theStateLineAndTheStatusTabFollowTheStream() {
        const panel = testCase.makePanel();
        compare(panel.stateLabel.text, "Starts when the session runs");
        TestEngine.start(testCase.sessionId);
        tryCompare(panel.stateLabel, "text", "No stream key is set");
        panel.section = "status";
        compare(panel.problemLabel.text, "No stream key is set");
        TestEngine.stop(testCase.sessionId);

        BroadcastStatus.setPassword(testCase.sessionId, "live", "live_0123456789");
        TestEngine.start(testCase.sessionId);
        tryCompare(panel.stateLabel, "text", "On the air");
        verify(panel.timeLabel.visible);
        compare(panel.reconnectsLabel.text, "0");
        verify(panel.rateLabel.text.endsWith("kbps"), panel.rateLabel.text);
        compare(panel.framesLabel.text, "0 sent, 0 left out");
        TestEngine.stop(testCase.sessionId);
        tryCompare(panel.stateLabel, "text", "Starts when the session runs");
    }

    function test_theWordsSayKeyNotPassword() {
        const panel = testCase.makePanel();
        compare(panel.words.stateText("waitingForPassword", "", 0), "Getting the stream key…");
        compare(panel.words.stateText("retrying", "rejected", 2000),
                "The service refused the stream, or its key. Trying again in 2 seconds…");
        compare(panel.words.stateText("failed", "encoderMissing", 0), "This system has no H.264 encoder that works");
        compare(panel.words.stateText("retrying", "closed", 1000),
                "The server closed the connection. Trying again in 1 second…");
    }
}
