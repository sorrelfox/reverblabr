// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Recorder's face and panel: every control writing the parameter
// it names, the qualities setting several at once, the file name's live
// preview, the formats this system cannot write left out with a word on
// why, and Pause and Split working from the face and from the panel.

import QtQuick
import QtQuick.Window
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "Recorder"
    when: windowShown

    property string sessionId: ""

    // In windows of their own, for tests that press on them.
    Component {
        id: faceComponent

        Window {
            property alias face: inner

            width: 240
            height: 160
            visible: true

            RecorderFace {
                id: inner

                width: 180
                height: 110
            }
        }
    }

    Component {
        id: panelComponent

        Window {
            property alias panel: inner

            width: 480
            height: 1000
            visible: true

            RecorderPanel {
                id: inner

                width: 420
            }
        }
    }

    function makeFace() {
        const face = createTemporaryObject(faceComponent, null).face;
        face.blockId = "rec";
        return face;
    }

    function makePanel() {
        const panel = createTemporaryObject(panelComponent, null).panel;
        panel.blockId = "rec";
        return panel;
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Take");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "rec", type: "output.recorder" }, index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        TestEngine.stop(testCase.sessionId);
        SessionModel.setParams("rec", [["format", "mp3"], ["bitrate", 256], ["bitrateMode", "vbr"],
                                       ["channels", "stereo"], ["sampleFormat", "int24"], ["sampleRate", "session"],
                                       ["fileName", ""], ["folder", ""]]);
    }

    function test_itIsAnOutputWithAFaceAndAPanelOfItsOwn() {
        verify(BlockCatalogue.types.indexOf("output.recorder") >= 0, "the engine does not offer it");
        compare(BlockPresentation.category("output.recorder"), "outputs");
        verify(BlockFaces.faceFor("output.recorder") !== null, "it has no face");
        verify(BlockPanels.panelFor("output.recorder") !== null, "it has no panel");
    }

    function test_theWordsCoverWhatTheEngineOffers() {
        const panel = testCase.makePanel();
        const words = panel.words;
        const format = BlockCatalogue.describeParam("output.recorder", "format");
        const known = format.choices.concat((format.unavailableChoices ?? []).map(entry => entry.id));
        compare(known.slice().sort(), words.formats.slice().sort());
        for (const id of words.formats) {
            verify(words.formatName(id) !== id, id + " has no name, only its id");
        }
        compare(BlockCatalogue.describeParam("output.recorder", "sampleRate").choices, words.sampleRates);
        compare(BlockCatalogue.describeParam("output.recorder", "sampleFormat").choices, words.sampleFormats);
        // Every quality sets only parameters the block has, to values it
        // takes.
        for (const quality of words.qualities) {
            for (const key in quality.params) {
                const param = BlockCatalogue.describeParam("output.recorder", key);
                verify(param.id === key, quality.id + " sets " + key + ", which the Recorder does not have");
                if (param.choices !== undefined) {
                    verify(param.choices.indexOf(quality.params[key]) >= 0
                               || (param.unavailableChoices ?? []).some(entry => entry.id === quality.params[key]),
                           quality.id + " sets " + key + " to " + quality.params[key]);
                }
            }
            verify(words.qualityName(quality.id) !== words.qualityName("custom"), quality.id + " has no name");
        }
    }

    function test_theFaceSaysTheFormatAndHowItRecords() {
        const face = testCase.makeFace();
        tryCompare(face.headlineLabel, "text", "MP3");
        compare(face.captionLabel.text, "256 kbps · Stereo");
        verify(!face.pill.visible, "a REC pill on a Recorder that is not recording");

        SessionModel.setParams("rec", [["format", "flac"], ["sampleFormat", "float32"], ["channels", "mono"]]);
        tryCompare(face.headlineLabel, "text", "FLAC");
        // FLAC has no floating point: it keeps 24 bits of it.
        compare(face.captionLabel.text, "24-bit · Mono");
    }

    function test_pauseAndSplitWorkFromTheFace() {
        const face = testCase.makeFace();
        verify(!face.pauseButton.enabled && !face.splitButton.enabled, "the buttons work with nothing recording");

        TestEngine.start(testCase.sessionId);
        tryVerify(() => face.pill.visible, 2000, "no REC pill once it records");
        verify(face.pillLabel.text.startsWith("REC 00:00:0"), face.pillLabel.text);
        verify(face.pauseButton.enabled && face.splitButton.enabled);

        const firstFile = RecordingStatus.file(testCase.sessionId, "rec");
        verify(firstFile.endsWith(".mp3"), firstFile);
        mouseClick(face.splitButton);
        // A split is a new file, not a new recording.
        compare(RecordingStatus.state(testCase.sessionId, "rec"), "recording");

        mouseClick(face.pauseButton);
        tryCompare(face, "recording", "paused");
        verify(face.pillLabel.text.startsWith("PAUSED"), face.pillLabel.text);
        compare(face.pauseButton.text, "Resume");
        verify(!face.splitButton.enabled, "a paused recording offered a split");
        mouseClick(face.pauseButton);
        tryCompare(face, "recording", "recording");

        TestEngine.stop(testCase.sessionId);
        tryCompare(face, "recording", "idle");
        verify(!face.pill.visible);
    }

    function test_pauseAndSplitWorkFromThePanelWhichSaysWhatIsGoingOn() {
        const panel = testCase.makePanel();
        compare(panel.statusLabel.text, "Ready to record MP3");
        verify(!panel.timeLabel.visible);

        TestEngine.start(testCase.sessionId);
        tryCompare(panel.statusLabel, "text", "Recording MP3");
        verify(panel.timeLabel.visible);
        compare(panel.timeLabel.text.slice(0, 7), "00:00:0");

        mouseClick(panel.pauseButton);
        tryCompare(panel.statusLabel, "text", "Paused");
        mouseClick(panel.pauseButton);
        tryCompare(panel.statusLabel, "text", "Recording MP3");
        mouseClick(panel.splitButton);
        compare(panel.recording, "recording");
        TestEngine.stop(testCase.sessionId);
        tryCompare(panel.statusLabel, "text", "Ready to record MP3");
    }

    function test_everyFormatOptionWritesItsParameter() {
        const panel = testCase.makePanel();
        panel.formatOptions.expanded = true;
        tryVerify(() => panel.bitrateBox.width > 0, 1000);

        panel.formatBox.activated(panel.formats.indexOf("opus"));
        compare(SessionModel.param("rec", "format"), "opus");
        panel.bitrateBox.activated(panel.words.bitrates.indexOf(96));
        compare(SessionModel.param("rec", "bitrate"), 96);
        mouseClick(panel.constantButton);
        compare(SessionModel.param("rec", "bitrateMode"), "cbr");
        mouseClick(panel.monoButton);
        compare(SessionModel.param("rec", "channels"), "mono");
        panel.rateBox.activated(panel.words.sampleRates.indexOf("44100"));
        compare(SessionModel.param("rec", "sampleRate"), "44100");
        // Bit rate for the formats that have one, bit depth for the rest.
        verify(panel.bitrateBox.visible && !panel.depthBox.visible);

        panel.formatBox.activated(panel.formats.indexOf("wav"));
        tryVerify(() => panel.depthBox.visible && !panel.bitrateBox.visible);
        panel.depthBox.activated(panel.depthBox.offered.indexOf("float32"));
        compare(SessionModel.param("rec", "sampleFormat"), "float32");
        panel.formatBox.activated(panel.formats.indexOf("flac"));
        tryCompare(panel.depthBox, "displayText", "24-bit");
        verify(panel.depthBox.offered.indexOf("float32") < 0, "FLAC offered floating point");

        // **Undo takes back the last of them**, as a control moved by hand.
        SessionModel.undo();
        tryCompare(panel.formatBox, "displayText", "WAV");
    }

    function test_aQualitySetsItsOptionsAsOneStepAndReadsBack() {
        const panel = testCase.makePanel();
        // The block's own starting point.
        compare(panel.qualityBox.displayText, "High-quality MP3");

        panel.qualityBox.activated(panel.qualityBox.model.indexOf("voice"));
        compare(SessionModel.param("rec", "format"), "opus");
        compare(SessionModel.param("rec", "bitrate"), 48);
        compare(SessionModel.param("rec", "channels"), "mono");
        tryCompare(panel.qualityBox, "displayText", "Voice");
        verify(panel.qualityLine.text.indexOf("48 kbps") >= 0, panel.qualityLine.text);
        SessionModel.undo();
        tryCompare(panel.qualityBox, "displayText", "High-quality MP3");

        panel.qualityBox.activated(panel.qualityBox.model.indexOf("lossless"));
        tryCompare(panel.qualityBox, "displayText", "Lossless");
        compare(SessionModel.param("rec", "format"), "flac");

        // Set by hand: custom.
        SessionModel.setParam("rec", "sampleFormat", "int16");
        tryCompare(panel.qualityBox, "displayText", "Custom");
    }

    function test_formatsThisSystemCannotWriteAreLeftOutAndSaySo() {
        const panel = testCase.makePanel();
        panel.formatOptions.expanded = true;
        // What the catalogue says on a system whose FFmpeg has neither.
        panel.formatParam = {
            id: "format",
            choices: ["wav", "aiff", "flac", "mp3", "opus", "vorbis"],
            unavailableChoices: [{ id: "aac", reason: "encoderMissing" }, { id: "alac", reason: "encoderMissing" }]
        };
        verify(panel.formatBox.model.indexOf("aac") < 0, "AAC offered with no encoder");
        verify(panel.qualityBox.model.indexOf("highAac") < 0, "the AAC quality offered with no encoder");
        tryVerify(() => panel.unavailableLine.visible);
        const hint = panel.unavailableLine.text;
        verify(hint.indexOf("AAC") >= 0 && hint.indexOf("ALAC") >= 0, hint);
        verify(hint.indexOf("RPM Fusion") >= 0 && hint.indexOf("Opus") >= 0, hint);

        // With them, there is nothing to say.
        panel.formatParam = { id: "format", choices: panel.words.formats };
        tryVerify(() => !panel.unavailableLine.visible);
        verify(panel.qualityBox.model.indexOf("highAac") >= 0);
    }

    function test_theFileNameEditorPutsTokensInAndPreviewsTheName() {
        const panel = testCase.makePanel();
        const editor = panel.fileName;
        // The default, previewed with today's date: "Take 2026-09-25 14-03-22.mp3".
        verify(/^Next file: Take \d{4}-\d\d-\d\d \d\d-\d\d-\d\d\.mp3$/.test(editor.previewLabel.text),
               editor.previewLabel.text);

        editor.field.text = "Interview";
        editor.commit();
        compare(SessionModel.param("rec", "fileName"), "Interview");
        editor.insert("%counter");
        compare(SessionModel.param("rec", "fileName"), "Interview %counter");
        tryCompare(editor.previewLabel, "text", "Next file: Interview 001.mp3");

        // What the writer would make safe, the preview makes safe.
        editor.field.text = "Mix 1/2 %session";
        editor.commit();
        tryCompare(editor.previewLabel, "text", "Next file: Mix 1-2 Take.mp3");
        SessionModel.setParam("rec", "format", "flac");
        tryCompare(editor.previewLabel, "text", "Next file: Mix 1-2 Take.flac");
    }

    function test_theTagsAndTheLimitsWriteTheirParameters() {
        const panel = testCase.makePanel();
        panel.section = "limits";
        tryVerify(() => panel.splitCheck.width > 0, 1000);
        mouseClick(panel.splitCheck);
        compare(SessionModel.param("rec", "splitEnabled"), true);
        panel.splitUnitBox.activated(2);
        compare(SessionModel.param("rec", "splitUnit"), "megabytes");
        panel.endUnitBox.activated(0);
        compare(SessionModel.param("rec", "endUnit"), "minutes");
        panel.silenceActionBox.activated(2);
        compare(SessionModel.param("rec", "silenceAction"), "stop");
        panel.silenceSecondsBox.value = 35;
        panel.silenceSecondsBox.valueModified();
        compare(SessionModel.param("rec", "silenceSeconds"), 3.5);
        panel.thresholdSlider.value = -50;
        panel.thresholdSlider.moved();
        compare(SessionModel.param("rec", "silenceThreshold"), -50);

        panel.section = "tags";
        tryVerify(() => panel.tagFields.width > 0, 1000);
        verify(panel.tagFields.visible);
        SessionModel.setParam("rec", "tagArtist", "Анна");
        // A tag written by an undo, a preset or another window shows here.
        tryVerify(() => {
            for (let index = 0; index < panel.tagFields.children.length; ++index) {
                const row = panel.tagFields.children[index];
                if (row.modelData !== undefined && row.modelData.id === "tagArtist") {
                    return row.children[1].text === "Анна";
                }
            }
            return false;
        });
    }
}
