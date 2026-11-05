// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Model block's face and panel offscreen, against the mock engine: the
// Beta mark on both, always; and for every state, the word, and the reason
// it is silent. The block is in the engine's catalogue only in a build with
// ONNX Runtime (ADR-0031), so what needs a block is skipped elsewhere; the
// Beta mark needs none, and is checked in every build.

import QtQuick
import QtQuick.Window
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "Model"
    when: windowShown

    property string sessionId: ""
    readonly property string typeId: "effect.model"
    readonly property string betaWords: "Beta: this block is still being debugged and may contain bugs. It is not yet fully tested."

    Component {
        id: panelComponent

        Window {
            property alias panel: inner

            width: 420
            height: 800
            visible: true

            ModelPanel {
                id: inner

                width: 380
            }
        }
    }

    Component {
        id: faceComponent

        Window {
            property var face: holder.item

            width: 200
            height: 120
            visible: true

            Loader {
                id: holder

                anchors.fill: parent
                sourceComponent: BlockRegister.faceFor(testCase.typeId)
            }
        }
    }

    Component {
        id: unmarkedBadge

        BetaBadge {
            typeId: "effect.volume"
        }
    }

    function panelFor() {
        const panel = createTemporaryObject(panelComponent, null).panel;
        panel.blockId = "m";
        return panel;
    }

    function faceFor() {
        const face = createTemporaryObject(faceComponent, null).face;
        face.blockId = "m";
        return face;
    }

    function hasBlock() {
        return BlockCatalogue.types.indexOf(testCase.typeId) >= 0;
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Model");
        verify(testCase.sessionId !== "", "the mock opened a session");
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
        if (testCase.hasBlock()) {
            TestEngine.applyEdit(testCase.sessionId,
                                 JSON.stringify({ op: "addBlock", block: { id: "m", type: testCase.typeId }, index: -1 }));
        }
    }

    function cleanup() {
        TestEngine.stop(testCase.sessionId);
        Meters.watch("");
    }

    function run(values) {
        Meters.watch(testCase.sessionId);
        TestEngine.start(testCase.sessionId);
        tryVerify(() => Meters.live, 1000, "the session never went live");
        TestEngine.setFakeBlockValues(testCase.sessionId, "m", values);
    }

    function needTheBlock() {
        if (!testCase.hasBlock()) {
            skip("built without ONNX Runtime");
        }
    }

    function test_itHasAFaceAPanelAndHelpOfItsOwn() {
        verify(BlockRegister.panelFor(testCase.typeId) !== null, "the Model is in the panel register");
        verify(BlockRegister.faceFor(testCase.typeId) !== null, "the Model is in the face register");
    }

    // --- the Beta mark ---

    function test_theTileAlwaysSaysBeta() {
        const face = testCase.faceFor();
        verify(face.betaBadge.visible, "the badge shows");
        compare(face.betaBadge.Accessible.name, testCase.betaWords);
    }

    function test_theCardAlwaysSaysBeta() {
        const panel = testCase.panelFor();
        verify(panel.betaLine.visible, "the line shows");
        compare(panel.betaLine.text, testCase.betaWords);
        compare(panel.betaLine.Accessible.name, testCase.betaWords);
    }

    function test_theLibrarysTooltipSaysBeta() {
        verify(BlockPresentation.isBeta(testCase.typeId));
        verify(BlockPresentation.tooltip(testCase.typeId).indexOf(testCase.betaWords) >= 0);
        verify(!BlockPresentation.isBeta("effect.volume"), "no other block is");
        verify(BlockPresentation.tooltip("effect.volume").indexOf("Beta") < 0);
    }

    function test_theBadgeFollowsTheFlagAndNothingElse() {
        const badge = createTemporaryObject(unmarkedBadge, testCase);
        verify(badge !== null);
        verify(!badge.shown, "a block with no flag has no badge");
    }

    // --- every state, and why it is silent ---

    function test_noFolderIsSaid() {
        testCase.needTheBlock();
        SessionModel.setParam("m", "folder", "");
        testCase.run([0, 0, 0, 0]);
        const face = testCase.faceFor();
        const panel = testCase.panelFor();
        tryCompare(face.captionLabel, "text", "No model");
        tryVerify(() => panel.silenceLabel.text.indexOf("Silent: no model is chosen") === 0);
        verify(panel.silenceLabel.visible);
        compare(panel.folderLabel.text, "No folder chosen");
        verify(!panel.tryAgainButton.visible);
    }

    function test_everyStateSaysWhyItIsSilent_data() {
        return [
            { tag: "loading", values: [1, 0, 0, 0], word: "Loading", silent: "Silent: the model is still loading." },
            { tag: "cannot keep up", values: [3, 63, 140, 0], word: "Cannot keep up",
              silent: "Silent for now: the model is not finishing in time, and nothing else may leave this block." },
            { tag: "a cycle too long", values: [3, 63, 10, 16], word: "Cannot keep up",
              silent: "Silent: the session's cycle is longer than Spare time allows. Choose a longer Spare time." },
            { tag: "failed", values: [4, 0, 0, 6], word: "Failed",
              silent: "Silent: The folder only holds files that can run code when opened (.pt, .pth, .ckpt, .pkl). They are never opened. Use an ONNX version of the model." },
            { tag: "crashing", values: [4, 0, 0, 11], word: "Failed",
              silent: "Silent: The model's process crashed three times in a minute." },
            { tag: "restarting", values: [5, 63, 0, 0], word: "Restarting",
              silent: "Silent: the model's process stopped, and it is being started again." },
            { tag: "bad output", values: [6, 63, 10, 0], word: "Bad output",
              silent: "Silent: the model gave back something that is not sound." },
        ];
    }

    function test_everyStateSaysWhyItIsSilent(data) {
        testCase.needTheBlock();
        SessionModel.setParam("m", "folder", "/somewhere/voice");
        testCase.run(data.values);
        const face = testCase.faceFor();
        const panel = testCase.panelFor();
        tryCompare(face.captionLabel, "text", data.word);
        tryCompare(panel.stateLabel, "text", data.word);
        tryCompare(panel.silenceLabel, "text", data.silent);
        compare(face.headlineLabel.text, "voice");
        compare(face.detail, data.silent);
        compare(panel.tryAgainButton.visible, data.word === "Failed");
    }

    function test_runningSaysItsDelayAndNothingAboutSilence() {
        testCase.needTheBlock();
        SessionModel.setParam("m", "folder", "/somewhere/voice");
        testCase.run([2, 63.2, 34, 1]);
        const face = testCase.faceFor();
        const panel = testCase.panelFor();
        tryCompare(face.captionLabel, "text", "Running, 63 ms");
        tryCompare(panel.stateLabel, "text", "Running");
        tryCompare(panel.latencyLabel, "text", "63.2 ms");
        tryCompare(panel.loadLabel, "text", "34 %");
        compare(panel.silenceLabel.text, "");
        verify(!panel.silenceLabel.visible);
    }

    function test_switchedOffItSaysSoWhateverTheModelIsDoing() {
        testCase.needTheBlock();
        SessionModel.setParam("m", "folder", "/somewhere/voice");
        testCase.run([2, 63, 34, 1]);
        const panel = testCase.panelFor();
        const face = testCase.faceFor();
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "setEnabled", blockId: "m", enabled: false }));
        tryCompare(panel.stateLabel, "text", "Off");
        tryCompare(face.captionLabel, "text", "Off");
        compare(panel.silenceLabel.text, "Silent: this block is switched off. No sound passes through it.");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "setEnabled", blockId: "m", enabled: true }));
        tryCompare(panel.stateLabel, "text", "Running");
    }

    function test_aStoppedSessionIsSaid() {
        testCase.needTheBlock();
        SessionModel.setParam("m", "folder", "/somewhere/voice");
        const panel = testCase.panelFor();
        compare(panel.stateLabel.text, "Not running");
        compare(panel.silenceLabel.text, "Silent: the session is not running.");
    }

    // --- what the folder holds ---

    function answered(panel, text) {
        tryVerify(() => !panel.folderReader.busy, 6000);
        panel.folderReader.takeAnswer(text);
    }

    function test_theFolderIsDescribedAndItsLicenceNamed() {
        testCase.needTheBlock();
        SessionModel.setParam("m", "folder", "/somewhere/voice");
        const panel = testCase.panelFor();
        testCase.answered(panel, "refusal=0\nfamily=2\nmodelFile=denoiser.onnx\nlicenceFile=LICENSE\nsampleRate=48000\nchannels=1\nhopSize=480\n");
        compare(panel.descriptionLabel.text, "DeepFilterNet noise cleanup, denoiser.onnx, 48,000 Hz, 1 channel");
        compare(panel.licenceLabel.text, "Licence file: LICENSE");
        verify(panel.openLicenceButton.visible);
        verify(!panel.refusalLabel.visible);
    }

    function test_noLicenceFileIsSaid() {
        testCase.needTheBlock();
        SessionModel.setParam("m", "folder", "/somewhere/voice");
        const panel = testCase.panelFor();
        testCase.answered(panel, "refusal=0\nfamily=1\nmodelFile=m.onnx\nsampleRate=16000\nchannels=2\n");
        compare(panel.licenceLabel.text, "No licence file in this folder. Check the model's terms where you got it.");
        verify(!panel.openLicenceButton.visible);
    }

    function test_aRefusedFolderSaysWhy_data() {
        const reasons = [];
        for (let reason = 1; reason <= 20; ++reason) {
            // 16 is about the session's cycle, and no folder gives it.
            if (reason !== 16) {
                reasons.push({ tag: "reason " + reason, refusal: reason });
            }
        }
        return reasons;
    }

    function test_aRefusedFolderSaysWhy(data) {
        testCase.needTheBlock();
        SessionModel.setParam("m", "folder", "/somewhere/voice");
        const panel = testCase.panelFor();
        testCase.answered(panel, "refusal=" + data.refusal + "\n");
        verify(panel.refusalLabel.visible);
        verify(panel.refusalLabel.text !== "" && panel.refusalLabel.text !== "The model will not run.");
    }

    function test_familiesNotBuiltAreNamed() {
        testCase.needTheBlock();
        SessionModel.setParam("m", "folder", "/somewhere/voice");
        const panel = testCase.panelFor();
        testCase.answered(panel, "refusal=8\nfamily=4\n");
        compare(panel.descriptionLabel.text, "Voice conversion");
        compare(panel.refusalLabel.text, "This kind of model is recognised, but this version cannot run it yet.");
    }

    function test_aVoiceNamesItsFilesAndHasItsOwnSetting() {
        testCase.needTheBlock();
        SessionModel.setParam("m", "folder", "/somewhere/voice");
        const panel = testCase.panelFor();
        testCase.answered(panel, "refusal=0\nfamily=4\nmodelFile=voice.onnx\ncontentFile=encoder.onnx\npitchFile=estimator.onnx\nsampleRate=40000\nchannels=1\nhopSize=2560\n");
        compare(panel.descriptionLabel.text, "Voice conversion, voice.onnx, 40,000 Hz, 1 channel");
        compare(panel.voiceFilesLabel.text, "Content encoder: encoder.onnx. Pitch estimator: estimator.onnx.");
        verify(panel.voiceFilesLabel.visible);
        verify(panel.pitchRow.visible);
        verify(!panel.monoRow.visible, "a voice always runs once, on the two sides mixed");
    }

    function test_aModelThatIsNoVoiceHasNoVoiceSetting() {
        testCase.needTheBlock();
        SessionModel.setParam("m", "folder", "/somewhere/voice");
        const panel = testCase.panelFor();
        testCase.answered(panel, "refusal=0\nfamily=2\nmodelFile=denoiser.onnx\nsampleRate=48000\nchannels=1\nhopSize=480\n");
        verify(!panel.voiceFilesLabel.visible);
        verify(!panel.pitchRow.visible);
        verify(panel.monoRow.visible);
    }

    function test_aVoiceThatCannotRunHasNoVoiceSetting() {
        testCase.needTheBlock();
        SessionModel.setParam("m", "folder", "/somewhere/voice");
        const panel = testCase.panelFor();
        testCase.answered(panel, "refusal=18\nfamily=4\n");
        verify(panel.refusalLabel.visible);
        verify(!panel.voiceFilesLabel.visible);
        verify(!panel.pitchRow.visible);
    }

    function test_tryAgainPressesOnce() {
        testCase.needTheBlock();
        SessionModel.setParam("m", "folder", "/somewhere/voice");
        testCase.run([4, 0, 0, 11]);
        const panel = testCase.panelFor();
        tryVerify(() => panel.tryAgainButton.visible);
        panel.tryAgainButton.clicked();
        compare(panel.presses, 1);
    }

    function test_theChoicesHaveWords() {
        testCase.needTheBlock();
        for (const choice of ["20ms", "50ms", "100ms", "250ms"]) {
            verify(ChoiceWords.hasWords(testCase.typeId, "spareTime", choice), choice);
        }
    }
}
