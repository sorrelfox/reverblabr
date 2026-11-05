// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Simple Compressor's face and panel: a mode by name where the
// engine has an id, the numbers a mode sets read from the block, and the
// one control writing the setting it names.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "SimpleCompressor"

    property string sessionId: ""

    Component {
        id: faceComponent

        SimpleCompressorFace {
            width: 180
            height: 90
        }
    }

    Component {
        id: panelComponent

        SimpleCompressorPanel {
            width: 340
        }
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Simple Compressor");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "cmp", type: "effect.simplecompressor" },
                                              index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParam("cmp", "mode", "gentle");
    }

    function test_itIsInTheEffectsWithAFaceAndAPanelOfItsOwn() {
        verify(BlockCatalogue.types.indexOf("effect.simplecompressor") >= 0, "the engine does not offer it");
        compare(BlockPresentation.category("effect.simplecompressor"), "effects");
        compare(BlockPresentation.name("effect.simplecompressor"), "Simple Compressor");
        verify(BlockFaces.faceFor("effect.simplecompressor") !== null, "it has no face");
        verify(BlockPanels.panelFor("effect.simplecompressor") !== null, "it has no panel");
    }

    function test_theModesAreTheOnesTheEngineOffersEachWithWords() {
        const panel = createTemporaryObject(panelComponent, null);
        const words = panel.words;
        compare(words.modes, BlockCatalogue.describeParam("effect.simplecompressor", "mode").choices);
        compare(BlockCatalogue.describeParam("effect.simplecompressor", "mode").default, CompressorModes.defaultId);
        for (const mode of words.modes) {
            verify(words.modeName(mode) !== mode, mode + " has no name, only its id");
            verify(words.explanation(mode) !== "", mode + " has no explanation");
        }
        compare(words.modes.map(mode => words.modeName(mode)),
                ["Off", "Gentle", "Moderate", "Heavy", "Voice", "Broadcast"]);
    }

    function test_theFaceSaysTheModeAndHowHardItWorks() {
        const face = createTemporaryObject(faceComponent, null);
        face.blockId = "cmp";
        wait(50);
        // The design's own example, and the block's default.
        compare(face.headlineLabel.text, "Gentle");
        compare(face.captionLabel.text, "2:1 · -18 dB");
        compare(face.detail, face.words.explanation("gentle"));

        SessionModel.setParam("cmp", "mode", "broadcast");
        tryVerify(() => face.headlineLabel.text === "Broadcast", 1000);
        compare(face.captionLabel.text, "10:1 · -16 dB");

        SessionModel.setParam("cmp", "mode", "off");
        tryVerify(() => face.headlineLabel.text === "Off", 1000);
        compare(face.captionLabel.text, "no compression");
    }

    function test_theListWritesTheModeByItsId() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "cmp";
        wait(50);
        compare(panel.modeBox.currentIndex, panel.words.modes.indexOf("gentle"));
        compare(panel.modeBox.displayText, "Gentle");

        panel.modeBox.currentIndex = panel.words.modes.indexOf("voice");
        panel.modeBox.activated(panel.modeBox.currentIndex);
        compare(SessionModel.param("cmp", "mode"), "voice");
        tryCompare(panel.modeBox, "displayText", "Voice");
        compare(panel.explanationLine.text, panel.words.explanation("voice"));

        // And from elsewhere — undo, a preset, the tile — the list follows.
        SessionModel.setParam("cmp", "mode", "heavy");
        tryCompare(panel.modeBox, "currentIndex", panel.words.modes.indexOf("heavy"));
        compare(panel.explanationLine.text, panel.words.explanation("heavy"));
    }

    function test_theSettingsShownAreTheBlocksOwnNumbers() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "cmp";
        verify(!panel.detailsSection.expanded, "the numbers are shown before anybody asked for them");
        panel.detailsSection.expanded = true;

        SessionModel.setParam("cmp", "mode", "heavy");
        tryCompare(panel.detailRows, "count", 7);
        const shown = [];
        for (let i = 0; i < panel.detailRows.count; ++i) {
            shown.push(panel.detailRows.itemAt(i).Accessible.name);
        }
        // What the block's table says for Heavy, written the way a person
        // reads it; every one of them read from the block, none typed here.
        compare(shown, ["Threshold: -24 dB", "Ratio: 6:1", "Knee: 6 dB", "Attack: 5 ms", "Release: 150 ms",
                        "Makeup gain: +8 dB", "Follows: Peaks"]);

        SessionModel.setParam("cmp", "mode", "gentle");
        tryVerify(() => panel.detailRows.itemAt(0).Accessible.name === "Threshold: -18 dB", 1000);
        compare(panel.detailRows.itemAt(6).Accessible.name, "Follows: Average (RMS)");

        // Off sets nothing, and says so rather than listing a flat curve.
        SessionModel.setParam("cmp", "mode", "off");
        tryCompare(panel.detailRows, "count", 0);
    }
}
