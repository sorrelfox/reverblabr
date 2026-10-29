// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Volume block's face and panel offscreen, against the mock
// engine: that the slider shows a percentage and writes an amplitude, that
// lowering Overdrive brings the stored gain with it, and that the ids the
// interface spells out are the ids the engine offers.

import QtQuick
import QtTest
import org.kde.kirigami as Kirigami
import io.github.reverblabr

TestCase {
    id: testCase

    name: "VolumePanel"
    when: windowShown

    property string sessionId: ""

    // **No parent.** An item's `visible` is its own value and its parent's
    // together, and a test case offscreen is not shown.
    Component {
        id: panelComponent

        VolumePanel {
            width: 320
        }
    }

    Component {
        id: faceComponent

        VolumeFace {
            width: 220
            height: 120
        }
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Volume");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock",
                                              block: { id: "vol", type: "effect.volume" },
                                              index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        // Every test starts from the block's defaults rather than from what
        // the one before it left.
        SessionModel.setParam("vol", "gain", VolumeScale.defaultGain);
        SessionModel.setParam("vol", "overdrive", VolumeScale.defaultOverdrive);
        SessionModel.setParam("vol", "muted", false);
        SessionModel.setBlockEnabled("vol", true);
    }

    function test_theBlockHasAPanelOfItsOwn() {
        verify(BlockPanels.panelFor("effect.volume") !== null, "the Volume block is in the register");
        verify(BlockFaces.faceFor("effect.volume") !== null, "the Volume block has a face");
    }

    function test_theIdsTheInterfaceSpellsOutAreTheOnesTheEngineOffers() {
        // **The one thing that cannot be checked by reading either side.**
        // The interface layer does not depend on the DSP one, so the
        // overdrive ids are written down twice; this is what stops them
        // drifting apart.
        const descriptor = BlockCatalogue.describeParam("effect.volume", "overdrive");
        compare(VolumeScale.overdriveChoices, descriptor.choices);
        compare(VolumeScale.defaultOverdrive, descriptor.default);
        compare(VolumeScale.defaultGain, BlockCatalogue.describeParam("effect.volume", "gain").default);
    }

    function test_theSliderShowsAPercentageAndWritesAnAmplitude() {
        // The criterion: 200 % writes 2.0.
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "vol";
        wait(50);

        SessionModel.setParam("vol", "overdrive", "2x");
        wait(50);
        compare(panel.topPercent, 200);
        compare(panel.slider.to, 200);

        // What letting go of the slider at the top does.
        panel.slider.value = 200;
        panel.slider.moved();
        SessionModel.commitParam("vol", "gain");
        compare(SessionModel.param("vol", "gain"), 2.0);
        compare(panel.percentLabel.text, "200 %");

        // And halfway along is half the signal, not half of something else.
        panel.slider.value = 50;
        panel.slider.moved();
        SessionModel.commitParam("vol", "gain");
        compare(SessionModel.param("vol", "gain"), 0.5);
    }

    function test_loweringOverdriveMovesTheSliderToTheNewTop() {
        // The criterion: lowering Overdrive moves the slider to the new top
        // without the user having to. **What is stored is what is heard** —
        // 350 % under a slider that stops at 200 % would be a gain nobody
        // could see and nobody could undo.
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "vol";
        wait(50);

        SessionModel.setParam("vol", "overdrive", "4x");
        SessionModel.setParam("vol", "gain", 3.5);
        wait(50);
        compare(panel.slider.to, 400);
        compare(panel.slider.value, 350);

        panel.chooseOverdrive("2x");
        wait(50);
        compare(SessionModel.param("vol", "overdrive"), "2x");
        compare(SessionModel.param("vol", "gain"), 2.0);
        compare(panel.slider.to, 200);
        compare(panel.slider.value, 200);

        // Raising it again leaves the gain where it was: the slider grew,
        // the sound did not.
        panel.chooseOverdrive("4x");
        wait(50);
        compare(SessionModel.param("vol", "gain"), 2.0);
        compare(panel.slider.to, 400);
    }

    function test_theButtonsAreTheFourTheBlockOffers() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "vol";
        wait(50);

        compare(panel.overdriveButtons.count, 4);
        const labels = [];
        for (let i = 0; i < panel.overdriveButtons.count; ++i) {
            labels.push(panel.overdriveButtons.itemAt(i).text);
        }
        compare(labels, ["Off", "2×", "3×", "4×"]);

        // Clicking one is what calling the function does, so the button and
        // the keyboard cannot disagree.
        panel.overdriveButtons.itemAt(3).clicked();
        wait(50);
        compare(SessionModel.param("vol", "overdrive"), "4x");
        compare(panel.overdriveButtons.itemAt(3).checked, true);
    }

    function test_thePanelIsLaidOutAsTheDesignDrawsIt() {
        // The name and the number on one line with the number in the
        // accent, the slider under them, and "Overdrive:" beside one
        // segmented row with the chosen one filled. No paragraph.
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "vol";
        wait(50);

        const number = panel.percentLabel.mapToItem(panel, 0, 0);
        const slider = panel.slider.mapToItem(panel, 0, 0);
        verify(number.y + panel.percentLabel.height <= slider.y + 1, "the number is not above the slider");
        verify(Qt.colorEqual(panel.percentLabel.color, AppColors.accentText), "the number is not in the accent");
        verify(panel.slider.width >= panel.width - 1, "the slider does not take the panel's width");

        compare(panel.overdriveTitle.text, "Overdrive:");
        const chosen = panel.overdriveButtons.itemAt(0);
        verify(chosen.checked && chosen.highlighted, "the chosen overdrive is not filled");
        verify(!panel.overdriveButtons.itemAt(1).highlighted, "an overdrive that is not chosen is filled");
        compare(testCase.textsOf(panel).filter(text => text.indexOf("Overdrive raises") >= 0).length, 0,
                "the paragraph about overdrive is still in the panel");
    }

    /// Every piece of visible text in \a item, deep.
    function textsOf(item) {
        let found = [];
        for (const child of item.children) {
            if (!child.visible) {
                continue;
            }
            if (typeof child.text === "string" && child.text.length > 0) {
                found.push(child.text);
            }
            found = found.concat(testCase.textsOf(child));
        }
        return found;
    }

    function test_theMuteIsTheBlocksOwnParameter() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "vol";
        wait(50);

        compare(panel.muteButton.checked, false);
        panel.muteButton.toggle();
        panel.muteButton.toggled();
        wait(50);
        compare(SessionModel.param("vol", "muted"), true);
        // **A mute is not a gain of nothing.** The stored volume is left
        // exactly where it was, and the slider still moves: setting the
        // level to come back to is half of what a mute is for.
        compare(SessionModel.param("vol", "gain"), 1.0);
        compare(panel.slider.enabled, true);
        compare(panel.slider.value, 100);
        compare(panel.muteButton.icon.name, "audio-volume-muted");
    }

    function test_thePanelFollowsAChangeItDidNotMake() {
        // **A control that has been used once still follows the
        // session.** Working a control assigns its own property, which
        // would drop a binding drawing it from the session; without the
        // panel putting the session's answer back, it would stop
        // following an undo, a second window, or a session opened while
        // the panel is showing. All three kinds of control here
        // are covered, because each is written in its own way.
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "vol";
        wait(50);

        panel.muteButton.toggle();
        panel.muteButton.toggled();
        wait(50);
        compare(SessionModel.param("vol", "muted"), true);
        SessionModel.setParam("vol", "muted", false);
        tryVerify(() => !panel.muteButton.checked, 250,
                  "the mute stopped following the session after it was clicked");

        // The slider, which is let go of rather than clicked.
        panel.slider.value = 50;
        panel.slider.moved();
        SessionModel.commitParam("vol", "gain");
        compare(SessionModel.param("vol", "gain"), 0.5);
        SessionModel.setParam("vol", "gain", 1.0);
        tryVerify(() => panel.slider.value === 100, 250,
                  "the volume slider stopped following the session after it was moved");

        // And the overdrive buttons, which are one repeater's delegates and
        // so are written four at a time.
        panel.overdriveButtons.itemAt(3).clicked();
        wait(50);
        compare(SessionModel.param("vol", "overdrive"), "4x");
        SessionModel.setParam("vol", "overdrive", VolumeScale.defaultOverdrive);
        tryVerify(() => panel.overdriveButtons.itemAt(0).checked
                        && !panel.overdriveButtons.itemAt(3).checked, 250,
                  "the overdrive buttons stopped following the session after one was clicked");
    }

    function test_theFaceShowsThePercentageAndTheSameSlider() {
        // A face is glanced at: the number is the largest thing on it, and
        // the slider it offers is the panel's slider, stopping where the
        // panel's stops.
        const face = createTemporaryObject(faceComponent, null);
        face.blockId = "vol";
        SessionModel.setParam("vol", "overdrive", "3x");
        SessionModel.setParam("vol", "gain", 1.5);
        wait(50);

        compare(face.percentLabel.text, "150 %");
        compare(face.slider.to, 300);
        compare(face.slider.value, 150);

        face.slider.value = 300;
        face.slider.moved();
        SessionModel.commitParam("vol", "gain");
        compare(SessionModel.param("vol", "gain"), 3.0);
    }

    function test_theSettingsPanelShowsTheBlocksOwnPanel() {
        // The register is only worth having if the settings panel actually
        // reaches into it and hands the panel its block.
        const panel = createTemporaryObject(settingsComponent, null);
        panel.open("vol");
        wait(50);

        compare(panel.typeId, "effect.volume");
        const body = panel.body.item;
        verify(body !== null, "the panel loaded a body");
        compare(body.blockId, "vol");
    }

    Component {
        id: settingsComponent

        SettingsPanel {
            width: 320
            height: 480
        }
    }

    // --- the face as the design draws it ---

    /// Every item under \a item that has a property called \a name.
    function itemsWith(item, name) {
        let found = [];
        for (const child of item.children) {
            if (child[name] !== undefined) {
                found.push(child);
            }
            found = found.concat(testCase.itemsWith(child, name));
        }
        return found;
    }

    function test_theFaceIsAValueAndASliderInTheEffectsColour() {
        const face = createTemporaryObject(faceComponent, null);
        face.blockId = "vol";
        wait(50);

        // The number in the fixed-width face, and what it is beside it.
        compare(face.percentLabel.text, "100 %");
        // The family the appearance sets numbers in.
        compare(face.percentLabel.font.family, AppFonts.monoFamily);
        compare(face.caption.text, "gain");
        // **Nothing else**: no meter and no icon, which the design leaves
        // to the header's dot and the wires.
        compare(testCase.itemsWith(face, "channelClips").length, 0, "there is still a meter on the face");
        compare(testCase.itemsWith(face, "isMask").length, 0, "there is still an icon on the face");

        // The track filled in the effects colour up to a ring of the same.
        const tint = AppColors.categoryTint(BlockPresentation.category("effect.volume"));
        compare(Qt.colorEqual(face.tint, tint), true);
        compare(Qt.colorEqual(face.slider.handle.border.color, tint), true);
        compare(Qt.colorEqual(face.slider.handle.color, AppColors.surface), true, "the handle is not a ring");
    }

    function test_aBypassedBlocksSliderIsGrey() {
        const face = createTemporaryObject(faceComponent, null);
        face.blockId = "vol";
        SessionModel.setBlockEnabled("vol", false);
        tryVerify(() => !face.blockEnabled, 1000, "the face did not hear that the block is off");
        compare(Qt.colorEqual(face.tint, AppColors.muted), true, "a bypassed block's slider is still coloured");
        compare(Qt.colorEqual(face.slider.handle.border.color, AppColors.muted), true);
        SessionModel.setBlockEnabled("vol", true);
        tryVerify(() => face.blockEnabled, 1000);
    }
}

