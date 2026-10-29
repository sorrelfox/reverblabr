// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The two device blocks' faces and panels offscreen, against
// the mock engine: that the pickers list what the engine says is there and
// write a node name rather than a position, that a volume slider set to
// 50 % writes an amplitude of a half, that the channel pickers follow the
// chosen device, and that a device unplugged reads as unavailable and stops
// reading so when it comes back; and that the face register hands both
// device blocks a component a bare Loader can draw.

import QtQuick
import QtQuick.Window
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "DevicePanels"
    when: windowShown

    property string sessionId: ""

    /// How long the interface has to notice a device coming or going. The
    /// task asks for a quarter of a second.
    readonly property int noticeMs: 250

    // **No parent.** An item's `visible` is its own value and its parent's
    // together, and a test case offscreen is not shown.
    Component {
        id: outputPanelComponent

        OutputDevicePanel {
            width: 320
        }
    }

    Component {
        id: inputPanelComponent

        InputDevicePanel {
            width: 320
        }
    }

    // **In a window of its own**, for the one test that opens a list: a
    // popup needs a window to open in.
    Component {
        id: shownInputPanelComponent

        Window {
            property alias panel: shownPanel

            width: 400
            height: 400
            visible: true

            InputDevicePanel {
                id: shownPanel

                width: 320
                blockId: "in"
            }
        }
    }

    Component {
        id: faceComponent

        DeviceFace {}
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Devices");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock",
                                              block: { id: "out", type: "output.device" },
                                              index: -1 }));
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock",
                                              block: { id: "in", type: "source.device" },
                                              index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        // Every test starts from the same made-up machine, whatever the
        // last one unplugged or chose.
        TestEngine.addFakeInventory();
        SessionModel.setParam("out", "device", "default");
        SessionModel.setParam("in", "device", "default");
        SessionModel.setParam("out", "leftChannel", "default");
        SessionModel.setParam("out", "rightChannel", "default");
        SessionModel.setParam("out", "volume", 1);
        tryVerify(() => Inventory.ready, 1000, "the engine answered with an inventory");
    }

    /// The right-hand edge of \a item, in \a panel's coordinates.
    function rightEdge(item, panel) {
        return item.mapToItem(panel, item.width, 0).x;
    }

    function test_theOutputPanelIsLaidOutAsTheDesignDrawsIt() {
        // "Audio device" against its box, "Output volume" with the
        // number in the accent over a slider at full width, and the two
        // channels under a folded "Advanced", their labels lined up with the
        // device's.
        const panel = createTemporaryObject(outputPanelComponent, null);
        panel.blockId = "out";
        wait(50);

        compare(panel.devicePicker.label.text, "Audio device");
        verify(Qt.colorEqual(panel.devicePicker.label.color, AppColors.muted), "the device's label is not muted");
        compare(panel.devicePicker.label.horizontalAlignment, Text.AlignRight);

        compare(panel.volumeLabel.text, "100 %");
        verify(Qt.colorEqual(panel.volumeLabel.color, AppColors.accentText), "the volume is not in the accent");
        const number = panel.volumeLabel.mapToItem(panel, 0, 0);
        const slider = panel.volumeSlider.mapToItem(panel, 0, 0);
        verify(number.y + panel.volumeLabel.height <= slider.y + 1, "the volume is not over its slider");
        verify(panel.volumeSlider.width >= panel.width - 1, "the slider does not take the panel's width");

        compare(panel.advancedSection.title, "Advanced");
        compare(panel.advancedSection.expanded, false, "Advanced starts open");
        panel.advancedSection.toggle.toggle();
        panel.advancedSection.toggle.toggled();
        compare(panel.advancedSection.expanded, true, "pressing the heading did not open Advanced");
        compare(panel.channelRows.leftLabel.text, "Left channel");
        compare(panel.channelRows.rightLabel.text, "Right channel");
        fuzzyCompare(testCase.rightEdge(panel.channelRows.leftLabel, panel),
                     testCase.rightEdge(panel.devicePicker.label, panel), 1,
                     "the channel labels do not line up with the device's");
    }

    function test_theInputPanelIsLaidOutAsTheDesignDrawsIt() {
        // The same form without a volume.
        const panel = createTemporaryObject(inputPanelComponent, null);
        panel.blockId = "in";
        wait(50);

        compare(panel.devicePicker.label.text, "Audio device");
        compare(panel.advancedSection.title, "Advanced");
        compare(panel.advancedSection.expanded, false, "Advanced starts open");
        panel.advancedSection.toggle.toggle();
        panel.advancedSection.toggle.toggled();
        compare(panel.advancedSection.expanded, true);
        compare(panel.channelRows.leftLabel.text, "Left channel");
        fuzzyCompare(testCase.rightEdge(panel.channelRows.rightLabel, panel),
                     testCase.rightEdge(panel.devicePicker.label, panel), 1,
                     "the channel labels do not line up with the device's");
    }

    function test_theOutputComboListsTheLiveSinksAndWritesANodeName() {
        // The criterion: the combo lists the live sinks and writes
        // `node.name`, or "default", into the block.
        const panel = createTemporaryObject(outputPanelComponent, null, { blockId: "out" });
        const combo = panel.devicePicker.combo;

        compare(panel.devicePicker.entries.map(e => e.value),
                ["default", "alsa_output.hdmi", "alsa_output.analog"]);
        compare(panel.devicePicker.entries[0].label, "Default output");
        // What the engine calls them, not what it names them.
        compare(panel.devicePicker.entries.map(e => e.label),
                ["Default output", "HDMI", "Headphones"]);

        combo.currentIndex = 1;
        combo.activated(1);
        compare(SessionModel.param("out", "device"), "alsa_output.hdmi");

        combo.currentIndex = 0;
        combo.activated(0);
        compare(SessionModel.param("out", "device"), "default");
    }

    function test_theInputComboListsTheLiveInputsAndWritesANodeName() {
        const panel = createTemporaryObject(inputPanelComponent, null, { blockId: "in" });
        const combo = panel.devicePicker.combo;

        compare(panel.devicePicker.entries.map(e => e.value), ["default", "alsa_input.usb-Microphone"]);
        compare(panel.devicePicker.entries[0].label, "Default input");

        combo.currentIndex = 1;
        combo.activated(1);
        compare(SessionModel.param("in", "device"), "alsa_input.usb-Microphone");

        combo.currentIndex = 0;
        combo.activated(0);
        compare(SessionModel.param("in", "device"), "default");
    }

    function test_thePanelFollowsAChangeItDidNotMake() {
        // **A combo that has been used once still follows the session.**
        // Choosing an entry assigns `currentIndex` from C++, which would
        // drop a binding drawing it from the session; without the picker
        // putting the session's answer back, the combo would stop
        // following an undo, a second window, or a session opened while
        // the panel is showing.
        const panel = createTemporaryObject(outputPanelComponent, null, { blockId: "out" });
        const combo = panel.devicePicker.combo;

        combo.currentIndex = 1;
        combo.activated(1);
        compare(SessionModel.param("out", "device"), "alsa_output.hdmi");

        SessionModel.setParam("out", "device", "default");
        tryVerify(() => combo.currentIndex === 0, testCase.noticeMs,
                  "the device combo stopped following the session after it was used");
    }

    function test_virtualDevicesComeLastUnderAHeadingOfTheirOwn() {
        // A virtual output is recorded from and a virtual microphone
        // played into, and both are listed after the cards, under a heading
        // that has no value of its own.
        TestEngine.addFakeVirtualDevices();
        const output = createTemporaryObject(outputPanelComponent, null, { blockId: "out" });
        tryVerify(() => output.devicePicker.entries.length === 6, testCase.noticeMs,
                  "the virtual devices were not offered to play to");
        compare(output.devicePicker.entries.map(e => e.value),
                ["default", "alsa_output.hdmi", "alsa_output.analog", undefined,
                 "virtual.everyapp", "virtual.processed"]);
        compare(output.devicePicker.entries[3].heading, true);
        compare(output.devicePicker.entries[3].label, "Virtual devices");
        compare(output.devicePicker.entries[5].label, "Processed microphone");

        const input = createTemporaryObject(inputPanelComponent, null, { blockId: "in" });
        compare(input.devicePicker.entries.map(e => e.value),
                ["default", "alsa_input.usb-Microphone", undefined, "virtual.everyapp", "virtual.processed"]);
        compare(input.devicePicker.entries[3].label, "Every app");

        // Without any, there is no heading over nothing.
        TestEngine.addFakeInventory();
        tryVerify(() => input.devicePicker.entries.length === 2, testCase.noticeMs,
                  "the heading outlived the virtual devices");
    }

    function test_aVirtualOutputIsChosenByItsNodeName() {
        TestEngine.addFakeVirtualDevices();
        const panel = createTemporaryObject(inputPanelComponent, null, { blockId: "in" });
        const combo = panel.devicePicker.combo;
        tryVerify(() => panel.devicePicker.entries.length === 5, testCase.noticeMs);

        combo.currentIndex = 3;
        combo.activated(3);
        compare(SessionModel.param("in", "device"), "virtual.everyapp");
    }

    function test_theHeadingIsSteppedOverAndCannotBeClicked() {
        TestEngine.addFakeVirtualDevices();
        const panel = createTemporaryObject(shownInputPanelComponent, testCase).panel;
        const combo = panel.devicePicker.combo;
        tryVerify(() => panel.devicePicker.entries.length === 5, testCase.noticeMs);

        // Going down from the default, the arrow lands on the heading and
        // goes on to the first virtual device.
        combo.currentIndex = 2;
        combo.activated(2);
        compare(SessionModel.param("in", "device"), "virtual.everyapp", "the heading was written as a device");
        tryCompare(combo, "currentIndex", 3);

        // Going up from there, it goes on to the card above the heading.
        combo.currentIndex = 2;
        combo.activated(2);
        compare(SessionModel.param("in", "device"), "alsa_input.usb-Microphone");
        tryCompare(combo, "currentIndex", 1);

        // A pointer cannot choose it at all: its row is not a button that
        // works, and it says it is a heading.
        combo.popup.open();
        tryVerify(() => combo.popup.opened, testCase.noticeMs, "the list did not open");
        const list = combo.popup.contentItem;
        tryVerify(() => list.itemAtIndex(2) !== null, testCase.noticeMs, "the heading's row was never made");
        const heading = list.itemAtIndex(2);
        verify(!heading.enabled, "the heading can be clicked");
        compare(heading.Accessible.role, Accessible.Heading);
        verify(Qt.colorEqual(heading.contentItem.color, AppColors.muted), "the heading is not muted");
        verify(list.itemAtIndex(3).enabled, "a virtual device cannot be chosen");
        combo.popup.close();
    }

    function test_theListIsLiveWhileThePanelIsOpen() {
        const panel = createTemporaryObject(outputPanelComponent, null, { blockId: "out" });

        TestEngine.setFakeDevicePresent("alsa_output.hdmi", false);
        tryVerify(() => panel.devicePicker.entries.length === 2, testCase.noticeMs,
                  "a device that went away was still in the picker");
        TestEngine.setFakeDevicePresent("alsa_output.hdmi", true);
        tryVerify(() => panel.devicePicker.entries.length === 3, testCase.noticeMs,
                  "a device that came back was not in the picker");
    }

    function test_theVolumeSliderShowsAPercentageAndWritesAnAmplitude() {
        // The criterion: 50 % writes 0.5. Half the signal is −6.02 dB, and
        // neither of those is a number to put beside a slider.
        const panel = createTemporaryObject(outputPanelComponent, null, { blockId: "out" });

        compare(panel.amplitudeOf(50), 0.5);
        compare(panel.percentOf(0.5), 50);
        compare(panel.amplitudeOf(100), 1);
        compare(panel.amplitudeOf(0), 0);

        // The slider reads the session back as a percentage.
        SessionModel.setParam("out", "volume", 0.25);
        tryVerify(() => Math.round(panel.volumeSlider.value) === 25, 500,
                  "a quarter of the signal did not read as 25 %");

        // And through the slider itself, which is what a person moves.
        // **On its own panel**, because setting a slider's value from a
        // test is what breaks the binding that reads the session back.
        const moved = createTemporaryObject(outputPanelComponent, null, { blockId: "out" });
        moved.volumeSlider.value = 50;
        moved.volumeSlider.moved();
        compare(SessionModel.param("out", "volume"), 0.5);
        compare(typeof SessionModel.param("out", "volume"), "number");
    }

    function test_theChannelCombosListTheChosenDevicesOwnChannels() {
        // The criterion: the channel combos list the chosen device's own
        // channels, by the names its ports carry.
        const panel = createTemporaryObject(outputPanelComponent, null, { blockId: "out" });

        // The default output is a stereo pair.
        compare(panel.channelRows.leftCombo.model.map(e => e.value), ["default", "FL", "FR"]);
        compare(panel.channelRows.leftCombo.model[0].label, "Default");

        // The surround card has six, and the picker follows the choice.
        SessionModel.setParam("out", "device", "alsa_output.hdmi");
        tryVerify(() => panel.channelRows.leftCombo.model.length === 7, testCase.noticeMs,
                  "the channel picker did not follow the chosen device");
        compare(panel.channelRows.rightCombo.model.map(e => e.value),
                ["default", "FL", "FR", "FC", "LFE", "RL", "RR"]);

        // And a channel chosen travels as its name.
        panel.channelRows.rightCombo.currentIndex = 6;
        panel.channelRows.rightCombo.activated(6);
        compare(SessionModel.param("out", "rightChannel"), "RR");
    }

    function test_theInputChannelCombosFollowTheChosenMicrophone() {
        const panel = createTemporaryObject(inputPanelComponent, null, { blockId: "in" });

        // A mono microphone has one channel, and "Default" is still the
        // first thing offered: the natural mapping is not a channel.
        compare(panel.channelRows.leftCombo.model.map(e => e.value), ["default", "MONO"]);

        panel.channelRows.leftCombo.currentIndex = 1;
        panel.channelRows.leftCombo.activated(1);
        compare(SessionModel.param("in", "leftChannel"), "MONO");
    }

    function test_aDeviceThatGoesAwayShowsUnavailableAndClearsWhenItComesBack() {
        // The criterion, for both blocks: unavailable within 250 ms, and
        // cleared when the device returns.
        const panel = createTemporaryObject(outputPanelComponent, null, { blockId: "out" });
        SessionModel.setParam("out", "device", "alsa_output.hdmi");
        tryVerify(() => !panel.unavailable, testCase.noticeMs, "a device that is there read as unavailable");

        TestEngine.setFakeDevicePresent("alsa_output.hdmi", false);
        tryVerify(() => panel.unavailable, testCase.noticeMs, "an unplugged device did not read as unavailable");

        TestEngine.setFakeDevicePresent("alsa_output.hdmi", true);
        tryVerify(() => !panel.unavailable, testCase.noticeMs, "a device that came back still read as unavailable");
    }

    function test_theInputBlockNoticesItsMicrophoneGoingAway() {
        const panel = createTemporaryObject(inputPanelComponent, null, { blockId: "in" });
        SessionModel.setParam("in", "device", "alsa_input.usb-Microphone");
        tryVerify(() => !panel.unavailable, testCase.noticeMs, "a microphone that is there read as unavailable");

        TestEngine.setFakeDevicePresent("alsa_input.usb-Microphone", false);
        tryVerify(() => panel.unavailable, testCase.noticeMs, "an unplugged microphone read as available");

        TestEngine.setFakeDevicePresent("alsa_input.usb-Microphone", true);
        tryVerify(() => !panel.unavailable, testCase.noticeMs, "a microphone that came back read as unavailable");
    }

    // --- a wireless headset on the wrong profile ---

    readonly property string headset: "bluez_input.00:1B:66:A1:B2:C3"

    /// Runs the session with the input block pointed at \a node, and
    /// answers the panel, its wait shortened.
    function runningPanelOn(node) {
        SessionModel.setParam("in", "device", node);
        const panel = createTemporaryObject(inputPanelComponent, null, { blockId: "in", profileGraceMs: 100 });
        Meters.watch(testCase.sessionId);
        TestEngine.start(testCase.sessionId);
        testCase.running = true;
        tryVerify(() => Meters.live, 1000, "a running session published nothing");
        return panel;
    }

    property bool running: false

    function cleanup() {
        if (!testCase.running) {
            return;
        }
        TestEngine.clearFakeLevels(testCase.sessionId);
        TestEngine.stop(testCase.sessionId);
        Meters.watch("");
        testCase.running = false;
    }

    function test_aHeadsetOnItsPlaybackProfileSaysWhyItIsSilent() {
        // The criterion: the hint appears when the chosen input is a
        // wireless headset on its playback profile, the session runs and
        // the meters see nothing; and goes once the profile is switched.
        TestEngine.addFakeHeadset(true);
        const panel = testCase.runningPanelOn(testCase.headset);
        verify(!panel.unavailable, "a headset that is there read as unavailable");

        tryVerify(() => panel.profileMessage.visible, 1000, "a silent headset on its playback profile said nothing");
        verify(panel.profileMessage.text.startsWith("Wireless Headset is on its playback profile"),
               "the hint does not name the headset: " + panel.profileMessage.text);
        verify(panel.profileMessage.text.includes("headset profile"), "the hint does not say what to switch to");

        // WirePlumber, or a person in the sound settings, switches it.
        TestEngine.addFakeHeadset(false);
        tryVerify(() => !panel.profileMessage.visible, testCase.noticeMs,
                  "the hint stayed after the microphone came on");
    }

    function test_theHintWaitsForTheSwitchWirePlumberMakesItself() {
        // Linking a recording to the headset is what makes WirePlumber switch
        // it, so the first moments of silence are not worth a word.
        TestEngine.addFakeHeadset(true);
        const panel = testCase.runningPanelOn(testCase.headset);
        panel.profileGraceMs = 2000;
        verify(panel.silentHeadset, "the panel did not see the silence");
        wait(500);
        verify(!panel.profileMessage.visible, "the hint did not wait for the switch");
    }

    function test_theHintIsOnlyForASilentHeadsetWhileTheSessionRuns() {
        // Not before the session runs: nothing is being recorded to miss.
        TestEngine.addFakeHeadset(true);
        SessionModel.setParam("in", "device", testCase.headset);
        const stopped = createTemporaryObject(inputPanelComponent, null, { blockId: "in", profileGraceMs: 50 });
        wait(200);
        verify(!stopped.profileMessage.visible, "the hint was shown with nothing running");

        // Not for a headset whose meters see something.
        const panel = testCase.runningPanelOn(testCase.headset);
        TestEngine.setFakeLevel(testCase.sessionId, "in", 0.5);
        tryVerify(() => Meters.levels("in").active ?? false, 1000, "the block never metered the signal");
        wait(300);
        verify(!panel.profileMessage.visible, "the hint was shown for a headset that is being heard");
    }

    function test_theHintIsNeverShownForADeviceRecordingNormally() {
        // A silent USB microphone, and a silent headset on its headset
        // profile: quiet is not a wrong profile.
        TestEngine.addFakeHeadset(false);
        const microphone = testCase.runningPanelOn("alsa_input.usb-Microphone");
        wait(300);
        verify(!microphone.profileMessage.visible, "the hint was shown for a USB microphone");

        SessionModel.setParam("in", "device", testCase.headset);
        wait(300);
        verify(!microphone.profileMessage.visible, "the hint was shown for a headset on its headset profile");
    }

    function test_aBlockFollowingTheDefaultFollowsItSomewhereElse() {
        // **A default that is not there is not a default.** A block
        // following it has to follow it, which is the whole reason the
        // reference exists.
        const panel = createTemporaryObject(outputPanelComponent, null, { blockId: "out" });
        compare(panel.deviceState.node, "alsa_output.analog");

        TestEngine.setFakeDevicePresent("alsa_output.analog", false);
        tryVerify(() => panel.unavailable, testCase.noticeMs,
                  "the default went away and the block did not notice");
    }

    function test_aDeviceTheSessionNamesButNobodyHasPluggedInIsStillOffered() {
        // Opening the panel of a block whose interface is at home must not
        // quietly re-point it at something else.
        const panel = createTemporaryObject(outputPanelComponent, null, { blockId: "out" });
        SessionModel.setParam("out", "device", "alsa_output.usb-interface");
        tryVerify(() => panel.devicePicker.entries.length === 4, testCase.noticeMs,
                  "a device the session names was dropped from the picker");
        compare(panel.devicePicker.entries[3].value, "alsa_output.usb-interface");
        compare(panel.devicePicker.combo.currentValue, "alsa_output.usb-interface");
        verify(panel.unavailable, "a device nobody has plugged in read as available");
    }

    function test_theOutputFaceNamesWhatTheDefaultLandsOn() {
        // The criterion: a speaker icon and the device name, with the
        // resolved name under "Default output".
        const face = createTemporaryObject(faceComponent, null, { blockId: "out", output: true });

        compare(face.iconName, "audio-volume-high");
        compare(face.title, "Default output");
        compare(face.subtitle, "Headphones");

        SessionModel.setParam("out", "device", "alsa_output.hdmi");
        tryVerify(() => face.title === "HDMI", testCase.noticeMs, "the face did not follow the chosen device");
        // A named device says its name once and has nothing to add.
        compare(face.subtitle, "");

        TestEngine.setFakeDevicePresent("alsa_output.hdmi", false);
        tryVerify(() => face.subtitle === "Unavailable", testCase.noticeMs,
                  "the face did not say the device had gone");
    }

    function test_theInputFaceAsksForADeviceWhenThereIsNone() {
        const face = createTemporaryObject(faceComponent, null, { blockId: "in", output: false });

        compare(face.iconName, "audio-input-microphone");
        compare(face.title, "Default input");
        compare(face.subtitle, "USB Microphone");

        SessionModel.setParam("in", "device", "");
        tryVerify(() => face.title === "Choose an input", testCase.noticeMs,
                  "a block pointed nowhere did not ask to be pointed somewhere");
        // And nothing is missing: nobody has chosen anything to miss.
        compare(face.unavailable, false);
        SessionModel.setParam("in", "device", "default");
    }

    function test_bothDeviceBlocksHaveAFaceABareLoaderCanDraw() {
        // The criterion: the register's components go into a Loader the way
        // the canvas puts them there — nothing handed over at creation —
        // and the tile ends up naming the device.
        const output = createTemporaryObject(tileComponent, null,
                                             { typeId: "output.device", blockId: "out" });
        tryVerify(() => output.face !== null && output.face.title === "Default output", testCase.noticeMs,
                  "a Loader could not draw the Output Device face");
        // Which of the two blocks it is comes from the register, not from
        // the canvas: nothing set `output` here.
        compare(output.face.output, true);
        compare(output.face.subtitle, "Headphones");

        const input = createTemporaryObject(tileComponent, null,
                                            { typeId: "source.device", blockId: "in" });
        tryVerify(() => input.face !== null && input.face.title === "Default input", testCase.noticeMs,
                  "a Loader could not draw the Input Device face");
        compare(input.face.output, false);
        compare(input.face.subtitle, "USB Microphone");

        // A block with no entry falls back to what its face kind says,
        // which is the canvas's decision rather than the register's. Named
        // here by one this build does not have yet: the Echo, the Reverb and
        // the Pitch Shift all have faces of their own now.
        compare(BlockFaces.faceFor("advanced.transcribe"), null);
    }

    function test_bothDeviceBlocksHaveAPanelOfTheirOwn() {
        verify(BlockPanels.panelFor("output.device") !== null, "the Output Device block has no panel");
        verify(BlockPanels.panelFor("source.device") !== null, "the Input Device block has no panel");
        // And a block with no entry of its own gets the generic one, which
        // is not a lesser panel. Named by one this build does not have yet.
        compare(BlockPanels.panelFor("advanced.transcribe"), null);
    }

    function test_theSettingsPanelLoadsTheBlocksOwnPanelAndTellsItWhichBlock() {
        const panel = createTemporaryObject(settingsPanelComponent, null);
        panel.open("out");
        tryVerify(() => panel.body.item !== null && panel.body.item.blockId === "out", 1000,
                  "the block's own panel was not told which block it is");
        compare(panel.body.item.devicePicker.combo.currentValue, "default");
    }

    // What the canvas does with a face: a Loader given the register's
    // component, and the block id bound in afterwards, because a Loader
    // cannot give sourceComponent its initial property values.
    Component {
        id: tileComponent

        Item {
            id: tile

            property string typeId: ""
            property string blockId: ""

            readonly property alias face: faceLoader.item

            Loader {
                id: faceLoader

                sourceComponent: BlockFaces.faceFor(tile.typeId)
            }

            Binding {
                target: faceLoader.item
                property: "blockId"
                value: tile.blockId
                when: faceLoader.item !== null
            }
        }
    }

    Component {
        id: settingsPanelComponent

        SettingsPanel {
            width: 320
            height: 480
        }
    }
}
