// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The block library offscreen, against the mock engine: that it offers
// the blocks this build has and nobody else's, that searching narrows it by
// name and by description, that clicking puts a block on the canvas through
// the engine, and that a folded category is still folded next time.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "BlockLibrary"
    when: windowShown

    property string sessionId: ""

    // **In a window of its own.** A list only builds the delegates it can
    // show, and an item that is in no window shows nothing at all — so a
    // test that reached for a row would be told there was none.
    Component {
        id: libraryWindow

        Window {
            property alias library: inner

            width: 320
            height: 700
            visible: true

            BlockLibrary {
                id: inner

                anchors.fill: parent
                mimeType: "application/x-reverblabr-blocktype"
            }
        }
    }

    // **Short enough to scroll**: every block the build has, in a
    // window not much more than a quarter of a screen high.
    Component {
        id: shortLibraryWindow

        Window {
            property alias library: inner

            width: 320
            height: 360
            visible: true

            BlockLibrary {
                id: inner

                anchors.fill: parent
                mimeType: "application/x-reverblabr-blocktype"
            }
        }
    }

    Component {
        id: canvasComponent

        SessionCanvas {
            width: 800
            height: 600
        }
    }

    Component {
        id: mainWindow

        Main {}
    }

    // **Wide enough for the canvas to have room**: the library beside it,
    // the tiles and an empty spot to click on.
    Component {
        id: wideWindow

        Main {
            width: 1400
            height: 900
        }
    }

    Component {
        id: dragSpy

        SignalSpy {
            signalName: "activeChanged"
        }
    }

    Component {
        id: chosenSpy

        SignalSpy {
            signalName: "blockChosen"
        }
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Library");
        verify(testCase.sessionId !== "", "the mock opened a session");
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    // The singletons live as long as the QML engine does, so one test's
    // folded category and one test's blocks are not the next test's.
    function init() {
        SessionModel.sessionId = testCase.sessionId;
        WindowState.setCollapsedCategories([]);
    }

    function cleanup() {
        SessionModel.removeBlocks(SessionModel.blockIds);
        WindowState.setCollapsedCategories([]);
    }

    /// A library in a window, ready to be read.
    function openLibrary() {
        const window = createTemporaryObject(libraryWindow, null);
        verify(window, "there is a window for the library");
        waitForRendering(window.library);
        return window.library;
    }

    /// The type ids the library is showing, in the order it shows them.
    function shownTypes(library) {
        const types = [];
        for (const row of library.rows) {
            if (row.kind === "block") {
                types.push(row.id);
            }
        }
        return types;
    }

    function shownCategories(library) {
        const categories = [];
        for (const row of library.rows) {
            if (row.kind === "category") {
                categories.push(row.id);
            }
        }
        return categories;
    }

    function indexOf(library, kind, id) {
        for (let i = 0; i < library.rows.length; ++i) {
            if (library.rows[i].kind === kind && library.rows[i].id === id) {
                return i;
            }
        }
        return -1;
    }

    /// The delegate at \a index, scrolled into view first: a list builds
    /// the rows it can show and no more.
    ///
    /// **Waited for as a row, not as a frame.** `waitForRendering()` waits
    /// for the next frame and gives up after five seconds without failing,
    /// and a list whose row is already in view draws no new frame: with the
    /// design's shorter rows every heading was in view, and each lookup cost
    /// the whole five seconds.
    function rowAt(library, index) {
        if (index < 0) {
            return null;
        }
        library.list.positionViewAtIndex(index, ListView.Beginning);
        library.list.forceLayout();
        tryVerify(() => library.list.itemAtIndex(index) !== null, 2000, "row " + index + " was built");
        return library.list.itemAtIndex(index);
    }

    function rowFor(library, typeId) {
        return testCase.rowAt(library, testCase.indexOf(library, "block", typeId));
    }

    function rowForCategory(library, category) {
        return testCase.rowAt(library, testCase.indexOf(library, "category", category));
    }

    function test_onlyTheBlocksThisBuildHasAreOffered() {
        const library = testCase.openLibrary();
        const shown = testCase.shownTypes(library);

        verify(shown.length > 0, "the library has something in it");
        for (const typeId of shown) {
            verify(BlockCatalogue.types.indexOf(typeId) >= 0,
                   typeId + " is offered but this build cannot make it");
        }
        // The presentation table covers the whole planned catalogue so that
        // a session from a later version still shows words; the library is
        // what can actually be added.
        verify(BlockPresentation.types.length > shown.length,
               "the table knows about more blocks than this build has");
    }

    function test_theHeadingsAreInTheOrderASessionIsBuiltIn() {
        const library = testCase.openLibrary();
        const shown = testCase.shownCategories(library);

        let previous = -1;
        for (const category of shown) {
            const at = BlockPresentation.categories.indexOf(category);
            verify(at > previous, category + " is out of order");
            previous = at;
        }
        // An empty heading says this build is missing something, which is a
        // thing to put in release notes and not in a sidebar.
        for (const row of library.rows) {
            if (row.kind === "category") {
                verify(row.count > 0, row.id + " is a heading with nothing under it");
            }
        }
    }

    // **A shelf for listening with one ear at a time**, after the
    // blocks for less usual jobs and before the plugins, with Join L/R on it
    // from the start and the hearing blocks as they come.
    function test_theHearingGroupHoldsJoinLR() {
        const library = testCase.openLibrary();
        const shown = testCase.shownCategories(library);
        verify(shown.indexOf("hearing") >= 0, "there is no Hearing heading");
        compare(BlockPresentation.categoryName("hearing"), "Hearing");
        const categories = BlockPresentation.categories;
        verify(categories.indexOf("hearing") > categories.indexOf("advanced"), "Hearing is not after Advanced");
        verify(categories.indexOf("hearing") < categories.indexOf("plugins"), "Hearing is not before the plugins");

        compare(BlockPresentation.category("hearing.joinlr"), "hearing");
        compare(BlockPresentation.name("hearing.joinlr"), "Join L/R");
        const heading = library.rows.findIndex(row => row.kind === "category" && row.id === "hearing");
        const join = library.rows.findIndex(row => row.kind === "block" && row.id === "hearing.joinlr");
        verify(join > heading, "Join L/R is not under the Hearing heading");
        for (let index = heading + 1; index < join; ++index) {
            compare(library.rows[index].kind, "block", "another heading comes between Hearing and Join L/R");
        }
        // Its two inputs are named for the ear each carries.
        compare(BlockPresentation.laneName("left"), "Left");
        compare(BlockPresentation.laneName("right"), "Right");
    }

    function test_searchNarrowsByNameAndByDescription() {
        const library = testCase.openLibrary();
        const all = testCase.shownTypes(library);
        verify(all.indexOf("effect.volume") >= 0, "Volume is in the library to begin with");

        // By name, in any case: nobody types capitals into a search box.
        library.searchField.text = BlockPresentation.name("effect.volume").toLowerCase();
        const byName = testCase.shownTypes(library);
        verify(byName.indexOf("effect.volume") >= 0, "searching for its name found it");
        verify(byName.length < all.length, "and narrowed the list");

        // By description, which is half of what somebody types: they know
        // what they want done rather than what it is called.
        const description = BlockPresentation.description("meter.peakrms");
        library.searchField.text = description.split(" ")[0].toUpperCase();
        verify(testCase.shownTypes(library).indexOf("meter.peakrms") >= 0,
               "searching for what it does found it");

        library.searchField.text = "nothingiscalledthis";
        compare(library.rows.length, 0);

        library.searchField.text = "";
        compare(testCase.shownTypes(library).length, all.length);
    }

    function test_aFoldedCategoryHidesItsBlocksAndIsStillFoldedNextTime() {
        const library = testCase.openLibrary();
        const before = testCase.shownTypes(library).length;
        const category = testCase.shownCategories(library)[0];

        library.toggleCategory(category);
        verify(library.isCollapsed(category), "the category is folded");
        verify(testCase.shownTypes(library).length < before, "its blocks went with it");
        // The heading stays, with its count, so a folded category is
        // something a person can find again.
        verify(testCase.shownCategories(library).indexOf(category) >= 0, "the heading is still there");

        // A library built again — which is what the next run is — finds it
        // the way it was left.
        const again = testCase.openLibrary();
        verify(again.isCollapsed(category), "it is still folded");

        again.toggleCategory(category);
        verify(!again.isCollapsed(category), "and it opens again");
    }

    function test_theWordsAreTheCataloguesAndNotTheEngines() {
        const library = testCase.openLibrary();
        const row = testCase.rowFor(library, "effect.volume");

        verify(row !== null, "there is a row for the Volume block");
        compare(row.label, BlockPresentation.name("effect.volume"));
        verify(row.label !== "effect.volume", "and it is a word, not an id");
        // What a screen reader reads, and the longer form the tooltip holds.
        compare(row.Accessible.name, BlockPresentation.name("effect.volume"));
        compare(row.Accessible.description, BlockPresentation.description("effect.volume"));
        compare(row.toolTipText, BlockPresentation.tooltip("effect.volume"));

        const heading = testCase.rowForCategory(library, "effects");
        verify(heading !== null, "there is a heading for the effects");
        compare(heading.label, BlockPresentation.categoryName("effects"));
        verify(heading.label !== "effects", "and it is a word, not an id");
    }

    function test_clickingARowPutsTheBlockOnTheCanvas() {
        const canvas = createTemporaryObject(canvasComponent, null);
        const library = testCase.openLibrary();
        let chosen = "";
        library.blockChosen.connect(function (typeId) {
            chosen = typeId;
            canvas.addBlockAfterSelection(typeId);
        });

        const row = testCase.rowFor(library, "effect.volume");
        verify(row !== null, "there is a row to click");
        mouseClick(row);

        compare(chosen, "effect.volume");
        compare(SessionModel.blockIds.length, 1);
        compare(SessionModel.block(SessionModel.blockIds[0]).type, "effect.volume");
    }

    function test_enterOnAHeadingFoldsItAndOnABlockAddsIt() {
        const library = testCase.openLibrary();
        let chosen = "";
        library.blockChosen.connect(function (typeId) {
            chosen = typeId;
        });

        // The list is headings and blocks together, so the same key has to
        // do the right thing on either.
        const heading = library.rows[0];
        library.activate(heading);
        verify(library.isCollapsed(heading.id), "Enter on a heading folded it");
        library.activate(heading);
        verify(!library.isCollapsed(heading.id), "and opened it again");

        library.activate({ kind: "block", id: "effect.volume", count: 0 });
        compare(chosen, "effect.volume");
    }

    // **Enter in the search adds the first block it found**, as
    // Getting started says. The first row is the heading the match is
    // under, and Enter used to fold that heading instead.
    function test_enterInTheSearchAddsTheFirstBlockFound() {
        const library = testCase.openLibrary();
        let chosen = "";
        library.blockChosen.connect(function (typeId) {
            chosen = typeId;
        });
        library.searchField.forceActiveFocus();
        library.searchField.text = BlockPresentation.name("effect.volume").toLowerCase();
        compare(library.rows[0].kind, "category", "the first row is not a heading any more");
        keyClick(Qt.Key_Return);
        compare(chosen, "effect.volume", "Enter in the search did not add the block it found");
        verify(!library.isCollapsed(library.rows[0].id), "Enter in the search folded the heading");
        chosen = "";
        keyClick(Qt.Key_Enter);
        compare(chosen, "effect.volume", "the keypad's Enter did not add it");
        library.searchField.text = "";
    }

    function test_theKeyboardReachesEveryRow() {
        const library = testCase.openLibrary();
        library.list.forceActiveFocus();
        library.list.currentIndex = 0;

        // Down moves through headings and blocks alike, in the order they
        // are drawn: one list, so one order.
        keyClick(Qt.Key_Down);
        compare(library.list.currentIndex, 1);
        keyClick(Qt.Key_Down);
        compare(library.list.currentIndex, 2);
        keyClick(Qt.Key_Up);
        compare(library.list.currentIndex, 1);
    }

    function test_aRowCarriesItsTypeIdUnderTheTypeTheCanvasAccepts() {
        // The two halves of a drag are two files, so what travels between
        // them is checked rather than assumed. The gesture itself wants a
        // pointer and a window system; this is the contract it carries.
        const library = testCase.openLibrary();
        const row = testCase.rowFor(library, "effect.volume");

        verify(row !== null, "there is a row to drag");
        compare(row.Drag.mimeData[library.mimeType], "effect.volume");

        // A heading is not a thing to drag onto a canvas.
        const heading = testCase.rowForCategory(library, testCase.shownCategories(library)[0]);
        verify(heading !== null, "there is a heading");
        compare(heading.Drag.active, false);
    }

    // **One gesture is one block**. A row is a button and a drag
    // source at once, and the drop is the canvas's business: when the row
    // also counted the release as a click, one drag added a block where it
    // was dropped *and* another beside it, which is the two tiles the
    // report showed, half a tile apart.
    /// The list's vertical scroll bar: the scroll view's child that has an
    /// orientation and a size.
    function scrollBarOf(library) {
        for (const child of library.list.parent.children) {
            if (child.orientation === Qt.Vertical && child.size !== undefined) {
                return child;
            }
        }
        return null;
    }

    /// The rows the list has built, in the order it built them.
    function builtRows(library) {
        const rows = [];
        for (const child of library.list.contentItem.children) {
            if (child.typeId !== undefined) {
                rows.push(child);
            }
        }
        return rows;
    }

    /// **The scroll bar's thumb scrolls the list from its end**, and
    /// no row takes the gesture as a block's drag. On the desktop the row
    /// under the thumb took it and the thumb moved nothing; offscreen there
    /// is no native drag loop, so this passed before the fix as well. What
    /// holds the fix is test_noRowLiesUnderTheScrollBar below.
    function test_theScrollBarsThumbScrollsTheListAtItsEnd() {
        const window = createTemporaryObject(shortLibraryWindow, null);
        const library = window.library;
        waitForRendering(library);
        const list = library.list;
        verify(list.contentHeight > list.height * 1.5, "the list is long enough to scroll");

        list.positionViewAtEnd();
        waitForRendering(list);
        const atEnd = list.contentY;
        const bar = testCase.scrollBarOf(library);
        verify(bar && bar.visible, "the list shows its scroll bar");
        tryVerify(() => bar.size < 1, 1000, "the scroll bar has a thumb smaller than itself");

        const thumb = bar.contentItem;
        const x = thumb.width / 2;
        const y = thumb.height / 2;
        mousePress(thumb, x, y);
        for (let step = 1; step <= 10; ++step) {
            mouseMove(thumb, x, y - step * 15);
        }
        mouseRelease(thumb, x, y - 150);

        for (const row of testCase.builtRows(library)) {
            verify(!row.dragged, "the row " + row.typeId + " took the thumb's drag as a block's");
        }
        verify(list.contentY < atEnd - list.height / 2,
               "dragging the thumb up moved the list from " + atEnd + " only to " + list.contentY);
    }

    /// **No row lies under the scroll bar** while it is shown, so a
    /// press on the bar can only ever be the bar's.
    function test_noRowLiesUnderTheScrollBar() {
        const window = createTemporaryObject(shortLibraryWindow, null);
        const library = window.library;
        waitForRendering(library);
        const bar = testCase.scrollBarOf(library);
        verify(bar && bar.visible, "the list shows its scroll bar");
        const barLeft = bar.mapToItem(library, 0, 0).x;
        const rows = testCase.builtRows(library);
        verify(rows.length > 0, "the list built some rows");
        for (const row of rows) {
            const rowRight = row.mapToItem(library, row.width, 0).x;
            verify(rowRight <= barLeft + 0.5,
                   row.typeId + " reaches " + rowRight + ", under the scroll bar at " + barLeft);
        }
    }

    function test_aRowThatWasDraggedIsNotAlsoAClick() {
        const library = testCase.openLibrary();
        const row = testCase.rowFor(library, "effect.volume");
        verify(row !== null, "there is a row to drag");

        const chosen = createTemporaryObject(chosenSpy, testCase, { target: library });

        // A press that did not move is a click, and still adds a block.
        mouseClick(row, row.width / 2, row.height / 2);
        compare(chosen.count, 1, "a plain click should still choose the block");

        // A press that moved is a drag. Counted as Drag.active changes:
        // offscreen, Qt 6.10 ends an automatic drag at once, so it may be
        // false again by the time the moves return.
        const started = createTemporaryObject(dragSpy, testCase, { target: row.Drag });
        mousePress(row, row.width / 2, row.height / 2);
        mouseMove(row, row.width / 2 + 40, row.height / 2 + 30);
        mouseMove(row, row.width / 2 + 120, row.height / 2 + 90);
        verify(started.count > 0, "the gesture should have become a drag");
        mouseRelease(row, row.width / 2 + 120, row.height / 2 + 90);

        // **What the button is told afterwards is the window system's, not
        // ours.** An automatic drag runs a loop of its own: offscreen the
        // press is taken away and no click follows, and on a real
        // compositor the release comes back and one does — which is the
        // case the report hit and the only one worth testing. So it is made
        // to happen here rather than waited for.
        row.clicked();
        compare(chosen.count, 1, "a row that was dragged was counted as a click as well");

        // And the row is a button again afterwards: the guard is about the
        // gesture that has just happened, not about the row for ever.
        mouseClick(row, row.width / 2, row.height / 2);
        compare(chosen.count, 2, "a click after a drag should choose the block");
    }

    function test_theSidebarsBlocksTabHasTheLibraryInIt() {
        const window = createTemporaryObject(mainWindow, testCase);
        verify(window.blockLibrary, "the Blocks tab is filled in");
        compare(window.sidebarIndex, 0);

        // Ctrl+F is how a keyboard reaches it, and the window owns the key
        // because the tab has to be shown as well as typed in.
        window.sidebarIndex = 1;
        const find = window.menus.menuAt(1).actionAt(3);
        compare(find.shortcut, StandardKey.Find);
        find.trigger();
        compare(window.sidebarIndex, 0);
        compare(window.blockLibrary.searchField.focus, true);
    }

    function test_aHeadingSaysHowManyBlocksInTheRightNumber() {
        // A screen reader read "Outputs, 1 blocks". **Every heading,
        // against its own count**, so the test does not depend on how many
        // blocks a category happens to have in this build.
        const library = testCase.openLibrary();
        let sawOne = false;
        let sawMany = false;
        const checkHeadings = () => {
            for (const category of ["sources", "outputs", "effects", "meters"]) {
                const heading = testCase.rowForCategory(library, category);
                if (heading === null) {
                    continue;
                }
                const count = heading.entry.count;
                const expected = BlockPresentation.categoryName(category) + ", " + count
                    + (count === 1 ? " block" : " blocks");
                compare(heading.Accessible.name, expected, category);
                sawOne = sawOne || count === 1;
                sawMany = sawMany || count > 1;
            }
        };
        checkHeadings();
        // **One block under a heading, whatever this build has**: a search
        // that finds only the Peak/RMS block leaves the Meters heading over
        // it alone, which no longer depends on a category happening to hold
        // one block (the VU block, made Meters two).
        library.searchField.text = BlockPresentation.name("meter.peakrms").toLowerCase();
        compare(testCase.shownTypes(library), ["meter.peakrms"]);
        checkHeadings();
        library.searchField.text = "";
        verify(sawOne && sawMany, "no heading with one block and one with several: both forms were not checked");
    }

    /// The main window, drawn and in front, with an empty session.
    function openWideWindow() {
        const window = createTemporaryObject(wideWindow, testCase);
        tryVerify(() => AppShell.hasSession && BlockCatalogue.ready, 3000, "no session to build in");
        SessionModel.removeBlocks(SessionModel.blockIds);
        SessionModel.setAutoConnect(true);
        window.requestActivate();
        tryVerify(() => window.active, 2000, "the window never came to the front");
        waitForRendering(window.contentItem);
        return window;
    }

    function tileOf(canvas, blockId) {
        for (let i = 0; i < canvas.tiles.count; ++i) {
            const tile = canvas.tiles.itemAt(i);
            if (tile !== null && tile.blockId === blockId) {
                return tile;
            }
        }
        return null;
    }

    /// The style's text menu on \a field, or null in a style without one.
    function textMenuOf(field) {
        for (const child of field.data) {
            if (child.editor === field && typeof child.popup === "function") {
                return child;
            }
        }
        return null;
    }

    /// Types \a text into the library's search the way a person does: a
    /// click into the field, then the keys.
    function typeIntoSearch(window, text) {
        const search = window.blockLibrary.searchField;
        mouseClick(search, search.width / 2, search.height / 2);
        verify(search.activeFocus, "a click into the search did not give it the keyboard");
        search.selectAll();
        for (const letter of text) {
            keyClick(letter);
        }
        compare(search.text, text);
    }

    // **Ctrl+Z on the canvas undoes the session after the search was
    // typed in.** In the application's style it did nothing: every text
    // field kept a menu whose Undo held Ctrl+Z for the whole window while
    // the field had typing to undo, and with the window's own Undo holding
    // it too the key belonged to neither.
    function test_ctrlZAfterTheSearchUndoesTheSessionOnceTheCanvasIsClicked() {
        const window = testCase.openWideWindow();
        const canvas = window.sessionCanvas;
        const added = SessionModel.addBlock("effect.volume", Qt.point(40, 200));
        verify(added !== "", "nothing was added");
        tryVerify(() => window.undoAction.enabled, 1000, "adding a block left nothing to undo");

        testCase.typeIntoSearch(window, "vol");
        // A spot on the canvas well away from the tile and the overlays.
        mouseClick(canvas, canvas.width * 0.6, canvas.height * 0.6);
        verify(!window.blockLibrary.searchField.activeFocus, "the search kept the keyboard after a click on the canvas");
        keyClick(Qt.Key_Z, Qt.ControlModifier);
        tryVerify(() => SessionModel.blockIds.indexOf(added) < 0, 1000,
                  "Ctrl+Z after a click on the canvas did not undo the session's last edit");
        compare(window.blockLibrary.searchField.text, "vol", "Ctrl+Z undid the typing as well");

        // **The field's own menu still opens**, with its Undo in it, and
        // is gone again, keys and all, once it closes. In the application's
        // style, which is the one that has it (the menu a right click
        // asks for is opened here as the request would open it).
        const textMenu = testCase.textMenuOf(window.blockLibrary.searchField);
        if (textMenu !== null) {
            textMenu.popup(Qt.point(4, 4));
            verify(textMenu.menu !== null, "the search's menu was not made");
            tryVerify(() => textMenu.menu.opened, 1000, "the search's menu did not open");
            compare(textMenu.menu.actionAt(0).text, "Undo", "the menu's first entry is not Undo");
            verify(textMenu.menu.actionAt(0).enabled, "the menu's Undo had nothing to undo after typing");
            textMenu.menu.close();
            tryVerify(() => textMenu.menu === null, 1000, "the search's menu was kept after it closed");
            // Offscreen the menu is a window of its own, which leaves the
            // main one not active when it goes.
            window.requestActivate();
            tryVerify(() => window.active, 2000, "the window did not come back to the front");
        }

        // **The same after a click on a tile.**
        window.redoAction.trigger();
        tryVerify(() => SessionModel.blockIds.indexOf(added) >= 0, 1000, "Redo did not put the block back");
        testCase.typeIntoSearch(window, "vu");
        waitForRendering(window.contentItem);
        const tile = testCase.tileOf(canvas, added);
        verify(tile !== null, "the block has a tile");
        mouseClick(tile, tile.width / 2, tile.height / 2);
        keyClick(Qt.Key_Z, Qt.ControlModifier);
        tryVerify(() => SessionModel.blockIds.indexOf(added) < 0, 1000,
                  "Ctrl+Z after a click on a tile did not undo the session's last edit");
        window.blockLibrary.searchField.text = "";
    }

    // **Enter adds the best match, and the list reads best first.**
    // "Peak" and Enter added Output Device, whose description says
    // "speakers", because Outputs comes before Meters; "eq" has to be an
    // EQ, not a block whose description says "frequency".
    function test_enterAddsTheBestMatchAndTheListShowsItFirst() {
        const library = testCase.openLibrary();
        let chosen = "";
        library.blockChosen.connect(typeId => chosen = typeId);
        library.searchField.forceActiveFocus();

        library.searchField.text = "Peak";
        verify(testCase.shownTypes(library).indexOf("output.device") >= 0,
               "the test needs a description match that the name match has to beat");
        compare(testCase.shownTypes(library)[0], "meter.peakrms", "the name that starts with it is not first");
        keyClick(Qt.Key_Return);
        compare(chosen, "meter.peakrms", "Enter did not add the block whose name starts with what was typed");

        library.searchField.text = "eq";
        const shown = testCase.shownTypes(library);
        verify(shown.some(typeId => !BlockPresentation.name(typeId).toLowerCase().includes("eq")),
               "the test needs a block that only its description finds");
        keyClick(Qt.Key_Return);
        verify(/\bEQ\b/.test(BlockPresentation.name(chosen)), "Enter on eq added " + BlockPresentation.name(chosen));

        // **Inside a category, the best match first**: a name before a
        // description, and a name with a word that starts with it before
        // one that has it inside a word.
        let category = "";
        let previous = -1;
        for (const row of library.rows) {
            if (row.kind === "category") {
                category = row.id;
                previous = -1;
            } else if (row.kind === "block") {
                const rank = library.rankOf(row.id, "eq");
                verify(rank >= previous, row.id + " came after a better match in " + category);
                previous = rank;
            }
        }
        const frequency = shown.find(typeId => BlockPresentation.name(typeId).toLowerCase().startsWith("frequency"));
        if (frequency !== undefined) {
            verify(shown.indexOf(frequency) > shown.indexOf(chosen), "Frequency came before the EQ");
        }

        // **The categories follow their best match**: Meters, with the
        // name that starts with "peak", comes before Outputs.
        library.searchField.text = "peak";
        const categories = testCase.shownCategories(library);
        verify(categories.indexOf("meters") < categories.indexOf("outputs"), "Meters is not first: " + categories);

        // **With nothing typed, the catalogue's own order** is kept.
        library.searchField.text = "";
        compare(testCase.shownCategories(library), BlockPresentation.categories.filter(
                    category => testCase.shownCategories(library).indexOf(category) >= 0));
    }
}
