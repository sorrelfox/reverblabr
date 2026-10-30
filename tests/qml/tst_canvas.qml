// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The session canvas offscreen, against the mock engine: that adding,
// selecting, moving, renaming and removing a tile all go through the engine
// and survive the session being read back, and that zooming and fitting put
// the view where they say they do.
//
// **The mock is driven, not mocked.** TestEngine is the same facade the
// window is bound to, so an edit made here is the edit the real engine
// would be given.

import QtQuick
import QtTest
import org.kde.kirigami as Kirigami
import org.kde.ki18n
import io.github.reverblabr

TestCase {
    id: testCase

    name: "SessionCanvas"
    when: windowShown

    property string sessionId: ""

    // **No parent.** An item's `visible` is its own and its parent's
    // together, and a test case offscreen is not shown, so a canvas
    // parented to it would read as hidden however it was made.
    Component {
        id: canvasComponent

        SessionCanvas {
            width: 800
            height: 600
        }
    }

    // A canvas in a window of its own, for the tests that press a button
    // and move a pointer: an item with no window is an item no synthetic
    // mouse event can reach.
    Component {
        id: shownCanvas

        Window {
            property alias canvas: inner

            width: 800
            height: 600
            visible: true

            SessionCanvas {
                id: inner

                anchors.fill: parent
            }
        }
    }

    Component {
        id: mainWindow

        Main {}
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Canvas");
        verify(testCase.sessionId !== "", "the mock opened a session");
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    // **Said again before every test**, because a test that makes a whole
    // window binds the session model to the window's session — which is
    // empty — and the tests after it would then be editing nothing.
    function init() {
        SessionModel.sessionId = testCase.sessionId;
    }

    // One test's session is not the next test's. The singletons live as long
    // as the QML engine does, so what is left on the canvas has to be taken
    // off again.
    function cleanup() {
        SessionModel.removeBlocks(SessionModel.blockIds);
        compare(SessionModel.blockIds.length, 0);
    }

    /// The tile showing \a blockId, or null.
    function tileFor(canvas, blockId) {
        for (let row = 0; row < canvas.tiles.count; ++row) {
            const tile = canvas.tiles.itemAt(row);
            if (tile !== null && tile.blockId === blockId) {
                return tile;
            }
        }
        return null;
    }

    // **A block for one ear says which**: an L or an R on its ear's
    // colour, from the moment it is added — left, until somebody says
    // otherwise — and following the setting. A block without an Ear has
    // no badge. The block is the tests' own, test.oneEar, until the first
    // hearing block ships.
    function test_aBlockForOneEarWearsItsEar() {
        const canvas = createTemporaryObject(canvasComponent, null);
        const ear = canvas.addBlockAt("test.oneEar", 200, 150);
        verify(ear !== "", "the engine made the block for one ear");
        const volume = canvas.addBlockAt("effect.volume", 420, 150);
        tryVerify(() => testCase.tileFor(canvas, ear) !== null && testCase.tileFor(canvas, volume) !== null, 2000,
                  "both tiles are on the canvas");
        const tile = testCase.tileFor(canvas, ear);

        tryCompare(tile, "ear", "left");
        verify(tile.earBadge.visible, "a block for the left ear shows no badge");
        compare(tile.earBadge.letter, "L");
        compare(tile.earBadge.color, AppColors.earLeft);
        verify(tile.Accessible.name.indexOf("left ear") >= 0, "a screen reader is not told which ear: "
               + tile.Accessible.name);

        SessionModel.setParam(ear, "ear", "right");
        tryCompare(tile, "ear", "right");
        compare(tile.earBadge.letter, "R");
        compare(tile.earBadge.color, AppColors.earRight);
        verify(tile.Accessible.name.indexOf("right ear") >= 0, tile.Accessible.name);

        const other = testCase.tileFor(canvas, volume);
        compare(other.ear, "");
        verify(!other.earBadge.visible, "a block without an Ear shows a badge");
    }

    function test_addingABlockGoesThroughTheEngineAndLandsOnTheGrid() {
        const canvas = createTemporaryObject(canvasComponent, null);
        const added = canvas.addBlockAt("effect.volume", 203, 149);

        verify(added !== "", "the engine gave the block an id");
        compare(SessionModel.blockIds, [added]);
        compare(SessionModel.block(added).type, "effect.volume");

        // **Dropped on a spot means centred on it**, and the eight-pixel
        // snap is what makes two tiles dropped by eye line up exactly.
        const at = SessionModel.block(added).position;
        compare(at.x % canvas.snapPixels, 0);
        compare(at.y % canvas.snapPixels, 0);
        verify(Math.abs(at.x + canvas.tileSize().width / 2 - 203) <= canvas.snapPixels,
               "the tile is centred on where it was dropped");

        // And it is what is selected, so that whatever is done next is done
        // to the block that was just added.
        compare(canvas.selectedIds, [added]);
        compare(canvas.blocks.count, 1);
        verify(testCase.tileFor(canvas, added) !== null, "there is a tile for it");
    }

    function test_clickingAddsAfterWhateverIsSelected() {
        const canvas = createTemporaryObject(canvasComponent, null);
        const first = canvas.addBlockAt("source.device", 100, 100);
        const second = canvas.addBlockAfterSelection("effect.volume");

        const left = SessionModel.block(first).position;
        const right = SessionModel.block(second).position;
        verify(right.x > left.x + canvas.tileSize().width - 1,
               "the new block is clear of the one it follows");
        compare(right.y, left.y);
        compare(canvas.selectedIds, [second]);
    }

    function test_selectingOneTileAndThenSeveral() {
        const canvas = createTemporaryObject(canvasComponent, null);
        const first = canvas.addBlockAt("effect.volume", 100, 100);
        const second = canvas.addBlockAt("meter.peakrms", 300, 100);
        compare(canvas.selectedIds, [second]);

        canvas.pressOn(first, Qt.NoModifier);
        compare(canvas.selectedIds, [first]);

        // Ctrl adds to the selection and takes away again, which is what it
        // does in every list on the desktop.
        canvas.pressOn(second, Qt.ControlModifier);
        compare(canvas.selectedIds.length, 2);
        canvas.pressOn(second, Qt.ControlModifier);
        compare(canvas.selectedIds, [first]);

        // **A press on a tile that is already selected leaves the selection
        // alone**, or dragging two tiles by one of them would drag one.
        canvas.pressOn(second, Qt.ControlModifier);
        canvas.pressOn(first, Qt.NoModifier);
        compare(canvas.selectedIds.length, 2);

        canvas.clearSelection();
        compare(canvas.selectedIds.length, 0);
    }

    function test_theRubberBandTakesEverythingItTouches() {
        const canvas = createTemporaryObject(canvasComponent, null);
        const near = canvas.addBlockAt("effect.volume", 120, 120);
        const far = canvas.addBlockAt("effect.volume", 600, 500);
        canvas.clearSelection();

        // Touching is enough rather than containing: a band that has to
        // swallow a tile whole is a band nobody can draw around a row.
        canvas.selectWithin(Qt.rect(100, 100, 60, 60), false);
        compare(canvas.selectedIds, [near]);

        canvas.selectWithin(Qt.rect(0, 0, 800, 600), false);
        compare(canvas.selectedIds.length, 2);
        verify(canvas.isSelected(far), "the far tile is in the band too");
    }

    function test_draggingATileMovesItThroughTheEngineAndSnaps() {
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        waitForRendering(canvas);
        const block = canvas.addBlockAt("effect.volume", 200, 200);
        const before = SessionModel.block(block).position;
        const tile = testCase.tileFor(canvas, block);
        verify(tile !== null, "there is a tile to drag");

        // The pointer, not the function behind it: what a person does is
        // press on the tile, move and let go.
        mousePress(tile, 20, 20);
        mouseMove(tile, 20 + 37, 20 + 45);
        verify(canvas.dragging, "the tile is following the pointer");
        // Nothing has been sent yet: the session hears about a move when
        // the button comes up, not sixty times a second on the way.
        compare(SessionModel.block(block).position, before);
        mouseRelease(tile, 20 + 37, 20 + 45);

        const after = SessionModel.block(block).position;
        compare(after.x % canvas.snapPixels, 0);
        compare(after.y % canvas.snapPixels, 0);
        compare(after.x, canvas.snapped(before.x + 37));
        compare(after.y, canvas.snapped(before.y + 45));
        compare(canvas.dragging, false);
    }

    // --- dragged by the header, worked by the face ---

    function test_aBlockIsDraggedByItsHeaderAndNotByItsFace() {
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        waitForRendering(canvas);
        const block = canvas.addBlockAt("meter.peakrms", 200, 200);
        const tile = testCase.tileFor(canvas, block);
        const before = SessionModel.block(block).position;

        // The face: a press there selects the block and a drag moves
        // nothing.
        const faceAt = tile.face.mapToItem(tile, tile.face.width / 2, tile.face.height / 2);
        mousePress(tile, faceAt.x, faceAt.y);
        compare(canvas.selectedIds, [block], "a press on the face did not select the block");
        mouseMove(tile, faceAt.x + 30, faceAt.y + 30);
        mouseMove(tile, faceAt.x + 60, faceAt.y + 60);
        verify(!canvas.dragging, "the block followed a drag that started on its face");
        mouseRelease(tile, faceAt.x + 60, faceAt.y + 60);
        compare(SessionModel.block(block).position, before, "a drag on the face moved the block");

        // A double-click on the face still opens the settings.
        mouseDoubleClickSequence(tile, faceAt.x, faceAt.y);
        compare(canvas.settings.blockId, block, "a double-click on the face did not open the settings");
        canvas.closeSettings();

        // The header, which is what a block is carried by.
        const headerAt = tile.header.mapToItem(tile, tile.header.width / 2, tile.header.height / 2);
        mousePress(tile, headerAt.x, headerAt.y);
        mouseMove(tile, headerAt.x + 30, headerAt.y + 30);
        mouseMove(tile, headerAt.x + 60, headerAt.y + 60);
        verify(canvas.dragging, "the block did not follow a drag by its header");
        mouseRelease(tile, headerAt.x + 60, headerAt.y + 60);
        verify(SessionModel.block(block).position.x !== before.x, "a drag by the header did not move the block");
    }

    function test_theSliderOnAVolumeTileWorksOnTheCanvas() {
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        waitForRendering(canvas);
        const block = canvas.addBlockAt("effect.volume", 200, 200);
        const tile = testCase.tileFor(canvas, block);
        const before = SessionModel.block(block).position;
        const gainBefore = SessionModel.param(block, "gain") ?? VolumeScale.defaultGain;
        waitForRendering(canvas);

        const slider = tile.face.item.slider;
        const y = slider.height / 2;
        mousePress(slider, slider.width * 0.9, y);
        mouseMove(slider, slider.width * 0.5, y);
        mouseMove(slider, slider.width * 0.2, y);
        mouseRelease(slider, slider.width * 0.2, y);
        wait(50);

        const gainAfter = SessionModel.param(block, "gain") ?? VolumeScale.defaultGain;
        verify(gainAfter < gainBefore, "the slider on the tile did not turn the volume down: " + gainAfter);
        compare(SessionModel.block(block).position, before, "working the slider moved the block");
        verify(!canvas.dragging, "working the slider started dragging the block");
        compare(canvas.settings.blockId, "", "working the slider opened the settings");

        // And the settings, opened now, say the same.
        canvas.activateBlock(block);
        wait(50);
        fuzzyCompare(canvas.settings.body.item.slider.value, tile.face.item.slider.value, 0.01,
                     "the panel and the tile disagree about the volume");
    }

    // --- a block's settings, beside that block ---

    function test_aSingleClickSelectsABlockAndOpensNothing() {
        // A click is for choosing a block. Its settings are asked for
        // on purpose, with a double-click, Enter or the menu.
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        waitForRendering(canvas);
        const first = canvas.addBlockAt("effect.volume", 200, 200);
        const second = canvas.addBlockAt("effect.volume", 200, 400);
        const tile = testCase.tileFor(canvas, first);
        verify(tile !== null, "there is a tile to click");

        mouseClick(tile, 20, 20);
        compare(canvas.selectedIds, [first], "the click did not select the block");
        compare(canvas.settings.blockId, "", "a single click opened the settings");

        // Nor does a click on another block take over a panel that is open.
        canvas.activateBlock(first);
        mouseClick(testCase.tileFor(canvas, second), 20, 20);
        compare(canvas.selectedIds, [second]);
        compare(canvas.settings.blockId, first, "a single click on another block changed the settings showing");
    }

    // In the Reverblabr appearance a tile is the prototype's 132 by
    // 96 with its 24-pixel band, and its settings open 16 pixels to its
    // right at the prototype's 322, eight above its top.
    function test_aTileAndItsPanelAreWhereThePrototypePutsThem() {
        Appearance.mode = Appearance.ReverblabrDark;
        try {
            const window = createTemporaryObject(shownCanvas, null);
            const canvas = window.canvas;
            waitForRendering(canvas);
            const block = canvas.addBlockAt("effect.volume", 200, 200);
            const tile = testCase.tileFor(canvas, block);
            verify(tile !== null, "there is a tile to measure");
            canvas.clearSelection();
            waitForRendering(canvas);

            compare(tile.width, 132, "the tile is not the prototype's width");
            compare(tile.height, 96, "the tile is not the prototype's height");
            // The band's 24 pixels inside the tile's one-pixel border, and
            // the rule under them.
            compare(tile.headerRule.mapToItem(tile, 0, 0).y, 25, "the tile's header is not 24 pixels");

            canvas.activateBlock(block);
            tryCompare(canvas.settings, "visible", true);
            waitForRendering(canvas);
            const topRight = tile.mapToItem(canvas, tile.width, 0);
            compare(canvas.settings.width, 322, "the panel is not the prototype's width");
            compare(canvas.settings.x - topRight.x, 16, "the panel is not 16 pixels off its block");
            compare(topRight.y - canvas.settings.y, 8, "the panel's top is not 8 pixels above its block's");
        } finally {
            Appearance.mode = SuiteAppearance;
        }
    }

    // Selecting a tile drew a thicker border, and everything inside
    // the tile moved in by a pixel each side to make room for it.
    function test_selectingATileMovesNothingInsideIt() {
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        waitForRendering(canvas);
        const block = canvas.addBlockAt("effect.volume", 200, 200);
        const other = canvas.addBlockAt("effect.volume", 200, 400);
        const tile = testCase.tileFor(canvas, block);
        verify(tile !== null, "there is a tile to click");
        canvas.clearSelection();
        waitForRendering(canvas);

        const rectOf = item => {
            const at = item.mapToItem(tile, 0, 0);
            return Qt.rect(at.x, at.y, item.width, item.height);
        };
        const rule = rectOf(tile.headerRule);
        const face = rectOf(tile.face);
        verify(!tile.selectionHalo.visible, "an unselected tile has a halo");

        mouseClick(tile, 20, 10);
        compare(canvas.selectedIds, [block]);
        waitForRendering(canvas);
        compare(rectOf(tile.headerRule), rule, "selecting the tile moved its header rule");
        compare(rectOf(tile.face), face, "selecting the tile moved or resized its face");
        verify(tile.selectionHalo.visible, "a selected tile has no halo");

        // **One ring, not two**: the click that selected the tile also
        // focused it, and the halo already says which tile that is.
        verify(tile.activeFocus, "the click did not focus the tile");
        verify(!tile.focusRing.visible, "a clicked tile drew a focus ring over its halo");

        // The keyboard's focus is still shown.
        const next = testCase.tileFor(canvas, other);
        next.forceActiveFocus(Qt.TabFocusReason);
        verify(next.focusRing.visible, "a tile reached from the keyboard shows no focus ring");
    }

    function test_aDoubleClickOpensABlocksSettingsBesideIt() {
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        waitForRendering(canvas);
        const block = canvas.addBlockAt("effect.volume", 200, 200);
        const tile = testCase.tileFor(canvas, block);
        verify(tile !== null, "there is a tile to click");
        compare(canvas.settings.blockId, "", "something was already open");

        mouseDoubleClickSequence(tile, 20, 20);
        compare(canvas.settings.blockId, block, "double-clicking a block did not open its settings");
        compare(canvas.settings.visible, true);

        // **Beside the block, measured off the tile itself** rather than off
        // the same arithmetic that placed the panel.
        const topRight = tile.mapToItem(canvas, tile.width, 0);
        fuzzyCompare(canvas.settings.x, topRight.x + canvas.settingsGap, 1,
                     "the panel is not a gap to the right of its block");
        fuzzyCompare(canvas.settings.y, topRight.y - canvas.settingsMargin, 1,
                     "the panel is not level with the top of its block");
        compare(canvas.settings.caret, "left", "the caret does not point back at the block");
    }

    function test_enterAndTheMenuOpenABlocksSettings() {
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        waitForRendering(canvas);
        const block = canvas.addBlockAt("effect.volume", 200, 200);
        const tile = testCase.tileFor(canvas, block);

        mouseClick(tile, 20, 20);
        keyClick(Qt.Key_Return);
        compare(canvas.settings.blockId, block, "Enter did not open the settings");
        canvas.closeSettings();

        // The context menu's Settings, as a person reaches it: a right
        // click, then the entry.
        mouseClick(tile, 20, 20, Qt.RightButton);
        let entry = null;
        for (let index = 0; index < canvas.tileMenu.count; ++index) {
            const item = canvas.tileMenu.itemAt(index);
            if (item !== null && item.text === KI18n.i18nc("@action:inmenu open a block's settings", "Settings")) {
                entry = item;
            }
        }
        verify(entry !== null, "the menu has no Settings entry");
        verify(entry.enabled, "Settings is greyed out with one block selected");
        entry.triggered();
        compare(canvas.settings.blockId, block, "the menu's Settings did not open the settings");
        canvas.tileMenu.close();
    }

    function test_aDragDoesNotOpenTheSettings() {
        // **The reason a click is its own signal.** Opening on the press
        // would put a panel up every time somebody moved a tile.
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        waitForRendering(canvas);
        const block = canvas.addBlockAt("effect.volume", 200, 200);
        const tile = testCase.tileFor(canvas, block);

        mousePress(tile, 20, 20);
        mouseMove(tile, 20 + 40, 20 + 40);
        verify(canvas.dragging, "the tile is following the pointer");
        mouseRelease(tile, 20 + 40, 20 + 40);

        compare(canvas.settings.blockId, "", "a drag opened the settings");
    }

    function test_theSettingsFlipToTheLeftWhenThereIsNoRoom() {
        const canvas = createTemporaryObject(canvasComponent, null);
        const near = canvas.addBlockAt("effect.volume", 120, 150);
        canvas.openSettings(near);
        compare(canvas.settingsFlipped, false, "there was room on the right and it flipped anyway");
        compare(canvas.settings.caret, "left");

        const far = canvas.addBlockAt("effect.volume", canvas.width - 60, 150);
        canvas.openSettings(far);
        compare(canvas.settingsFlipped, true, "there was no room on the right and it did not flip");
        compare(canvas.settings.caret, "right");
        // On the far side of its block, not over it.
        const block = SessionModel.block(far).position;
        verify(canvas.settings.x + canvas.settings.width <= block.x * canvas.zoom + canvas.originX + 1,
               "the flipped panel overlaps its own block");
    }

    function test_theSettingsAreKeptInsideTheCanvas() {
        const canvas = createTemporaryObject(canvasComponent, null);
        const block = canvas.addBlockAt("effect.volume", 200, 200);
        canvas.openSettings(block);

        // Panned until the block is a long way off each edge in turn: the
        // panel stays where it can be read rather than following it out.
        canvas.originX = -5000;
        canvas.originY = 5000;
        compare(canvas.settings.x, canvas.settingsMargin, "the panel followed its block off the left");
        compare(canvas.settings.y, canvas.height - canvas.settings.height - canvas.settingsMargin,
                "the panel followed its block off the bottom");

        canvas.originX = 5000;
        canvas.originY = -5000;
        compare(canvas.settings.x, canvas.width - canvas.settings.width - canvas.settingsMargin,
                "the panel followed its block off the right");
        compare(canvas.settings.y, canvas.settingsMargin, "the panel followed its block off the top");
    }

    /// How big an item really is on the canvas, through whatever transforms
    /// are between them. **Not its `width`**: a parent's `scale` is a
    /// rendering transform and leaves a child's own width alone, so a panel
    /// put inside the zoom transform by mistake would report exactly the
    /// same size while being drawn at twice it.
    function extentOn(canvas, item) {
        const topLeft = item.mapToItem(canvas, 0, 0);
        const bottomRight = item.mapToItem(canvas, item.width, item.height);
        return Qt.size(bottomRight.x - topLeft.x, bottomRight.y - topLeft.y);
    }

    function test_theSettingsAreNotScaledWithTheCanvas() {
        // **The whole reason it is not inside the transform.** At 200 % its
        // text would blur and its buttons would be the wrong size to hit.
        const canvas = createTemporaryObject(canvasComponent, null);
        const block = canvas.addBlockAt("effect.volume", 150, 150);
        canvas.openSettings(block);
        const before = testCase.extentOn(canvas, canvas.settings);
        const where = canvas.settings.mapToItem(canvas, 0, 0);
        verify(before.width > 0, "the panel has a size to compare");

        canvas.setZoom(2.0, 0, 0);
        compare(canvas.zoom, 2.0);
        const after = testCase.extentOn(canvas, canvas.settings);
        compare(after.width, before.width, "the panel was drawn scaled with the canvas");
        compare(after.height, before.height, "the panel was drawn scaled with the canvas");
        verify(canvas.settings.mapToItem(canvas, 0, 0).x !== where.x,
               "the panel did not follow its block as the canvas zoomed");

        // A tile, on the other hand, is meant to scale — so the comparison
        // above is a real distinction and not a test that cannot fail.
        const tile = testCase.tileFor(canvas, block);
        verify(testCase.extentOn(canvas, tile).width > tile.width + 1,
               "the tiles are not being scaled either, so the panel proves nothing");
    }

    function test_oneBlocksSettingsAtATime() {
        const canvas = createTemporaryObject(canvasComponent, null);
        const first = canvas.addBlockAt("effect.volume", 150, 150);
        const second = canvas.addBlockAt("effect.volume", 400, 150);

        canvas.openSettings(first);
        compare(canvas.settings.blockId, first);
        canvas.openSettings(second);
        compare(canvas.settings.blockId, second, "opening a second panel left the first one open");
    }

    function test_escapeAndAPressOnNothingCloseTheSettings() {
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        waitForRendering(canvas);
        const block = canvas.addBlockAt("effect.volume", 200, 200);
        const tile = testCase.tileFor(canvas, block);

        // Opened the way a person opens it, so that the focus is where a
        // person's would be when they reach for Escape.
        mouseDoubleClickSequence(tile, 20, 20);
        compare(canvas.settings.blockId, block);
        compare(canvas.selectedIds, [block], "double-clicking a block did not select it");

        // **Escape closes the panel before it clears the selection**, so
        // that one press does one thing.
        keyClick(Qt.Key_Escape);
        compare(canvas.settings.blockId, "", "Escape did not close the settings");
        compare(canvas.selectedIds, [block], "Escape cleared the selection as well as closing the panel");

        // And again, so the second Escape is the one that clears it.
        keyClick(Qt.Key_Escape);
        compare(canvas.selectedIds, [], "a second Escape did not clear the selection");

        // Bare canvas: clear of the tile, of the panel, and of the zoom
        // control in the bottom corner.
        canvas.openSettings(block);
        mouseClick(canvas, canvas.width - 40, 40);
        compare(canvas.settings.blockId, "", "a press on empty canvas did not close the settings");
    }

    function test_theSettingsFollowTheirBlockWhileItIsDragged() {
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        waitForRendering(canvas);
        const block = canvas.addBlockAt("effect.volume", 200, 200);
        const tile = testCase.tileFor(canvas, block);
        canvas.openSettings(block);
        const before = canvas.settings.x;

        mousePress(tile, 20, 20);
        mouseMove(tile, 20 + 60, 20);
        verify(canvas.dragging, "the tile is following the pointer");
        fuzzyCompare(canvas.settings.x, before + 60, 1,
                     "the panel stayed behind while its block moved");
        mouseRelease(tile, 20 + 60, 20);
    }

    // --- pulling the settings off their block ---

    /// Every settings panel showing \a blockId that can be seen, docked or
    /// detached. **Found by looking**, not by asking the canvas, so that it
    /// says the same thing whatever the canvas thinks it did.
    function panelsShowing(canvas, blockId) {
        const found = [];
        (function walk(item) {
            for (const child of item.children) {
                if (child.detachButton !== undefined && child.blockId === blockId && child.visible) {
                    found.push(child);
                }
                walk(child);
            }
        })(canvas);
        return found;
    }

    function test_detachingLeavesThePanelWhereItWasAndDropsTheCaret() {
        const canvas = createTemporaryObject(canvasComponent, null);
        const block = canvas.addBlockAt("effect.volume", 200, 200);
        canvas.openSettings(block);
        const docked = Qt.point(canvas.settings.x, canvas.settings.y);
        compare(canvas.settings.detached, false);
        verify(canvas.settings.caret !== "", "a docked panel has no caret");

        canvas.settings.detachButton.clicked();
        const panel = canvas.detachedPanelFor(block);
        verify(panel !== null, "detaching made no detached panel");
        compare(panel.detached, true);
        compare(panel.caret, "", "a detached panel kept its caret");
        compare(canvas.settings.blockId, "", "the docked panel stayed open beside the detached one");
        compare(testCase.panelsShowing(canvas, block).length, 1, "the block's settings are showing twice");

        // **Nudged, not moved across the canvas.** It has not been reopened,
        // so it has to stay where the eye left it.
        verify(Math.abs(panel.x - docked.x) <= Kirigami.Units.gridUnit * 2,
               "detaching threw the panel across the canvas");
        verify(Math.abs(panel.y - docked.y) <= Kirigami.Units.gridUnit * 2,
               "detaching threw the panel across the canvas");
        verify(panel.x !== docked.x || panel.y !== docked.y, "detaching did not visibly move the panel at all");
    }

    function test_aDetachedPanelDoesNotFollowItsBlockAndAttachingPutsItBack() {
        const canvas = createTemporaryObject(canvasComponent, null);
        const block = canvas.addBlockAt("effect.volume", 200, 200);
        canvas.openSettings(block);
        canvas.settings.detachButton.clicked();
        const panel = canvas.detachedPanelFor(block);
        const where = Qt.point(panel.x, panel.y);

        // The block is moved well down the canvas — somewhere the docked
        // panel would still have room on its right, so that what is checked
        // below is the attaching and not the flip.
        SessionModel.moveBlock(block, Qt.point(250, 380));
        wait(50);
        compare(Qt.point(panel.x, panel.y), where, "the detached panel followed its block");

        // Attaching brings it back beside the block, caret and all.
        panel.detachButton.clicked();
        compare(canvas.detachedCount, 0, "attaching left a detached panel behind");
        compare(canvas.settings.blockId, block, "attaching did not dock the settings");
        verify(canvas.settings.caret !== "", "the caret did not come back");
        const tile = testCase.tileFor(canvas, block);
        const topRight = tile.mapToItem(canvas, tile.width, 0);
        fuzzyCompare(canvas.settings.x, topRight.x + canvas.settingsGap, 1,
                     "attaching did not put the panel back beside its block");
    }

    function test_draggingTheHeaderMovesADetachedPanelAndKeepsItInside() {
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        waitForRendering(canvas);
        const block = canvas.addBlockAt("effect.volume", 200, 200);
        canvas.openSettings(block);
        canvas.settings.detachButton.clicked();
        const panel = canvas.detachedPanelFor(block);
        waitForRendering(canvas);
        const before = Qt.point(panel.x, panel.y);

        // **The pointer, not the function behind it.** What a person does is
        // press on the header, move, and let go.
        const grab = panel.headerGrab;
        mousePress(grab, 30, 8);
        mouseMove(grab, 30 + 40, 8 + 30);
        fuzzyCompare(panel.x, before.x + 40, 1, "dragging the header did not move the panel");
        fuzzyCompare(panel.y, before.y + 30, 1, "dragging the header did not move the panel");
        mouseRelease(grab, 30 + 40, 8 + 30);

        // **It cannot be dragged out of sight.** Far past every edge in turn.
        const index = canvas.detachedIndexOf(block);
        canvas.dragDetachedBy(index, -5000, -5000, panel.width, panel.height);
        compare(panel.x, canvas.settingsMargin, "it was dragged off the left");
        compare(panel.y, canvas.settingsMargin, "it was dragged off the top");

        canvas.dragDetachedBy(index, 5000, 5000, panel.width, panel.height);
        compare(panel.x, canvas.width - panel.width - canvas.settingsMargin, "it was dragged off the right");
        compare(panel.y, canvas.height - panel.height - canvas.settingsMargin, "it was dragged off the bottom");
    }

    function test_theHeaderOnlyDragsWhileDetached() {
        // Docked, the panel belongs to its block and the header is not a
        // handle: dragging it would promise something that cannot happen.
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        waitForRendering(canvas);
        const block = canvas.addBlockAt("effect.volume", 200, 200);
        canvas.openSettings(block);
        const docked = Qt.point(canvas.settings.x, canvas.settings.y);

        // **With the pointer**, not by asking the handle: the header takes
        // presses in both states, and only one of them moves it.
        const grab = canvas.settings.headerGrab;
        verify(grab !== null, "there is a header to drag");
        waitForRendering(canvas);
        mousePress(grab, 10, grab.height / 2);
        mouseMove(grab, 70, grab.height / 2 + 40);
        mouseRelease(grab, 70, grab.height / 2 + 40);
        compare(Qt.point(canvas.settings.x, canvas.settings.y), docked, "a docked panel moved with its header");
        compare(canvas.settings.blockId, block, "dragging the header closed the settings");

        canvas.settings.detachButton.clicked();
        const panel = canvas.detachedPanelFor(block);
        waitForRendering(canvas);
        const detached = Qt.point(panel.x, panel.y);
        mousePress(panel.headerGrab, 10, panel.headerGrab.height / 2);
        mouseMove(panel.headerGrab, 40, panel.headerGrab.height / 2 + 20);
        mouseMove(panel.headerGrab, 70, panel.headerGrab.height / 2 + 40);
        mouseRelease(panel.headerGrab, 70, panel.headerGrab.height / 2 + 40);
        verify(panel.x !== detached.x || panel.y !== detached.y, "a detached panel did not move with its header");
    }

    function test_aDetachedPanelStaysOpenAndInPlaceWhateverElseIsClicked() {
        // Another block's settings took the detached panel over, and a
        // click on the canvas closed it.
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        waitForRendering(canvas);
        const first = canvas.addBlockAt("effect.volume", 100, 100);
        const second = canvas.addBlockAt("effect.volume", 100, 350);
        mouseDoubleClickSequence(testCase.tileFor(canvas, first), 20, 12);
        canvas.settings.detachButton.clicked();
        waitForRendering(canvas);
        const out = testCase.panelsShowing(canvas, first);
        compare(out.length, 1, "detaching did not leave the first block's settings showing");
        const where = Qt.point(out[0].x, out[0].y);

        // Everything a person does to the rest of the canvas.
        const other = testCase.tileFor(canvas, second);
        mouseClick(other, 20, 12);
        mouseDoubleClickSequence(other, 20, 12);
        compare(testCase.panelsShowing(canvas, second).length, 1, "the second block's settings did not open");
        mouseClick(canvas, canvas.width - 20, canvas.height - 20);
        keyClick(Qt.Key_Escape);
        waitForRendering(canvas);

        const still = testCase.panelsShowing(canvas, first);
        compare(still.length, 1, "the detached panel closed or was taken over");
        compare(still[0].detached, true, "the detached panel was docked again");
        compare(Qt.point(still[0].x, still[0].y), where, "the detached panel moved");

        // Asking for its own block's settings brings it forward where it is.
        mouseDoubleClickSequence(testCase.tileFor(canvas, first), 20, 12);
        const again = testCase.panelsShowing(canvas, first);
        compare(again.length, 1, "the block's settings came up a second time");
        compare(again[0].detached, true, "double-clicking its block docked it");
        compare(Qt.point(again[0].x, again[0].y), where, "double-clicking its block moved it");
    }

    function test_severalPanelsCanBeDetachedAtOnce() {
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        waitForRendering(canvas);
        const blocks = [canvas.addBlockAt("effect.volume", 100, 100),
                        canvas.addBlockAt("meter.peakrms", 100, 300),
                        canvas.addBlockAt("effect.volume", 100, 480)];
        for (const block of blocks) {
            mouseDoubleClickSequence(testCase.tileFor(canvas, block), 20, 12);
            canvas.settings.detachButton.clicked();
        }
        for (const block of blocks) {
            const showing = testCase.panelsShowing(canvas, block);
            compare(showing.length, 1, "block " + block + " has no settings showing");
            compare(showing[0].detached, true);
        }
        // Closing one closes that one.
        canvas.detachedPanelFor(blocks[1]).closeButton.clicked();
        compare(testCase.panelsShowing(canvas, blocks[1]).length, 0, "close did not close the detached panel");
        compare(testCase.panelsShowing(canvas, blocks[0]).length, 1, "closing one closed another");
    }

    function test_detachedPanelsComeBackWithTheirSession() {
        const home = testCase.sessionId;
        let canvas = createTemporaryObject(canvasComponent, null);
        const block = canvas.addBlockAt("effect.volume", 200, 200);
        canvas.openSettings(block);
        canvas.settings.detachButton.clicked();
        canvas.dragDetachedBy(canvas.detachedIndexOf(block), 90, 60, 280, 200);
        canvas.detachedPanelFor(block).dragFinished();
        const panel = testCase.panelsShowing(canvas, block)[0];
        const where = Qt.point(panel.x, panel.y);

        // Another tab in front, and back again.
        TestEngine.createSession("Elsewhere");
        const elsewhere = testCase.sessionId;
        testCase.sessionId = home;
        SessionModel.sessionId = elsewhere;
        compare(testCase.panelsShowing(canvas, block).length, 0, "another session showed this one's panel");
        SessionModel.sessionId = home;
        let back = testCase.panelsShowing(canvas, block);
        compare(back.length, 1, "the detached panel did not come back with its session");
        compare(Qt.point(back[0].x, back[0].y), where, "the detached panel came back somewhere else");

        // And after the application is closed and opened again: a new
        // canvas, with the session already showing.
        canvas = createTemporaryObject(canvasComponent, null);
        back = testCase.panelsShowing(canvas, block);
        compare(back.length, 1, "the detached panel did not come back after a restart");
        compare(Qt.point(back[0].x, back[0].y), where, "the detached panel came back somewhere else after a restart");
    }

    function test_theHeaderWorksTheSameDetachedAsDocked() {
        const canvas = createTemporaryObject(canvasComponent, null);
        const block = canvas.addBlockAt("effect.volume", 200, 200);
        canvas.openSettings(block);
        canvas.settings.detachButton.clicked();
        const panel = canvas.detachedPanelFor(block);

        // The on/off switch, the rename and close all still reach the
        // engine with the panel pulled off its block.
        SessionModel.setBlockEnabled(block, false);
        wait(50);
        compare(panel.enabledSwitch.checked, false, "the switch stopped following the block");

        SessionModel.renameBlock(block, "Voice");
        wait(50);
        compare(panel.displayName, "Voice", "the name stopped following the block");

        panel.closeButton.clicked();
        compare(canvas.detachedCount, 0, "close did nothing while detached");
    }

    function test_aClickOnTheSettingsHeaderLeavesThemOpen() {
        // The header of a docked panel took no press of its own, so
        // a click on it fell through to the canvas, and a click on the
        // canvas closes the settings.
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        waitForRendering(canvas);
        const block = canvas.addBlockAt("effect.volume", 150, 150);
        canvas.openSettings(block);
        waitForRendering(canvas);
        let settings = canvas.settings;

        for (const detached of [false, true]) {
            if (detached) {
                settings.detachButton.clicked();
                settings = canvas.detachedPanelFor(block);
                waitForRendering(canvas);
            }
            mouseClick(settings.nameLabel, settings.nameLabel.width / 2, settings.nameLabel.height / 2);
            compare(settings.blockId, block, "a click on the header closed the settings, detached " + detached);
            // Nor anywhere else on the card that is not a control: the gap
            // between the header's name and its switch, the body's margin.
            mouseClick(settings, settings.width / 2, settings.height - 2);
            compare(settings.blockId, block, "a click on the card's edge closed the settings, detached " + detached);
        }
    }

    function test_aDoubleClickOnTheNameInTheSettingsRenamesTheBlock() {
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        waitForRendering(canvas);
        const block = canvas.addBlockAt("effect.volume", 150, 150);
        canvas.openSettings(block);
        waitForRendering(canvas);
        const settings = canvas.settings;

        // **No pencil**: the name itself is what is renamed.
        const header = [];
        (function walk(item) {
            for (const child of item.children) {
                if (child.icon !== undefined && child.icon.name === "edit-rename" && child.visible) {
                    header.push(child);
                }
                walk(child);
            }
        })(settings);
        compare(header.length, 0, "the header still has a rename button");

        mouseDoubleClickSequence(settings.nameLabel, settings.nameLabel.width / 2, settings.nameLabel.height / 2);
        verify(settings.nameField.visible, "a double-click on the name did not start renaming");
        compare(settings.blockId, block, "the double-click closed the settings");
        settings.nameField.text = "Voice";
        keyClick(Qt.Key_Return);
        compare(SessionModel.block(block).nickname, "Voice");
        verify(!settings.nameField.visible, "the field stayed open after Enter");

        // Escape leaves the name as it was.
        mouseDoubleClickSequence(settings.nameLabel, settings.nameLabel.width / 2, settings.nameLabel.height / 2);
        verify(settings.nameField.visible);
        settings.nameField.text = "Something else";
        keyClick(Qt.Key_Escape);
        compare(SessionModel.block(block).nickname, "Voice", "Escape renamed the block");

        // And detached, where the header is also what drags the panel.
        settings.detachButton.clicked();
        const floating = canvas.detachedPanelFor(block);
        waitForRendering(canvas);
        mouseDoubleClickSequence(floating.nameLabel, floating.nameLabel.width / 2, floating.nameLabel.height / 2);
        verify(floating.nameField.visible, "a double-click on a detached panel's name did not start renaming");
        keyClick(Qt.Key_Escape);
    }

    function test_severalSelectedTilesMoveTogether() {
        const canvas = createTemporaryObject(canvasComponent, null);
        const first = canvas.addBlockAt("effect.volume", 150, 150);
        const second = canvas.addBlockAt("effect.volume", 400, 150);
        const before = [SessionModel.block(first).position, SessionModel.block(second).position];
        canvas.pressOn(first, Qt.NoModifier);
        canvas.pressOn(second, Qt.ControlModifier);

        canvas.dragBy(64, -32);
        canvas.endDrag();

        compare(SessionModel.block(first).position.x, before[0].x + 64);
        compare(SessionModel.block(first).position.y, before[0].y - 32);
        compare(SessionModel.block(second).position.x, before[1].x + 64);
        compare(SessionModel.block(second).position.y, before[1].y - 32);
    }

    function test_escapePutsADragBackWhereItStarted() {
        const canvas = createTemporaryObject(canvasComponent, null);
        const block = canvas.addBlockAt("effect.volume", 200, 200);
        const before = SessionModel.block(block).position;

        canvas.dragBy(80, 80);
        compare(canvas.dragging, true);
        canvas.cancelDrag();

        compare(canvas.dragging, false);
        compare(SessionModel.block(block).position, before);
    }

    function test_renamingATileReachesTheEngine() {
        const canvas = createTemporaryObject(canvasComponent, null);
        const block = canvas.addBlockAt("effect.volume", 200, 200);
        const tile = testCase.tileFor(canvas, block);

        // Before anybody types, the tile shows the block's own name — the
        // interface's word, never the engine's id.
        compare(tile.displayName, BlockPresentation.name("effect.volume"));

        canvas.startRenaming();
        compare(canvas.renamingId, block);
        verify(tile.nameField.visible, "there is a field to type in");

        tile.nameField.text = "Podcast level";
        tile.nameField.editingFinished();

        compare(SessionModel.block(block).nickname, "Podcast level");
        compare(tile.displayName, "Podcast level");
        compare(canvas.renamingId, "");
    }

    function test_removingTakesTheSelectionOffTheCanvas() {
        const canvas = createTemporaryObject(canvasComponent, null);
        const first = canvas.addBlockAt("effect.volume", 100, 100);
        const second = canvas.addBlockAt("meter.peakrms", 300, 100);
        canvas.pressOn(first, Qt.NoModifier);
        canvas.pressOn(second, Qt.ControlModifier);

        canvas.removeSelected();

        compare(SessionModel.blockIds.length, 0);
        compare(canvas.blocks.count, 0);
        compare(canvas.selectedIds.length, 0);
    }

    function test_whatWasBuiltIsWhatComesBack() {
        // **Saved and read again.** What a tile is called and where it sits
        // are the session's, not the canvas's, so they have to survive the
        // canvas being told nothing and asking the engine again.
        const canvas = createTemporaryObject(canvasComponent, null);
        const block = canvas.addBlockAt("effect.volume", 240, 160);
        SessionModel.renameBlock(block, "Podcast level");
        SessionModel.setBlockEnabled(block, false);
        const placed = SessionModel.block(block).position;

        TestEngine.saveSession(testCase.sessionId);
        SessionModel.sessionId = "";
        compare(SessionModel.blockIds.length, 0);
        SessionModel.sessionId = testCase.sessionId;

        compare(SessionModel.blockIds, [block]);
        compare(SessionModel.block(block).nickname, "Podcast level");
        compare(SessionModel.block(block).enabled, false);
        compare(SessionModel.block(block).position, placed);

        const tile = testCase.tileFor(canvas, block);
        verify(tile !== null, "the tile came back with it");
        compare(tile.blockEnabled, false);
    }

    function test_aTileSaysWhatItIsAndWhetherItIsRunning() {
        // What a screen reader has to be able to read off a tile: which
        // block it is, and which of off, on and live it is in.
        const canvas = createTemporaryObject(canvasComponent, null);
        const block = canvas.addBlockAt("effect.volume", 200, 200);
        const tile = testCase.tileFor(canvas, block);

        compare(tile.Accessible.role, Accessible.Button);
        verify(tile.Accessible.name.indexOf(BlockPresentation.name("effect.volume")) >= 0,
               "the tile is named after the block");
        verify(tile.Accessible.name.indexOf(tile.stateText) >= 0, "and says what it is doing");
        verify(tile.stateText.length > 0, "there is a word for the state");

        SessionModel.setBlockEnabled(block, false);
        const off = tile.stateText;
        SessionModel.setBlockEnabled(block, true);
        verify(off !== tile.stateText, "a bypassed block does not read like a running one");
    }

    function test_aTilesStateDotLightsWhileTheBlockHasSignal() {
        // **What the canvas has to show, checked rather than reasoned
        // about.** The dot goes by the telemetry the engine publishes for
        // the block, which nothing offscreen could put there until a test
        // could drive it; the mock now publishes made-up levels for a
        // running session, so this drives the whole path rather than the
        // property at the end of it.
        const canvas = createTemporaryObject(canvasComponent, null);
        const block = canvas.addBlockAt("effect.volume", 200, 200);
        const tile = testCase.tileFor(canvas, block);
        Meters.watch(testCase.sessionId);

        compare(tile.live, false, "a session that is not running has no signal in it");
        compare(tile.stateDot.color, AppColors.okMark);

        TestEngine.start(testCase.sessionId);
        tryVerify(() => Meters.live, 1000, "a running session published nothing");
        compare(tile.live, false, "a running session with nothing playing had a lit dot");

        TestEngine.setFakeLevel(testCase.sessionId, block, 0.5);
        tryVerify(() => tile.live, 100, "the dot did not light within 100 ms of the signal");
        compare(tile.stateDot.color, AppColors.liveMark);
        const lit = tile.stateText;

        // **A bypassed block is a third state, not a quiet one**, and it
        // beats the signal: the dot says what the block is doing.
        SessionModel.setBlockEnabled(block, false);
        tryVerify(() => tile.stateDot.color === AppColors.muted, 100,
                  "a bypassed block's dot was still coloured by its signal");
        SessionModel.setBlockEnabled(block, true);

        TestEngine.clearFakeLevels(testCase.sessionId);
        tryVerify(() => !tile.live, 100, "the dot stayed lit after the signal stopped");
        verify(lit !== tile.stateText, "a block with signal does not read like a quiet one");

        TestEngine.stop(testCase.sessionId);
        Meters.watch("");
    }

    function test_aBlockThisBuildHasNeverHeardOfStillGetsATile() {
        // A session written by a later version. **Flagged, never dropped**:
        // opening and saving one must not lose anything, and a tile that
        // could not be drawn would be a block nobody can find again.
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock",
                                              block: { id: "bx", type: "effect.fromthefuture" },
                                              index: -1 }));
        const canvas = createTemporaryObject(canvasComponent, null);
        const tile = testCase.tileFor(canvas, "bx");

        verify(tile !== null, "there is a tile for it");
        // An unknown type is its own name: something a person can search
        // for and tell us about, which an empty tile is not.
        verify(tile.displayName.length > 0, "and it is called something");
        // And it still gets a face, because the catalogue has nothing to
        // say about a type it does not have and the tile has to draw
        // anyway.
        verify(tile.face.item !== null, "and something on it");
        compare(tile.faceKind, "icon");
    }

    function test_zoomStaysBetweenHalfAndDouble() {
        const canvas = createTemporaryObject(canvasComponent, null);
        compare(canvas.zoom, 1);

        for (let i = 0; i < 12; ++i) {
            canvas.zoomIn();
        }
        compare(canvas.zoom, canvas.maximumZoom);

        for (let i = 0; i < 24; ++i) {
            canvas.zoomOut();
        }
        compare(canvas.zoom, canvas.minimumZoom);

        canvas.actualSize();
        compare(canvas.zoom, 1);
        compare(canvas.originX, 0);
        compare(canvas.originY, 0);
    }

    function test_zoomingKeepsWhatIsUnderThePointerUnderIt() {
        const canvas = createTemporaryObject(canvasComponent, null);
        const before = canvas.toSession(300, 200);

        canvas.setZoom(1.75, 300, 200);

        const after = canvas.toSession(300, 200);
        verify(Math.abs(after.x - before.x) < 0.001, "the same place in the session is still there");
        verify(Math.abs(after.y - before.y) < 0.001, "and at the same height");
        compare(canvas.zoom, 1.75);
    }

    function test_fitPutsTheWholeSessionOnTheScreen() {
        const canvas = createTemporaryObject(canvasComponent, null);
        canvas.addBlockAt("effect.volume", 100, 100);
        canvas.addBlockAt("effect.volume", 700, 500);
        // Somewhere else entirely, so that fitting has work to do.
        canvas.setZoom(2, 0, 0);
        canvas.originX = -900;

        canvas.fitToView();

        const bounds = canvas.contentBounds();
        const left = bounds.x * canvas.zoom + canvas.originX;
        const top = bounds.y * canvas.zoom + canvas.originY;
        verify(left >= 0 && top >= 0, "nothing is off the top or the left");
        verify(left + bounds.width * canvas.zoom <= canvas.width, "nor off the right");
        verify(top + bounds.height * canvas.zoom <= canvas.height, "nor off the bottom");
        verify(canvas.zoom >= canvas.minimumZoom && canvas.zoom <= canvas.maximumZoom,
               "and it stayed inside the zoom range");
    }

    function test_anEmptySessionFitsToActualSize() {
        const canvas = createTemporaryObject(canvasComponent, null);
        canvas.setZoom(2, 0, 0);

        canvas.fitToView();

        compare(canvas.zoom, 1);
        compare(canvas.originX, 0);
    }

    function test_theWindowsZoomActionsDriveTheCanvas() {
        // The menu item and the canvas are one thing: the View menu is how
        // most people will find the zoom at all.
        const window = createTemporaryObject(mainWindow, testCase);
        verify(window.sessionCanvas, "the canvas is in the window");
        const view = window.menus.menuAt(2);
        compare(view.title, "&View");

        view.actionAt(0).trigger();
        verify(window.sessionCanvas.zoom > 1, "Zoom In zoomed in");
        view.actionAt(2).trigger();
        compare(window.sessionCanvas.zoom, 1);
    }

    // --- the connection mode ---

    function test_theCanvasSaysWhichConnectionModeItIsIn() {
        const canvas = createTemporaryObject(canvasComponent, null);
        const mode = canvas.connectionMode;

        SessionModel.setAutoConnect(true);
        compare(mode.text, "Automatic connections");
        compare(mode.automatic, true);

        // **Changed from somewhere else** — the menu item, the Info tab —
        // and the canvas follows, because it is the same setting.
        SessionModel.setAutoConnect(false);
        compare(mode.text, "Manual connections");
        compare(mode.automatic, false);

        SessionModel.setAutoConnect(true);
    }

    function test_pressingTheConnectionModeChangesIt() {
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        waitForRendering(canvas);
        const block = canvas.addBlockAt("effect.volume", 300, 300);
        SessionModel.setAutoConnect(true);
        compare(canvas.selectedIds, [block]);

        mouseClick(canvas.connectionMode);
        compare(SessionModel.autoConnect, false, "pressing it did not switch to manual connections");
        compare(canvas.connectionMode.text, "Manual connections");
        // **A press on it is not a press on the canvas**: it does not take
        // the selection away from what was about to be worked on.
        compare(canvas.selectedIds, [block], "pressing the mode cleared the selection");

        mouseClick(canvas.connectionMode);
        compare(SessionModel.autoConnect, true, "pressing it again did not switch back");
    }

    function test_theConnectionModeStaysPutWhileTheCanvasIsZoomedAndPanned() {
        const canvas = createTemporaryObject(canvasComponent, null);
        const mode = canvas.connectionMode;
        const at = mode.mapToItem(canvas, 0, 0);
        const size = testCase.extentOn(canvas, mode);

        // At the top left, clear of the edge.
        verify(at.x > 0 && at.x < Kirigami.Units.gridUnit * 2, "it is not at the left: " + at.x);
        verify(at.y > 0 && at.y < Kirigami.Units.gridUnit * 2, "it is not at the top: " + at.y);

        canvas.zoom = 2;
        canvas.originX += 150;
        canvas.originY -= 90;

        const movedTo = mode.mapToItem(canvas, 0, 0);
        const resized = testCase.extentOn(canvas, mode);
        compare(movedTo.x, at.x, "it moved with the pan");
        compare(movedTo.y, at.y, "it moved with the pan");
        compare(resized.width, size.width, "it was scaled with the zoom");
        compare(resized.height, size.height, "it was scaled with the zoom");
    }

    // --- the connectors ---

    function test_aConnectorIsARing() {
        const canvas = createTemporaryObject(canvasComponent, null);
        const block = canvas.addBlockAt("effect.volume", 200, 200);
        const tile = testCase.tileFor(canvas, block);
        for (const port of [tile.inputPorts.itemAt(0), tile.outputPorts.itemAt(0)]) {
            // Hollow: the canvas inside, the colour on a border of its own.
            compare(Qt.colorEqual(port.ring.color, AppColors.canvas), true, "the connector is not hollow");
            verify(port.ring.border.width >= 2, "the ring's border is too thin to carry a colour");
            verify(port.ring.width >= Kirigami.Units.gridUnit / 2 - 1, "the ring is " + port.ring.width + " px");
            compare(port.ring.radius, port.ring.width / 2);

            // **The merge marker stays clear of it**, with a gap between
            // the two rather than a thicker ring.
            port.merging = true;
            verify(port.mergeRing.visible, "the merge marker went");
            verify(port.mergeRing.width / 2 - port.ring.width / 2 >= port.ring.border.width + 1,
                   "the merge marker runs into the ring");
        }
    }

    function test_aConnectorShowsWhatItsTilesStateDotShows() {
        // **The connector and the dot above it read the same value**, so
        // this drives the telemetry and checks the two together through
        // every state: quiet, live, off while live, and quiet again.
        const canvas = createTemporaryObject(canvasComponent, null);
        const block = canvas.addBlockAt("effect.volume", 200, 200);
        const tile = testCase.tileFor(canvas, block);
        const ports = [tile.inputPorts.itemAt(0), tile.outputPorts.itemAt(0)];
        const agree = (colour, what) => {
            for (const port of ports) {
                if (!Qt.colorEqual(port.ring.border.color, colour)) {
                    return false;
                }
            }
            return true;
        };
        Meters.watch(testCase.sessionId);

        verify(agree(AppColors.wire), "a quiet block's connectors are not the wire colour");

        TestEngine.start(testCase.sessionId);
        tryVerify(() => Meters.live, 1000, "a running session published nothing");
        TestEngine.setFakeLevel(testCase.sessionId, block, 0.5);
        tryVerify(() => tile.live, 100, "no signal reached the tile");
        compare(tile.stateDot.color, AppColors.liveMark);
        verify(agree(AppColors.live), "a block passing audio has connectors that say it is not");

        // Off beats the signal, for the dot and so for the connectors.
        SessionModel.setBlockEnabled(block, false);
        tryVerify(() => Qt.colorEqual(tile.stateDot.color, AppColors.muted), 100, "the dot did not go off");
        verify(agree(AppColors.wire), "a bypassed block's connectors still say live");
        SessionModel.setBlockEnabled(block, true);
        verify(agree(AppColors.live), "switched back on, the connectors did not light again");

        // **During a Ctrl-drag, where the wire may land beats what is
        // playing.**
        ports[0].candidate = true;
        compare(Qt.colorEqual(ports[0].ring.border.color, AppColors.accent), true,
                "a candidate connector on a live block is not the accent colour");
        tryVerify(() => ports[0].ring.scale > 1, 1000, "a candidate connector does not grow");
        ports[0].aimedAt = true;
        compare(Qt.colorEqual(ports[0].ring.color, AppColors.accentMark), true,
                "the connector the wire is aimed at is not filled");
        ports[0].aimedAt = false;
        ports[0].candidate = false;

        TestEngine.clearFakeLevels(testCase.sessionId);
        tryVerify(() => !tile.live, 100, "the signal did not stop");
        verify(agree(AppColors.wire), "the connectors stayed lit after the signal stopped");

        TestEngine.stop(testCase.sessionId);
        Meters.watch("");
    }

    // --- a tile's two parts ---

    /// Every line of writing drawn inside \a item, however deep.
    function linesIn(item) {
        let found = [];
        for (const child of item.children) {
            if (!child.visible) {
                continue;
            }
            if (child.truncated !== undefined && typeof child.text === "string" && child.text !== "") {
                found.push(child);
            }
            found = found.concat(testCase.linesIn(child));
        }
        return found;
    }

    function test_aTileHasAHeaderARuleAndAFaceTheWidthOfTheTile() {
        const canvas = createTemporaryObject(canvasComponent, null);
        const block = canvas.addBlockAt("effect.volume", 200, 200);
        const tile = testCase.tileFor(canvas, block);
        const border = 1;

        verify(tile.headerRule.visible, "there is no rule under the header");
        compare(tile.headerRule.height, 1);
        verify(tile.headerRule.width >= tile.width - 2 * border - 0.5,
               "the rule does not run the width of the tile: " + tile.headerRule.width);
        compare(tile.header.height, Math.round(Kirigami.Units.gridUnit * 4 / 3));

        const rule = tile.headerRule.mapToItem(tile, 0, 0);
        const face = tile.face.mapToItem(tile, 0, 0);
        verify(face.y >= rule.y + 1, "the face starts above the rule");
        verify(tile.face.width >= tile.width - 2 * border - 0.5,
               "the face has " + tile.face.width + " of the tile's " + tile.width + " pixels");
    }

    function test_anIconFaceSaysOneThing_data() {
        return [
            { tag: "application", type: "source.application" },
            { tag: "system audio", type: "source.system" },
            { tag: "input device", type: "source.device" },
            { tag: "output device", type: "output.device" }
        ];
    }

    function test_anIconFaceSaysOneThing(data) {
        // **One line, not two cut off halfway.** An output device read
        // "Default ou…" over "PCM2902 Aud…" at the size a tile is drawn.
        const canvas = createTemporaryObject(canvasComponent, null);
        const block = canvas.addBlockAt(data.type, 200, 200);
        const tile = testCase.tileFor(canvas, block);
        tryVerify(() => tile.face.item !== null, 1000, "the face was not drawn");

        const lines = testCase.linesIn(tile.face.item);
        compare(lines.length, 1, data.tag + " face shows " + lines.map(line => "\"" + line.text + "\"").join(", "));
        compare(lines[0].text, tile.face.item.line);

        // **What it left off is still there**, in the tooltip.
        verify(tile.face.detail.length > 0, "there is nothing to hover for");
        verify(tile.face.detail.indexOf(tile.face.item.line) >= 0 || tile.face.item.line === "",
               "the tooltip \"" + tile.face.detail + "\" does not include the line \"" + tile.face.item.line + "\"");
    }

    // --- "Presets" once ---

    /// Every piece of writing in \a item reading \a words, however deep.
    function writingOf(item, words) {
        let found = [];
        for (const child of item.children) {
            if (!child.visible) {
                continue;
            }
            if (child.text === words && child.font !== undefined) {
                found.push(child);
            }
            found = found.concat(testCase.writingOf(child, words));
        }
        return found;
    }

    function test_everyBlocksSettingsStayInsideTheirCard() {
        // A footer that asked for a fixed width made the card's column
        // wider than the card, and every panel's controls ran up to — and
        // past — its right edge, the margin the design gives them gone.
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        waitForRendering(canvas);
        const margin = Kirigami.Units.largeSpacing;
        for (const type of BlockCatalogue.types) {
            const block = canvas.addBlockAt(type, 200, 200);
            canvas.openSettings(block);
            tryVerify(() => canvas.settings.body.item !== null, 1000, "no panel body for " + type);
            waitForRendering(canvas);
            let widest = 0;
            let what = "";
            const measure = () => {
                widest = 0;
                (function walk(item) {
                    for (const child of item.children) {
                        if (!child.visible || child.width <= 0) {
                            continue;
                        }
                        // **What a person sees**: text and controls, the
                        // things with a font. A layout's own helpers are bare
                        // items.
                        const right = child.font === undefined ? 0 : child.mapToItem(canvas.settings, child.width, 0).x;
                        if (right > widest) {
                            widest = right;
                            what = String(child);
                        }
                        walk(child);
                    }
                })(canvas.settings.body.item);
                return widest <= canvas.settings.width - margin + 1;
            };
            // **Settled, not at the first frame**: Kirigami's form lays
            // itself out again a turn of the loop later, and until it has
            // its rows sit wherever they were first put.
            for (let turn = 0; turn < 20 && !measure(); ++turn) {
                wait(50);
            }
            verify(measure(), type + "'s settings reach " + Math.round(widest) + " of a card "
                   + canvas.settings.width + " wide (" + what + ")");
            canvas.closeSettings();
            SessionModel.removeBlocks([block]);
        }
    }

    /// Opens every folded section under \a item, so a check sees what a
    /// person sees once they have opened it.
    function openEverySection(item) {
        for (const child of item.children) {
            if (child.expanded !== undefined && child.toggle !== undefined && child.title !== undefined) {
                child.expanded = true;
            }
            testCase.openEverySection(child);
        }
    }

    function test_everyBlocksPanelFitsTheWindow_data() {
        return [
            { tag: "a 1400 px window", width: 1400, height: 900 },
            // The window's own minimum, read from it below.
            { tag: "the smallest window", width: 0, height: 0 }
        ];
    }

    /// The choices under \a item a person picks one of to see another part
    /// of a panel (a tab, a view, a band), visible now, in the order they
    /// are drawn.
    function choicesOf(item) {
        let found = [];
        for (const child of item.children) {
            if (!child.visible) {
                continue;
            }
            if (child instanceof SegmentedChoice) {
                found.push(child);
            }
            found = found.concat(testCase.choicesOf(child));
        }
        return found;
    }

    function test_everyBlocksPanelFitsTheWindow(data) {
        // With its audiogram open, Hearing Compensation 16's panel
        // asked for 609 pixels in a card of 460, and everything in it ran
        // under the card's right edge, in a window of any size — the check
        // above opens no folded section, and never saw it. In the smallest
        // window every card was wider than the canvas and half outside it.
        //
        // **In the whole window, with every section open and on every
        // tab**: the card inside the window, and nothing a person reads or
        // presses past the card's edge, less its margin. The Recorder's
        // Limits tab once ran a third of the card past its edge, and pulled
        // the status line and the buttons above it out with it, while a
        // check that looked only at the tab a panel opens on saw General,
        // which fitted.
        const window = createTemporaryObject(mainWindow, testCase);
        window.width = data.width > 0 ? data.width : window.minimumWidth;
        window.height = data.height > 0 ? data.height : window.minimumHeight;
        const canvas = window.sessionCanvas;
        waitForRendering(canvas);
        const margin = Kirigami.Units.largeSpacing;
        const faults = [];
        for (const type of BlockCatalogue.types) {
            const block = canvas.addBlockAt(type, 60, 60);
            canvas.openSettings(block);
            tryVerify(() => canvas.settings.body.item !== null, 1000, "no panel body for " + type);
            testCase.openEverySection(canvas.settings.body.item);
            const card = canvas.settings;
            let widest = 0;
            let what = "";
            const fits = () => {
                widest = 0;
                (function walk(item) {
                    for (const child of item.children) {
                        if (!child.visible || child.width <= 0) {
                            continue;
                        }
                        const right = child.font === undefined ? 0 : child.mapToItem(card, child.width, 0).x;
                        if (right > widest) {
                            widest = right;
                            what = String(child) + (child.text !== undefined ? " \"" + child.text + "\"" : "");
                        }
                        walk(child);
                    }
                })(card.body.item);
                return widest <= card.width - margin + 1;
            };
            // Settled, not at the first frame (see above).
            const settle = () => {
                for (let turn = 0; turn < 20 && !fits(); ++turn) {
                    wait(50);
                }
            };
            settle();
            const place = card.mapToItem(null, 0, 0);
            if (place.x < -0.5 || place.x + card.width > window.width + 0.5) {
                faults.push(type + ": the card is at " + Math.round(place.x) + "–" + Math.round(place.x + card.width)
                            + " in a window " + window.width + " wide");
            } else if (!fits()) {
                faults.push(type + ": reaches " + Math.round(widest) + " of a card " + card.width + " wide (" + what + ")");
            } else {
                // **Every tab, and every part a choice shows**: each choice
                // picked in turn, then any a part it showed has, until there
                // are none left that have not been picked.
                const picked = [];
                for (let next = 0; next < 64; ++next) {
                    const choice = testCase.choicesOf(card.body.item).find(candidate => picked.indexOf(candidate) < 0);
                    if (choice === undefined) {
                        break;
                    }
                    picked.push(choice);
                    if (!choice.enabled) {
                        continue;
                    }
                    choice.clicked();
                    testCase.openEverySection(card.body.item);
                    settle();
                    if (!fits()) {
                        faults.push(type + ", on \"" + choice.text + "\": reaches " + Math.round(widest) + " of a card "
                                    + card.width + " wide (" + what + ")");
                        break;
                    }
                }
            }
            canvas.closeSettings();
            SessionModel.removeBlocks([block]);
        }
        compare(faults.join("\n"), "", "in " + data.tag + " (" + window.width + " × " + window.height + ")");
    }

    function test_everyBlocksPanelSaysPresetsOnce() {
        // **Every block the catalogue has**, not the two the report named:
        // the placeholder was scaffolding from before the shared footer, and
        // wherever it was left the footer now sits right under it. In a
        // window, because an item with no window is never visible and the
        // count would be of nothing.
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        waitForRendering(canvas);
        for (const type of BlockCatalogue.types) {
            const block = canvas.addBlockAt(type, 200, 200);
            verify(block !== "", "the engine did not add a " + type);
            canvas.openSettings(block);
            tryVerify(() => canvas.settings.body.item !== null, 1000, "no panel body for " + type);
            // The footer's label, as the designs write it, and the
            // bare word, which is what a leftover placeholder would say.
            const presets = testCase.writingOf(canvas.settings, "Presets:")
                .concat(testCase.writingOf(canvas.settings, "Presets"));
            // Once, or not at all where the block's own settings say they
            // have no use for presets.
            const wanted = canvas.settings.bodyAsks("wantsPresets", true) ? 1 : 0;
            compare(presets.length, wanted, type + "'s panel says \"Presets\" " + presets.length + " times");
            canvas.closeSettings();
            SessionModel.removeBlocks([block]);
        }
    }

    // --- a block the canvas placed is brought into view ---

    function test_aBlockAddedPastTheEdgeIsBroughtIntoView() {
        const canvas = createTemporaryObject(canvasComponent, null);
        const margin = Kirigami.Units.gridUnit;
        // A lane, one click at a time, until it runs past the right edge.
        let added = "";
        for (let count = 0; count < 8; ++count) {
            added = canvas.addBlockAfterSelection("effect.volume");
            verify(added !== "", "the engine did not add a block");
            const shown = canvas.blockViewportRect(added);
            verify(shown.x >= margin - 0.5 && shown.x + shown.width <= canvas.width - margin + 0.5,
                   "block " + (count + 1) + " is at " + shown.x + "–" + (shown.x + shown.width)
                   + " on a canvas " + canvas.width + " wide");
            verify(shown.y >= margin - 0.5 && shown.y + shown.height <= canvas.height - margin + 0.5,
                   "block " + (count + 1) + " is off the top or bottom");
        }
        verify(canvas.originX < 0, "eight blocks in a row fitted a canvas this size, so the test tested nothing");
    }

    function test_aBlockThatIsAlreadyInViewDoesNotMoveTheView() {
        const canvas = createTemporaryObject(canvasComponent, null);
        const first = canvas.addBlockAfterSelection("effect.volume");
        verify(first !== "");
        const x = canvas.originX;
        const y = canvas.originY;
        canvas.addBlockAfterSelection("effect.volume");
        compare(canvas.originX, x, "the view moved for a block it could already show");
        compare(canvas.originY, y);

        // And one dropped by the pointer is where it was put, even past the
        // edge: the view does not move under it.
        canvas.addBlockAt("effect.volume", canvas.width + 400, 200);
        compare(canvas.originX, x, "the view moved under a dropped block");
    }
}
