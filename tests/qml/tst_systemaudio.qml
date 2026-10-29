// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The System Audio block's face and panel offscreen, against the
// mock engine: that the tile counts the exclusions, that the alert-sounds
// checkbox writes the block's own parameter, and that the picker writes
// match rules — including for a program that is not playing, which is
// exactly the one somebody wants to leave out before they start it.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "SystemAudio"
    when: windowShown

    property string sessionId: ""

    /// How long the interface has to notice something the engine said.
    readonly property int noticeMs: 250

    // **The panel is given the test case as a parent**, unlike the faces:
    // the application picker is a popup, and a popup has nowhere to open
    // when the item it hangs off is in no window.
    Component {
        id: panelComponent

        SystemAudioPanel {
            width: 320
        }
    }

    Component {
        id: faceComponent

        SystemAudioFace {}
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
        TestEngine.createSession("System audio");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock",
                                              block: { id: "sys", type: "source.system" },
                                              index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        // Every test starts from the same made-up machine, whatever the
        // last one left out.
        TestEngine.addFakeInventory();
        SessionModel.setParam("sys", "excluded", []);
        SessionModel.setParam("sys", "includeAlerts", false);
        tryVerify(() => Inventory.ready, 1000, "the engine answered with an inventory");
    }


    /// Every piece of text in \a item, deep. **Shown or not**: a panel
    /// with no window is never visible.
    function textsOf(item) {
        let found = [];
        for (const child of item.children) {
            if (typeof child.text === "string" && child.text.length > 0) {
                found.push(child.text);
            }
            found = found.concat(testCase.textsOf(child));
        }
        return found;
    }

    function test_thePanelIsLaidOutAsTheDesignDrawsIt() {
        // What the block does, the alert sounds in bold with their
        // reason under them, and the exclusions folded under "Advanced"
        // with a summary, a button as wide as its words and what it is for.
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "sys";
        wait(50);

        const words = testCase.textsOf(panel);
        verify(words.indexOf("System Audio captures everything the system plays through PipeWire.") >= 0,
               "the panel does not say what the block does");
        verify(panel.alertsCheck.font.bold, "the alert sounds setting is not in bold");
        verify(words.indexOf("Notification sounds are normally excluded. Turn this on to capture them as well.") >= 0,
               "the reason alert sounds are off is not there");

        compare(panel.advancedSection.title, "Advanced");
        compare(panel.advancedSection.expanded, false, "Advanced starts open");
        panel.advancedButton.toggle();
        compare(panel.advancedSection.expanded, true, "toggling the heading did not open Advanced");
        verify(words.indexOf("Excluded sources:") >= 0, "there is no summary of what is left out");
        compare(panel.excludedSummary.text, "None");
        verify(panel.excludeButton.width < panel.width / 2, "the exclude button is as wide as the panel");
        verify(words.indexOf("By default all system audio is captured. Add applications here to leave them out.") >= 0,
               "what the list is for is not said");
    }

    function test_theSubtitleCountsTheExclusions() {
        // The criterion: "All system audio" when nothing is left out, and a
        // count once something is.
        const face = createTemporaryObject(faceComponent, null, { blockId: "sys" });

        compare(face.subtitle, "All system audio");
        compare(face.lineLabel.text, "All system audio");

        SessionModel.setParam("sys", "excluded", ["binary:firefox"]);
        tryVerify(() => face.subtitle === "All except 1 app", testCase.noticeMs,
                  "the tile did not count one exclusion");

        SessionModel.setParam("sys", "excluded", ["binary:firefox", "binary:musicplayer"]);
        tryVerify(() => face.subtitle === "All except 2 apps", testCase.noticeMs,
                  "the tile did not count two exclusions");
    }

    function test_theTileIsQuietUntilSomethingIsBeingCaptured() {
        // Nothing is running, so the block has published nothing about
        // itself and the dot is the idle one. **A stopped session and a
        // silent one are the same dot** — in both, nothing is captured.
        const face = createTemporaryObject(faceComponent, null, { blockId: "sys" });

        compare(face.streamCount, 0);
        compare(face.capturing, false);
    }

    function test_theBlockHasAFaceAndAPanelOfItsOwn() {
        const tile = createTemporaryObject(tileComponent, null,
                                           { typeId: "source.system", blockId: "sys" });
        tryVerify(() => tile.face !== null && tile.face.subtitle === "All system audio", testCase.noticeMs,
                  "a Loader could not draw the System Audio face");
        verify(BlockPanels.panelFor("source.system") !== null, "the System Audio block has no panel");
    }

    function test_theAlertsCheckboxWritesTheBlocksOwnParameter() {
        // The criterion: turning alert sounds on and off is a parameter
        // change like any other, which is what takes effect without the
        // session being restarted.
        const panel = createTemporaryObject(panelComponent, testCase, { blockId: "sys" });

        compare(panel.alertsCheck.checked, false);

        panel.alertsCheck.toggle();
        panel.alertsCheck.toggled();
        compare(SessionModel.param("sys", "includeAlerts"), true);

        panel.alertsCheck.toggle();
        panel.alertsCheck.toggled();
        compare(SessionModel.param("sys", "includeAlerts"), false);
    }

    function test_thePanelFollowsAChangeItDidNotMake() {
        // **A control that has been clicked once still follows the
        // session.** Clicking assigns the property from C++, which would
        // drop a binding drawing it from the session; without the panel
        // putting the session's answer back, the checkbox would stop
        // following an undo, a second window, or a session opened while
        // the panel is showing.
        const panel = createTemporaryObject(panelComponent, testCase, { blockId: "sys" });

        panel.alertsCheck.toggle();
        panel.alertsCheck.toggled();
        compare(SessionModel.param("sys", "includeAlerts"), true);

        SessionModel.setParam("sys", "includeAlerts", false);
        tryVerify(() => !panel.alertsCheck.checked, testCase.noticeMs,
                  "the alerts checkbox stopped following the session after it was clicked");
    }

    function test_thePickerOffersWhatIsPlayingAndWritesItsMatchRule() {
        // The criterion: the picker writes match rules, never ids.
        const panel = createTemporaryObject(panelComponent, testCase, { blockId: "sys" });
        panel.advancedButton.toggle();

        panel.excludeButton.clicked();
        tryVerify(() => panel.picker.opened, testCase.noticeMs, "the picker did not open");
        compare(panel.picker.entries.map(entry => entry.key), ["binary:firefox", "binary:musicplayer"]);
        compare(panel.picker.entries[0].name, "Firefox");

        panel.picker.running.itemAt(0).clicked();
        compare(SessionModel.param("sys", "excluded"), ["binary:firefox"]);
        // And the row that appeared calls it what it calls itself.
        tryVerify(() => panel.excludedRows.count === 1, testCase.noticeMs, "the exclusion was not listed");

        // What is already left out is not offered a second time.
        panel.excludeButton.clicked();
        tryVerify(() => panel.picker.opened, testCase.noticeMs, "the picker did not open again");
        compare(panel.picker.entries.map(entry => entry.key), ["binary:musicplayer"]);
        panel.picker.close();
    }

    function test_anApplicationThatIsNotPlayingCanStillBeLeftOut() {
        // The criterion, and the reason a rule is stored rather than a
        // stream: the program somebody wants to leave out is usually the
        // one they have not started yet, so it cannot be in a list of what
        // is playing and has to be nameable.
        const panel = createTemporaryObject(panelComponent, testCase, { blockId: "sys" });
        panel.advancedButton.toggle();

        panel.excludeButton.clicked();
        tryVerify(() => panel.picker.opened, testCase.noticeMs, "the picker did not open");
        panel.picker.field.text = "obs";
        tryVerify(() => panel.picker.offersTyped, testCase.noticeMs,
                  "a program that is not playing could not be named");
        compare(panel.picker.typedRule, "binary:obs");

        panel.picker.typedEntry.clicked();
        compare(SessionModel.param("sys", "excluded"), ["binary:obs"]);
        // Nothing of it is playing, so the rule's own subject is what the
        // row says.
        tryVerify(() => panel.excludedRows.count === 1, testCase.noticeMs, "the exclusion was not listed");
        compare(Inventory.applicationName("binary:obs"), "obs");
    }

    function test_anApplicationThatHasBeenClosedCanStillBeLeftOut() {
        // **The same store, in the other picker**. Excluding a
        // program is most often done because it was a nuisance, and by
        // then it has usually been closed.
        const panel = createTemporaryObject(panelComponent, testCase, { blockId: "sys" });
        RecentApplications.remember("binary:noisy", "Noisy Thing");
        panel.advancedButton.toggle();

        panel.excludeButton.clicked();
        tryVerify(() => panel.picker.opened, testCase.noticeMs, "the picker did not open");
        verify(!Inventory.hasApplication("binary:noisy"), "the made-up machine should not be playing it");
        const row = panel.picker.recentEntries.findIndex(entry => entry.key === "binary:noisy");
        verify(row >= 0, "a program that has been closed was not offered");

        panel.picker.recent.itemAt(row).clicked();
        compare(SessionModel.param("sys", "excluded"), ["binary:noisy"]);
    }

    function test_anExclusionCanBeTakenBackIn() {
        const panel = createTemporaryObject(panelComponent, testCase, { blockId: "sys" });
        SessionModel.setParam("sys", "excluded", ["binary:firefox", "binary:musicplayer"]);
        panel.advancedButton.toggle();
        tryVerify(() => panel.excludedRows.count === 2, testCase.noticeMs, "both exclusions were not listed");

        panel.include("binary:firefox");
        compare(SessionModel.param("sys", "excluded"), ["binary:musicplayer"]);
        tryVerify(() => panel.excludedRows.count === 1, testCase.noticeMs,
                  "an application taken back in was still listed");
    }
}
