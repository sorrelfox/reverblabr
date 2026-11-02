// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The notice before the first Hearing block: said once, by the library and by
// a drop, and only for that group; Cancel adds nothing; every word can be
// reached and read from the keyboard.

import QtQuick
import QtTest
import org.kde.kirigami as Kirigami
import io.github.reverblabr

TestCase {
    id: testCase

    name: "HearingNotice"
    when: windowShown

    readonly property string hearingType: "hearing.compensation16"
    property string sessionId: ""
    property var window: null
    property var notice: null

    Component {
        id: shownCanvas

        Window {
            property alias canvas: inner

            width: 800
            height: 280
            visible: true

            SessionCanvas {
                id: inner

                anchors.fill: parent
            }
        }
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Notice");
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        FirstUseNotice.hearingAccepted = false;
        for (const id of Array.from(SessionModel.blockIds)) {
            SessionModel.removeBlock(id);
        }
        testCase.window = createTemporaryObject(shownCanvas, testCase);
        waitForRendering(testCase.window.contentItem);
        testCase.notice = testCase.window.canvas.hearingNotice;
    }

    function cleanup() {
        FirstUseNotice.hearingAccepted = false;
    }

    function blockCount(): int {
        return SessionModel.blockIds.length;
    }

    function waitOpen() {
        tryVerify(() => testCase.notice.opened, 2000, "the notice opened");
    }

    function waitShut() {
        tryVerify(() => !testCase.notice.visible, 2000, "the notice closed");
    }

    function pressAdd() {
        testCase.notice.customFooterActions[0].trigger();
        testCase.waitShut();
    }

    function words(): string {
        return [0, 1, 2, 3].map(i => testCase.notice.plainWords(i)).join(" ");
    }

    function test_theWordsAreAllThere() {
        compare(testCase.words().split(/\s+/).length, 286);
        verify(testCase.words().startsWith("Keep it comfortable."));
        verify(testCase.words().endsWith("where you need to hear traffic."));
        for (const paragraph of testCase.notice.paragraphs) {
            verify(paragraph.startsWith("<b>"), "each paragraph leads in bold");
        }
    }

    function test_itKeepsAReadableWidthInAWideWindow() {
        testCase.window.width = 1600;
        testCase.window.height = 900;
        waitForRendering(testCase.window.contentItem);
        testCase.notice.read();
        testCase.waitOpen();
        verify(testCase.notice.width <= testCase.notice.readableWidth + 1, "wider than its text should run: " + testCase.notice.width);
        verify(testCase.notice.width >= testCase.notice.readableWidth - 1, "it uses the room it has: " + testCase.notice.width);
        testCase.notice.close();
        testCase.waitShut();
    }

    function test_aNarrowWindowNarrowsIt() {
        testCase.window.width = 420;
        testCase.window.height = 600;
        waitForRendering(testCase.window.contentItem);
        testCase.notice.read();
        testCase.waitOpen();
        verify(testCase.notice.width < 420, "it stays inside the window: " + testCase.notice.width);
        testCase.notice.close();
        testCase.waitShut();
    }

    function test_theLibraryAsksOnceAndThenAdds() {
        const canvas = testCase.window.canvas;
        canvas.chooseBlock(testCase.hearingType);
        testCase.waitOpen();
        compare(testCase.blockCount(), 0, "nothing is added before the answer");
        testCase.pressAdd();
        compare(testCase.blockCount(), 1);
        verify(FirstUseNotice.hearingAccepted);

        canvas.chooseBlock(testCase.hearingType);
        verify(!testCase.notice.visible, "a second add does not ask");
        compare(testCase.blockCount(), 2);
    }

    function test_aDropAsksAndLandsWhereItWasDropped() {
        const canvas = testCase.window.canvas;
        canvas.dropBlock(testCase.hearingType, 300, 200);
        testCase.waitOpen();
        compare(testCase.blockCount(), 0);
        testCase.pressAdd();
        compare(testCase.blockCount(), 1);
        const place = canvas.placeOf(SessionModel.blockIds[0]);
        const size = canvas.tileSize();
        const wanted = canvas.toSession(300, 200);
        verify(Math.abs(place.x + size.width / 2 - wanted.x) <= size.width, "at the drop");
    }

    function test_cancelAddsNothingAndAsksAgain() {
        const canvas = testCase.window.canvas;
        canvas.chooseBlock(testCase.hearingType);
        testCase.waitOpen();
        testCase.notice.standardButton(Kirigami.Dialog.Cancel).clicked();
        testCase.waitShut();
        compare(testCase.blockCount(), 0);
        verify(!FirstUseNotice.hearingAccepted);
        canvas.dropBlock(testCase.hearingType, 300, 200);
        testCase.waitOpen();
        keyClick(Qt.Key_Escape);
        testCase.waitShut();
        compare(testCase.blockCount(), 0, "Escape adds nothing either");
        verify(!FirstUseNotice.hearingAccepted);
    }

    function test_otherBlocksNeverAsk_data() {
        return BlockCatalogue.types.filter(t => BlockPresentation.category(t) !== "hearing")
            .map(t => ({ tag: t, typeId: t }));
    }

    function test_otherBlocksNeverAsk(data) {
        verify(!testCase.window.canvas.noticeComesFirst(data.typeId));
    }

    function test_everyHearingBlockAsks_data() {
        return BlockCatalogue.types.filter(t => BlockPresentation.category(t) === "hearing")
            .map(t => ({ tag: t, typeId: t }));
    }

    function test_everyHearingBlockAsks(data) {
        verify(testCase.window.canvas.noticeComesFirst(data.typeId));
    }

    function test_aNonHearingAddLeavesTheAnswerOpen() {
        testCase.window.canvas.chooseBlock("effect.volume");
        verify(!testCase.notice.visible);
        compare(testCase.blockCount(), 1);
        verify(!FirstUseNotice.hearingAccepted);
    }

    function test_theKeyboardReachesEveryParagraphAndReturnDoesNotGoOn() {
        testCase.window.canvas.chooseBlock(testCase.hearingType);
        testCase.waitOpen();
        const seen = [];
        for (let i = 0; i < 4; ++i) {
            const item = testCase.window.activeFocusItem;
            verify(item, "something has focus");
            seen.push(item.Accessible.name);
            compare(item.Accessible.role, Accessible.StaticText);
            keyClick(Qt.Key_Return);
            compare(testCase.blockCount(), 0, "Return on the words adds nothing");
            verify(testCase.notice.visible);
            keyClick(Qt.Key_Tab);
        }
        compare(seen.join(" "), testCase.words(), "read in order, without tags");
        verify(testCase.notice.contentItem.contentItem.contentY > 0, "the last paragraph was scrolled to");
        const next = testCase.window.activeFocusItem;
        verify(String(next).startsWith("Button"), "Tab goes on to a button");
    }

    function test_arrowsScrollTheWords() {
        testCase.window.canvas.chooseBlock(testCase.hearingType);
        testCase.waitOpen();
        const view = testCase.notice.contentItem.contentItem;
        verify(view.contentHeight > view.height, "the short window makes the words scroll");
        const from = view.contentY;
        keyClick(Qt.Key_Down);
        verify(view.contentY > from);
        keyClick(Qt.Key_PageDown);
        verify(view.contentY > from);
    }

    function test_readingAgainChangesNothing() {
        testCase.notice.read();
        testCase.waitOpen();
        compare(testCase.notice.customFooterActions.length, 0, "there is nothing to go on to");
        testCase.notice.close();
        testCase.waitShut();
        verify(!FirstUseNotice.hearingAccepted);
        compare(testCase.blockCount(), 0);
    }
}
