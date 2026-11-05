// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The FIR Filter block's face and panel offscreen, against the mock
// engine, with real files for it to read: what they say with no file, a
// file while stopped and running, one it cannot use; choosing and removing
// a file; and the settings.

import QtCore
import QtQuick
import QtQuick.Window
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "FirFilter"
    when: windowShown

    property string sessionId: ""
    property string stereo: ""
    property string trueStereo: ""
    property string threeChannels: ""
    property string lowRate: ""

    // In a window of its own, so that what it shows and hides is shown and
    // hidden.
    Component {
        id: panelComponent

        Window {
            property alias panel: inner

            width: 420
            height: 700
            visible: true

            FirFilterPanel {
                id: inner

                width: 380
            }
        }
    }

    function panelFor() {
        const panel = createTemporaryObject(panelComponent, null).panel;
        panel.blockId = "fir";
        return panel;
    }

    function faceFor() {
        const face = createTemporaryObject(BlockFaces.faceFor("advanced.firfilter"), testCase, { width: 180, height: 90 });
        face.blockId = "fir";
        return face;
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("FIR Filter");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "fir", type: "advanced.firfilter" }, index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
        testCase.stereo = TestEngine.writeFakeImpulse("hall.wav", 2, 0.5);
        testCase.trueStereo = TestEngine.writeFakeImpulse("room-true-stereo.wav", 4, 0.25);
        testCase.threeChannels = TestEngine.writeFakeImpulse("three.wav", 3, 0.1);
        testCase.lowRate = TestEngine.writeFakeImpulse("cabinet.wav", 1, 0.1, 44100);
        verify(testCase.stereo !== "" && testCase.trueStereo !== "" && testCase.threeChannels !== ""
               && testCase.lowRate !== "", "the mock wrote the files");
    }

    function init() {
        SessionModel.setParam("fir", "file", "");
        SessionModel.setParam("fir", "normalise", true);
        SessionModel.setParam("fir", "trim", false);
        SessionModel.setParam("fir", "preGainDb", 0);
    }

    function cleanup() {
        TestEngine.stop(testCase.sessionId);
        Meters.watch("");
    }

    function run() {
        Meters.watch(testCase.sessionId);
        TestEngine.start(testCase.sessionId);
        tryVerify(() => Meters.live, 1000, "the session never went live");
    }

    function test_theBlockHasAFaceAndAPanelOfItsOwn() {
        verify(BlockPanels.panelFor("advanced.firfilter") !== null, "the FIR Filter is in the panel register");
        verify(BlockFaces.faceFor("advanced.firfilter") !== null, "the FIR Filter is in the face register");
    }

    function test_theRangesAreTheEnginesOwn() {
        const panel = testCase.panelFor();
        const preGain = BlockCatalogue.describeParam("advanced.firfilter", "preGainDb");
        compare(panel.lowestPreGain, preGain.minimum);
        compare(panel.preGainSlider.from, preGain.minimum);
        compare(panel.preGainSlider.to, preGain.maximum);
        compare(panel.defaultNormalise, BlockCatalogue.describeParam("advanced.firfilter", "normalise").default);
        compare(panel.defaultTrim, BlockCatalogue.describeParam("advanced.firfilter", "trim").default);
    }

    function test_withoutAFileItSaysSo() {
        const face = testCase.faceFor();
        const panel = testCase.panelFor();
        compare(face.headlineLabel.text, "No impulse response");
        compare(face.captionLabel.text, "Passes sound through");
        compare(panel.fileLabel.text, "No file chosen");
        verify(!panel.removeButton.visible, "there is a file to remove");
        compare(panel.delayLabel.text, "256 samples");
    }

    function test_aFileIsNamedWhileStoppedAndMeasuredWhileRunning() {
        SessionModel.setParam("fir", "file", testCase.stereo);
        const face = testCase.faceFor();
        const panel = testCase.panelFor();
        compare(face.headlineLabel.text, "hall.wav");
        compare(face.captionLabel.text, "Impulse response");
        compare(panel.fileLabel.text, "hall.wav");
        verify(panel.removeButton.visible, "a file cannot be removed");
        verify(!panel.descriptionLabel.visible, "there is a description with nothing measured");

        testCase.run();
        tryCompare(face.captionLabel, "text", "24,000 taps");
        compare(panel.descriptionLabel.text, "Stereo, 0.50 s, 24,000 taps");
        compare(panel.delayLabel.text, "5.3 ms (256 samples)");
    }

    function test_fourChannelsAreTrueStereoAndAnotherRateIsTheSessions() {
        const panel = testCase.panelFor();
        SessionModel.setParam("fir", "file", testCase.trueStereo);
        testCase.run();
        tryCompare(panel.descriptionLabel, "text", "True stereo, 0.25 s, 12,000 taps");
        // 0.1 s at 44.1 kHz is 4,800 taps at the session's 48.
        SessionModel.setParam("fir", "file", testCase.lowRate);
        tryCompare(panel.descriptionLabel, "text", "Mono, 0.10 s, 4,800 taps");
    }

    function test_aFileItCannotUseIsSaid() {
        const face = testCase.faceFor();
        const panel = testCase.panelFor();
        testCase.run();
        for (const path of [testCase.threeChannels, testCase.stereo + ".missing"]) {
            SessionModel.setParam("fir", "file", path);
            tryCompare(face.captionLabel, "text", "Cannot use this file");
            verify(panel.descriptionLabel.text.startsWith("The file is missing"), panel.descriptionLabel.text);
        }
    }

    function test_choosingAFileKeepsTheHomeFolderAsATilde() {
        const panel = testCase.panelFor();
        const home = StandardPaths.writableLocation(StandardPaths.HomeLocation);
        panel.reading.choose(home + "/Impulses/plate.wav");
        compare(SessionModel.param("fir", "file"), "~/Impulses/plate.wav");
        compare(panel.fileLabel.text, "plate.wav");
        panel.reading.choose("file:///srv/impulses/plate.wav");
        compare(SessionModel.param("fir", "file"), "/srv/impulses/plate.wav");
    }

    function test_removingTheFileLeavesNone() {
        SessionModel.setParam("fir", "file", testCase.stereo);
        const panel = testCase.panelFor();
        panel.removeButton.clicked();
        compare(SessionModel.param("fir", "file"), "");
        compare(panel.fileLabel.text, "No file chosen");
    }

    function test_theSettingsWriteWhatTheyShow() {
        const panel = testCase.panelFor();
        verify(panel.normaliseBox.checked, "matching the loudness is not on by default");
        verify(!panel.trimBox.checked, "trimming is on by default");
        panel.normaliseBox.toggle();
        panel.normaliseBox.toggled();
        compare(SessionModel.param("fir", "normalise"), false);
        panel.trimBox.toggle();
        panel.trimBox.toggled();
        compare(SessionModel.param("fir", "trim"), true);

        panel.preGainSlider.value = -6;
        panel.preGainSlider.moved();
        SessionModel.commitParam("fir", "preGainDb");
        compare(SessionModel.param("fir", "preGainDb"), -6);
        compare(panel.preGainLabel.text, "-6.0 dB");
    }
}
