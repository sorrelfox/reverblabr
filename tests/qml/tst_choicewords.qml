// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// **No choice shows its id.** Every choice of every built-in block in the
// catalogue has words in ChoiceWords, and the list a ParamRow puts up,
// in the generic panel that every block can show, reads them: "Adaptive"
// and "dB HL", not "adaptive" and "dbHl".

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "ChoiceWords"
    when: windowShown

    property string sessionId: ""

    Component {
        id: genericComponent

        GenericParamPanel {
            width: 400
        }
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Choices");
        verify(testCase.sessionId !== "", "the mock opened a session");
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    /// The built-in blocks: not the tests' own (test.*), whose words nobody
    /// reads.
    function builtIn(): var {
        return BlockCatalogue.types.filter(typeId => !typeId.startsWith("test."));
    }

    function test_everyChoiceInTheCatalogueHasWords() {
        const missing = [];
        let choices = 0;
        for (const typeId of testCase.builtIn()) {
            for (const param of BlockCatalogue.params(typeId)) {
                for (const choice of param.choices ?? []) {
                    ++choices;
                    if (!ChoiceWords.hasWords(typeId, param.id, choice)) {
                        missing.push(typeId + " " + param.id + " " + choice);
                    }
                }
            }
        }
        verify(choices > 100, "the catalogue has hardly any choices: " + choices);
        compare(missing.join(", "), "", "these choices would show their ids");
    }

    function test_theHearingBlocksChoicesInWords() {
        compare(ChoiceWords.labels("hearing.compensation16",
                                   BlockCatalogue.describeParam("hearing.compensation16", "speed")),
                ["Syllabic", "Slow", "Adaptive"]);
        compare(ChoiceWords.labels("hearing.compensation64",
                                   BlockCatalogue.describeParam("hearing.compensation64", "audiogramUnit")),
                ["dB HL", "dBFS"]);
        compare(ChoiceWords.labels("hearing.frequencylowering",
                                   BlockCatalogue.describeParam("hearing.frequencylowering", "ear")),
                ["Left", "Right"]);
        // The same id is a different word where the block moves the other
        // way.
        compare(ChoiceWords.label("hearing.frequencylowering", "move2Method", "transpose"), "Copy down");
        compare(ChoiceWords.label("hearing.frequencyraising", "move2Method", "transpose"), "Copy up");
    }

    /// Every list in \a item, with the ids of its row's parameter.
    function listsOf(item: Item, found: var): var {
        for (let index = 0; index < item.children.length; ++index) {
            const child = item.children[index];
            if (child.descriptor !== undefined && child.activeControl instanceof T.ComboBox) {
                found.push({ paramId: child.paramId, choices: child.descriptor.choices ?? [],
                             shown: child.activeControl.model });
                continue;
            }
            testCase.listsOf(child, found);
        }
        return found;
    }

    function test_theGenericPanelShowsEveryBlocksChoicesInWords() {
        const shownIds = [];
        let lists = 0;
        for (const typeId of testCase.builtIn()) {
            if (!BlockCatalogue.params(typeId).some(param => (param.choices ?? []).length > 0)) {
                continue;
            }
            const id = "choices-" + typeId.replace(/\./g, "-");
            TestEngine.applyEdit(testCase.sessionId,
                                 JSON.stringify({ op: "addBlock", block: { id: id, type: typeId }, index: -1 }));
            tryVerify(() => SessionModel.blockType(id) === typeId, 1000, "the mock did not add a " + typeId);
            const panel = createTemporaryObject(genericComponent, null, { blockId: id });
            verify(panel !== null);
            for (const list of testCase.listsOf(panel, [])) {
                ++lists;
                for (let index = 0; index < list.choices.length; ++index) {
                    const choice = list.choices[index];
                    if (!ChoiceWords.hasWords(typeId, list.paramId, choice)
                            || list.shown[index] !== ChoiceWords.label(typeId, list.paramId, choice)) {
                        shownIds.push(typeId + " " + list.paramId + " " + choice);
                    }
                }
            }
        }
        verify(lists > 20, "found hardly any lists: " + lists);
        compare(shownIds.join(", "), "", "these lists show ids");
    }
}
