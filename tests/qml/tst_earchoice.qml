// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Ear on every hearing panel for one ear: there, above everything else
// a person sets, Left and Right in words, following the block's setting,
// and a click on the other side one edit that one undo takes back. The
// tile's L or R and the channel the panel asks the meters for follow it.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "EarChoice"
    when: windowShown

    property string sessionId: ""
    property int made: 0

    Component {
        id: shownPanel

        Window {
            property alias loader: inner

            width: 520
            height: 900
            visible: true

            Loader {
                id: inner

                width: 480
            }
        }
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Ear");
        verify(testCase.sessionId !== "", "the mock opened a session");
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function cleanup() {
        Meters.watch("");
    }

    function test_everyHearingPanelForOneEarOffersTheEar_data() {
        return ["hearing.compensation16", "hearing.compensation64", "hearing.frequencylowering",
                "hearing.frequencyraising"].map(typeId => ({ tag: typeId, typeId: typeId }));
    }

    function test_everyHearingPanelForOneEarOffersTheEar(data) {
        testCase.made += 1;
        const id = "ear" + testCase.made;
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: id, type: data.typeId }, index: -1 }));
        tryVerify(() => SessionModel.blockType(id) === data.typeId, 1000, "the block was not added");

        Meters.watch(testCase.sessionId);
        const window = createTemporaryObject(shownPanel, null);
        window.loader.sourceComponent = BlockPanels.panelFor(data.typeId);
        const panel = window.loader.item;
        verify(panel !== null, "no panel for " + data.typeId);
        panel.blockId = id;
        const ear = panel.earChoice;
        verify(ear.visible, "the panel shows no Ear");
        compare(ear.Accessible.name, "Ear");
        compare(ear.choices.count, 2);
        const left = ear.choices.itemAt(0);
        const right = ear.choices.itemAt(1);
        compare(left.text, "Left");
        compare(right.text, "Right");
        tryVerify(() => left.checked && !right.checked, 1000, "a new block is not shown on the left ear");

        // **Found first**: above the view and every row of settings.
        verify(ear.y < panel.plotArea.mapToItem(panel, 0, 0).y, "the Ear is not above the view");

        tryVerify(() => right.width > 0, 1000);
        // A move panel picks its first move once it is shown, and lays out
        // again under it.
        tryVerify(() => panel.plotArea.width > 0 && (panel.moves === undefined || panel.selected >= 0), 2000,
                  "the panel was never laid out");
        waitForRendering(window.contentItem);
        mouseClick(right);
        tryVerify(() => SessionModel.param(id, "ear") === "right", 1000, "choosing Right set nothing");
        tryVerify(() => right.checked && !left.checked, 1000, "the choice does not show the setting");
        // The panel's own ear follows: what it asks the meters for.
        tryVerify(() => TestEngine.analysedChannel(testCase.sessionId) === 1, 1000,
                  "the panel still watches the left ear");

        // Choosing the side that is chosen is no edit.
        mouseClick(right);
        SessionModel.undo();
        tryVerify(() => (SessionModel.param(id, "ear") ?? "left") === "left", 1000,
                  "one undo did not put the block back on the left ear");
        tryVerify(() => left.checked && !right.checked, 1000);

        // Set from anywhere else, the choice follows.
        SessionModel.setParam(id, "ear", "right");
        tryVerify(() => right.checked, 1000, "the choice did not follow the setting");
    }
}
