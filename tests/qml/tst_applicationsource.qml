// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Application block's face and panel offscreen, against the mock
// engine: that choosing a program writes the three fields the session
// stores it by and nothing else, that the tile then says which program it
// is and what is happening to it, and that a program which is not playing
// can still be chosen — which is the whole reason a match rule is stored
// rather than a stream.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "ApplicationSource"
    when: windowShown

    property string sessionId: ""

    /// How long the interface has to notice something the engine said.
    readonly property int noticeMs: 250

    // **The panel is given the test case as a parent**, unlike the faces:
    // the application picker is a popup, and a popup has nowhere to open
    // when the item it hangs off is in no window.
    Component {
        id: panelComponent

        ApplicationSourcePanel {
            width: 320
        }
    }

    Component {
        id: faceComponent

        ApplicationSourceFace {}
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

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Application source");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock",
                                              block: { id: "app", type: "source.application" },
                                              index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        // Every test starts from the same made-up machine, pointed at
        // nothing, whatever the last one chose.
        TestEngine.addFakeInventory();
        SessionModel.setParam("app", "binary", "");
        SessionModel.setParam("app", "flatpakId", "");
        SessionModel.setParam("app", "name", "");
        SessionModel.setParam("app", "outputMode", "default");
        SessionModel.setParam("app", "outputs", []);
        SessionModel.setParam("app", "includeInput", false);
        SessionModel.setParam("app", "splitChannels", false);
        SessionModel.setParam("app", "openUrlOnStart", false);
        SessionModel.setParam("app", "url", "");
        tryVerify(() => Inventory.ready, 1000, "the engine answered with an inventory");
    }

    function test_theBlockHasAFaceAndAPanelOfItsOwn() {
        const tile = createTemporaryObject(tileComponent, null,
                                           { typeId: "source.application", blockId: "app" });
        tryVerify(() => tile.face !== null && tile.face.subtitle === "Choose an app", testCase.noticeMs,
                  "a Loader could not draw the Application face");
        verify(BlockPanels.panelFor("source.application") !== null, "the Application block has no panel");
    }

    function test_aBlockPointedAtNothingSaysSo() {
        // The criterion: "Choose an app" when unset. The tile is the only
        // place somebody sees that they have not finished setting it up.
        const face = createTemporaryObject(faceComponent, null, { blockId: "app" });

        compare(face.pointedAtSomething, false);
        compare(face.subtitle, "Choose an app");
        compare(face.lineLabel.text, "Choose an app");
        // The block's own name, until there is a program to name instead.
        compare(face.title, "Application");
        compare(face.capturing, false);
    }

    function test_choosingAnApplicationWritesItsMatchRuleAndTheFaceUpdates() {
        // The criterion: choosing an app from the picker creates the right
        // match rule, and the face updates.
        const panel = createTemporaryObject(panelComponent, testCase, { blockId: "app" });
        const face = createTemporaryObject(faceComponent, null, { blockId: "app" });

        panel.sourceButton.popup.open();
        tryVerify(() => panel.picker.opened, testCase.noticeMs, "the picker did not open");
        compare(panel.picker.entries.map(entry => entry.key), ["binary:firefox", "binary:musicplayer"]);
        // What the row says under the name: how many sounds it has going.
        compare(panel.picker.detail(panel.picker.entries[0]), "1 stream");
        // And a sandboxed one is told apart by the id the portal gave it.
        compare(panel.picker.detail({ streamCount: 2, flatpakId: "org.example.Player" }),
                "org.example.Player · 2 streams");

        panel.picker.running.itemAt(0).clicked();

        // The rule, in the three fields the session stores it as.
        compare(SessionModel.param("app", "binary"), "firefox");
        compare(SessionModel.param("app", "flatpakId"), "");
        compare(SessionModel.param("app", "name"), "");
        compare(panel.rule, "binary:firefox");

        tryVerify(() => face.title === "Firefox", testCase.noticeMs, "the tile did not name the application");
        // Nothing is running, so nothing is being captured — which is what
        // the tile says rather than pretending otherwise.
        compare(face.subtitle, "Waiting…");
        compare(face.capturing, false);
        // The program's own icon, which is what somebody recognises without
        // reading.
        compare(face.icon.applicationIconName, "firefox");
    }

    function test_pointingItSomewhereElseClearsTheFieldsItIsNoLongerUsing() {
        // **The field nobody wrote would go on being matched.** The engine
        // keeps the first of the three that is set, so a block moved from a
        // Flatpak to a packaged program would still be looking for the
        // Flatpak.
        const panel = createTemporaryObject(panelComponent, testCase, { blockId: "app" });

        panel.choose("flatpak:org.example.Player");
        compare(SessionModel.param("app", "flatpakId"), "org.example.Player");
        compare(SessionModel.param("app", "binary"), "");
        compare(panel.rule, "flatpak:org.example.Player");

        panel.choose("binary:firefox");
        compare(SessionModel.param("app", "binary"), "firefox");
        compare(SessionModel.param("app", "flatpakId"), "");
        compare(panel.rule, "binary:firefox");
    }

    function test_anApplicationThatIsNotPlayingCanStillBeChosen() {
        // The criterion a match rule exists for: the program somebody wants
        // to capture is often one they have not started yet, so it cannot
        // be in a list of what is playing and has to be nameable.
        const panel = createTemporaryObject(panelComponent, testCase, { blockId: "app" });

        panel.sourceButton.popup.open();
        tryVerify(() => panel.picker.opened, testCase.noticeMs, "the picker did not open");
        panel.picker.field.text = "obs";
        tryVerify(() => panel.picker.offersTyped, testCase.noticeMs,
                  "a program that is not playing could not be named");
        compare(panel.picker.typedRule, "binary:obs");

        panel.picker.typedEntry.clicked();
        compare(SessionModel.param("app", "binary"), "obs");
        verify(!Inventory.hasApplication("binary:obs"), "the made-up machine should not be playing it");
        compare(panel.sourceName, "obs");
    }

    function test_aStreamIsNotAnApplication() {
        // A `node:` rule names one stream on one boot. The exclusion picker
        // may hand one back, because an exclusion is matched while the
        // session runs; this block stores what it is pointed at in a file
        // that outlives the boot, so it may not.
        const panel = createTemporaryObject(panelComponent, testCase, { blockId: "app" });

        panel.sourceButton.popup.open();
        tryVerify(() => panel.picker.opened, testCase.noticeMs, "the picker did not open");
        panel.picker.field.text = "node:alsa_playback.someone";
        // The rule is well formed, and still not one this picker offers.
        compare(Inventory.applicationRule("node:alsa_playback.someone"), "node:alsa_playback.someone");
        compare(panel.picker.typedRule, "");
        compare(panel.picker.offersTyped, false);
        panel.picker.close();
    }

    function test_theCaptureModeAndTheOutputsItNamesAreWritten() {
        // The criterion: the three output-device modes, and a checklist of
        // sinks for the third.
        const panel = createTemporaryObject(panelComponent, testCase, { blockId: "app" });

        compare(panel.outputMode, "default");
        verify(panel.modeButtons.itemAt(0).checked, "the default output is not the choice to begin with");

        panel.modeButtons.itemAt(2).toggle();
        panel.modeButtons.itemAt(2).toggled();
        compare(SessionModel.param("app", "outputMode"), "chosen");

        // The machine's sinks, each one a box to tick.
        tryVerify(() => panel.outputRows.count === 2, testCase.noticeMs, "the sinks were not listed");
        panel.outputRows.itemAt(0).toggle();
        panel.outputRows.itemAt(0).toggled();
        compare(SessionModel.param("app", "outputs"), [panel.outputRows.itemAt(0).modelData.node]);

        panel.outputRows.itemAt(0).toggle();
        panel.outputRows.itemAt(0).toggled();
        compare(SessionModel.param("app", "outputs"), []);
    }

    function test_thePanelFollowsAChangeItDidNotMake() {
        // **A control that has been clicked once still follows the
        // session.** Clicking assigns the property from C++, which drops
        // the binding that was drawing it; a panel that did not put the
        // binding back would stop following an undo, or a session opened
        // while it is showing.
        const panel = createTemporaryObject(panelComponent, testCase, { blockId: "app" });
        panel.advancedButton.toggle();

        panel.microphoneCheck.toggle();
        panel.microphoneCheck.toggled();
        compare(SessionModel.param("app", "includeInput"), true);

        SessionModel.setParam("app", "includeInput", false);
        tryVerify(() => !panel.microphoneCheck.checked, testCase.noticeMs,
                  "the checkbox stopped following the session after it was clicked");

        panel.modeButtons.itemAt(1).toggle();
        panel.modeButtons.itemAt(1).toggled();
        compare(SessionModel.param("app", "outputMode"), "any");
        SessionModel.setParam("app", "outputMode", "default");
        tryVerify(() => panel.modeButtons.itemAt(0).checked, testCase.noticeMs,
                  "the capture mode stopped following the session after it was chosen");
    }

    function test_anApplicationThatHasBeenClosedCanStillBeChosen() {
        // What remembering recently seen applications is for: the program
        // somebody came to name is usually one they have already closed,
        // and it can only be offered by name because something wrote the
        // name down while it was playing.
        const panel = createTemporaryObject(panelComponent, testCase, { blockId: "app" });
        RecentApplications.remember("binary:conferencing", "Conferencing");

        panel.sourceButton.popup.open();
        tryVerify(() => panel.picker.opened, testCase.noticeMs, "the picker did not open");
        verify(!Inventory.hasApplication("binary:conferencing"), "the made-up machine should not be playing it");
        verify(panel.picker.recentEntries.some(entry => entry.key === "binary:conferencing"),
               "a program that has been closed was not offered");
        // And the ones that *are* playing are not offered twice: they are
        // in the list above, where they belong.
        verify(!panel.picker.recentEntries.some(entry => entry.key === "binary:firefox"),
               "a program that is playing was offered a second time");

        const row = panel.picker.recentEntries.findIndex(entry => entry.key === "binary:conferencing");
        panel.picker.recent.itemAt(row).clicked();
        compare(SessionModel.param("app", "binary"), "conferencing");
        compare(panel.sourceName, "Conferencing");
    }

    function test_thePanelIsLaidOutAsTheDesignDrawsIt() {
        // The source and the web address first, then the three ways
        // to choose what is captured in sight under their own heading, then
        // the rest folded under "Advanced", each in bold with what it does.
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "app";
        wait(50);

        compare(panel.urlCheck.text, "Open URL");
        verify(panel.urlCheck.font.bold, "Open URL is not in bold");
        compare(panel.urlField.placeholderText, "Opened when the session starts");
        verify(panel.urlCheck.mapToItem(panel, 0, 0).y < panel.modeSection.mapToItem(panel, 0, 0).y,
               "the web address is not above the capture choices");

        compare(panel.modeSection.title, "Audio device selection");
        compare(panel.modeSection.expanded, true, "the capture choices start folded away");
        compare(panel.modeButtons.count, 3);
        const labels = [];
        for (let index = 0; index < panel.modeButtons.count; ++index) {
            labels.push(panel.modeButtons.itemAt(index).text);
        }
        compare(labels, ["Capture audio played to the default output device",
                         "Capture audio played to all output devices",
                         "Capture audio played to the specified devices"]);

        compare(panel.advancedSection.title, "Advanced");
        compare(panel.advancedSection.expanded, false, "Advanced starts open");
        compare(panel.microphoneCheck.text, "Include audio input");
        compare(panel.splitCheck.text, "Split between channels");

        // The one the old panel never offered, written as the block's own.
        compare(panel.gapsCheck.text, "Fill playback gaps with silence");
        compare(panel.gapsCheck.checked, true, "filling the gaps is not on to begin with, as the block has it");
        panel.gapsCheck.toggle();
        panel.gapsCheck.toggled();
        compare(SessionModel.param("app", "fillGapsWithSilence"), false);
        panel.gapsCheck.toggle();
        panel.gapsCheck.toggled();
        compare(SessionModel.param("app", "fillGapsWithSilence"), true);
    }

    function test_theAdvancedSettingsWriteTheBlocksOwnParameters() {
        const panel = createTemporaryObject(panelComponent, testCase, { blockId: "app" });
        panel.advancedButton.toggle();

        // The split only means anything with the microphone in: there is
        // one thing to put on either side, or there are two.
        compare(panel.splitCheck.enabled, false);
        panel.microphoneCheck.toggle();
        panel.microphoneCheck.toggled();
        tryVerify(() => panel.splitCheck.enabled, testCase.noticeMs,
                  "the split stayed switched off with a microphone to split");

        panel.splitCheck.toggle();
        panel.splitCheck.toggled();
        compare(SessionModel.param("app", "splitChannels"), true);

        panel.urlCheck.toggle();
        panel.urlCheck.toggled();
        compare(SessionModel.param("app", "openUrlOnStart"), true);
        panel.urlField.text = "https://example.org/radio";
        panel.urlField.editingFinished();
        compare(SessionModel.param("app", "url"), "https://example.org/radio");
    }
}
