// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.ki18n

// The session, laid out: the tiles, what is selected, and where the view is
// looking.
//
// **The canvas holds the view, and the engine holds the session.** Which
// tiles are selected, where the view is panned to and how far it is zoomed
// are about looking at a session rather than about the session, so they live
// here and are never written to the file. Where a tile *sits* is the
// session's, and every move goes to the engine as an edit and comes back as
// a patch, like everything else.
//
// **Nothing is moved until the drag ends.** A tile follows the pointer by
// being drawn somewhere else; the session only hears about it when the
// button comes up. Sixty edits a second to say where a tile is halfway to
// would be sixty edits nobody asked for and sixty undo steps to walk back.
//
// **The selection is ids, never row numbers.** A row stops meaning the same
// block the moment one is removed, and a removal is exactly the moment a
// selection matters.
Item {
    id: canvas

    /// What a block type travels under when it is dragged out of the
    /// library. **Written down once**, because the two halves of that
    /// gesture are two files and a type nobody accepts is a drag that
    /// silently does nothing.
    readonly property string blockTypeMimeType: "application/x-reverblabr-blocktype"

    /// How far in and out the view goes. Half size is where a large session
    /// fits on a screen; twice is where somebody checks what a tile reads.
    readonly property real minimumZoom: 0.5
    readonly property real maximumZoom: 2.0
    property real zoom: 1.0

    /// Where the session's origin sits in the viewport, in viewport pixels.
    property real originX: 0
    property real originY: 0

    /// Where a tile lands. **Eight pixels**, which is small enough not to
    /// be felt while dragging and large enough that two tiles dropped by
    /// eye line up exactly rather than nearly.
    readonly property int snapPixels: 8
    /// How far apart the grid's dots are: three snaps while following the
    /// system, a dot to aim at and two steps between, which is a grid that
    /// helps rather than a screen full of dots; the prototype's twenty
    /// pixels in the Reverblabr appearances, where the dots are a
    /// texture rather than something to aim at.
    readonly property real gridCell: AppMetrics.canvasGridSpacing

    /// The blocks and the wires, as rows.
    readonly property alias blocks: blocks
    readonly property alias wires: wires
    /// The tiles and the curves themselves, for a test that drives what a
    /// person drives.
    readonly property alias tiles: tileRepeater
    readonly property alias wireShapes: wireRepeater

    /// Which blocks are selected, by id.
    property var selectedIds: []
    /// The wire that is selected, as its four ends, or nothing. **One at a
    /// time, and never together with tiles**: a wire is selected to be
    /// looked at or taken out, and a Del that removed three tiles and a
    /// wire because both happened to be selected is a Del nobody meant.
    property var selectedWire: null
    /// The block whose name is being typed, or empty. **Held here, not in
    /// the tile**, so that only one rename can be under way at a time.
    property string renamingId: ""

    /// How far the selection has been dragged so far, in session pixels.
    property real dragX: 0
    property real dragY: 0
    property bool dragging: false

    /// A block was double-clicked, Enter was pressed on it or its menu's
    /// Settings was chosen. The canvas opens its settings itself; the
    /// signal is what anything else watching the canvas goes by.
    signal blockActivated(string blockId)

    /// The settings panel, and the block it is showing. **The canvas owns
    /// it**: a block's settings open beside that block, and the
    /// canvas is the only thing that knows where a tile has got to.
    readonly property alias settings: settingsPanel
    /// The menu a right click on a tile opens, for a test that chooses from
    /// it what a person would.
    readonly property alias tileMenu: tileMenu
    /// The connection-mode toggle at the top left.
    readonly property alias connectionMode: connectionMode
    /// The session as a list, in place of the tiles and wires.
    readonly property alias sessionList: sessionList
    /// Whether the list shows rather than the canvas: the person's choice,
    /// kept in the state file.
    property bool listShown: WindowState.listShown

    /// How far the panel stands off its block, and how close it may come to
    /// the edge of the viewport. The design's 16 and 8.
    readonly property real settingsGap: AppMetrics.settingsGap
    readonly property real settingsMargin: AppMetrics.settingsMargin

    clip: true

    // --- looking at the session ---

    /// Turns a point in the viewport into a point in the session.
    function toSession(x: real, y: real): point {
        return Qt.point((x - canvas.originX) / canvas.zoom, (y - canvas.originY) / canvas.zoom);
    }

    /// Sets the zoom, keeping whatever is under (\a atX, \a atY) where it
    /// is. **Zooming is done at a place**, not at the middle of nowhere:
    /// the thing somebody is looking at has to stay under the pointer or
    /// they have to find it again afterwards.
    function setZoom(wanted: real, atX: real, atY: real) {
        const next = Math.max(canvas.minimumZoom, Math.min(canvas.maximumZoom, wanted));
        if (next === canvas.zoom) {
            return;
        }
        const anchor = canvas.toSession(atX, atY);
        canvas.zoom = next;
        canvas.originX = atX - anchor.x * next;
        canvas.originY = atY - anchor.y * next;
    }

    function zoomIn() {
        canvas.setZoom(canvas.zoom * 1.25, canvas.width / 2, canvas.height / 2);
    }

    function zoomOut() {
        canvas.setZoom(canvas.zoom / 1.25, canvas.width / 2, canvas.height / 2);
    }

    /// A hundred per cent, with the session's origin back where it started.
    function actualSize() {
        canvas.zoom = 1;
        canvas.originX = 0;
        canvas.originY = 0;
    }

    /// The whole session on the screen at once, with a tile's worth of room
    /// around it. An empty session has nothing to fit, so it goes back to
    /// actual size rather than to whatever zoom makes nothing fill a window.
    function fitToView() {
        const bounds = canvas.contentBounds();
        if (bounds.width <= 0 || bounds.height <= 0 || canvas.width <= 0 || canvas.height <= 0) {
            canvas.actualSize();
            return;
        }
        const margin = Kirigami.Units.gridUnit * 2;
        const wanted = Math.min((canvas.width - margin * 2) / bounds.width,
                                (canvas.height - margin * 2) / bounds.height);
        canvas.zoom = Math.max(canvas.minimumZoom, Math.min(canvas.maximumZoom, wanted));
        canvas.originX = (canvas.width - bounds.width * canvas.zoom) / 2 - bounds.x * canvas.zoom;
        canvas.originY = (canvas.height - bounds.height * canvas.zoom) / 2 - bounds.y * canvas.zoom;
    }

    /// What the tiles cover, in session pixels. **Read from the model, not
    /// from the delegates**: a row whose tile has not been built yet is
    /// still a block in the session, and a fit that left it out would be a
    /// fit that changed with the scroll position.
    function contentBounds(): rect {
        const size = canvas.tileSize();
        let left = Infinity;
        let top = Infinity;
        let right = -Infinity;
        let bottom = -Infinity;
        for (let row = 0; row < blocks.count; ++row) {
            const at = blocks.at(row).position;
            left = Math.min(left, at.x);
            top = Math.min(top, at.y);
            right = Math.max(right, at.x + size.width);
            bottom = Math.max(bottom, at.y + size.height);
        }
        if (left === Infinity) {
            return Qt.rect(0, 0, 0, 0);
        }
        return Qt.rect(left, top, right - left, bottom - top);
    }

    // --- what is selected ---

    function isSelected(blockId: string): bool {
        return canvas.selectedIds.indexOf(blockId) >= 0;
    }

    function selectOnly(blockId: string) {
        canvas.selectedIds = blockId === "" ? [] : [blockId];
    }

    function clearSelection() {
        canvas.selectedIds = [];
    }

    function toggleSelection(blockId: string) {
        canvas.selectedIds = canvas.isSelected(blockId)
            ? canvas.selectedIds.filter(id => id !== blockId)
            : canvas.selectedIds.concat([blockId]);
    }

    /// What a press on a tile means. **A press on a tile that is already
    /// selected leaves the selection alone**, or dragging three tiles by
    /// one of them would drag only the one that was pressed.
    function pressOn(blockId: string, modifiers: int) {
        // A tile and a wire are never selected together: what Del means has
        // to be one thing.
        canvas.selectedWire = null;
        if ((modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) !== 0) {
            canvas.toggleSelection(blockId);
            return;
        }
        if (!canvas.isSelected(blockId)) {
            canvas.selectOnly(blockId);
        }
    }

    // --- moving tiles ---

    function dragBy(dx: real, dy: real) {
        canvas.dragging = true;
        // The pointer moves in the viewport; the tiles live in the session,
        // so how far they have come depends on how far the view is zoomed.
        canvas.dragX = dx / canvas.zoom;
        canvas.dragY = dy / canvas.zoom;
    }

    /// The button came up. Every selected tile is written where it landed,
    /// snapped to the grid. **The engine's history merges edits that carry
    /// on one gesture**, so a move of five tiles is one Ctrl+Z.
    function endDrag() {
        if (!canvas.dragging) {
            return;
        }
        const dx = canvas.dragX;
        const dy = canvas.dragY;
        canvas.dragging = false;
        canvas.dragX = 0;
        canvas.dragY = 0;
        if (dx === 0 && dy === 0) {
            return;
        }
        for (const blockId of canvas.selectedIds) {
            const at = SessionModel.block(blockId).position;
            if (at === undefined) {
                continue;
            }
            SessionModel.moveBlock(blockId, Qt.point(canvas.snapped(at.x + dx), canvas.snapped(at.y + dy)));
        }
    }

    function cancelDrag() {
        canvas.dragging = false;
        canvas.dragX = 0;
        canvas.dragY = 0;
    }

    function snapped(value: real): real {
        return Math.round(value / canvas.snapPixels) * canvas.snapPixels;
    }

    // --- adding and removing ---

    /// **The ear a new hearing block starts on**: the side with fewer
    /// blocks of its type already, the left on a tie. A second Hearing
    /// Compensation added after one for the left ear is for the right one,
    /// which is how a pair for two ears is set up. \a typeId with the ear
    /// to start on, as SessionModel.addBlock() takes a setting with the
    /// type, so the block and its ear are one edit; \a typeId as it was for
    /// a block without an Ear.
    function withStartingEar(typeId: string): string {
        if (typeId.indexOf("?") >= 0 || BlockCatalogue.describeParam(typeId, "ear").id === undefined) {
            return typeId;
        }
        let left = 0;
        let right = 0;
        for (const id of SessionModel.blockIds) {
            if (SessionModel.blockType(id) === typeId) {
                if ((SessionModel.param(id, "ear") ?? "left") === "right") {
                    ++right;
                } else {
                    ++left;
                }
            }
        }
        return right < left ? typeId + "?ear=right" : typeId;
    }

    /// Puts a block where the pointer is, in viewport coordinates, with the
    /// tile centred on it: a block dropped on a spot belongs on that spot
    /// and not below and to the right of it.
    function addBlockAt(typeId: string, x: real, y: real): string {
        const at = canvas.toSession(x, y);
        const size = canvas.tileSize();
        // **Where it was dropped, not where there is room**, and wired to
        // nothing. Somebody who carried a block to a spot chose that spot
        // and chose to put it there rather than after something; automatic
        // connections are what addBlockAfterSelection() does, where placing
        // the block and joining it up are one gesture.
        const added = SessionModel.addBlock(canvas.withStartingEar(typeId), Qt.point(canvas.snapped(at.x - size.width / 2),
                                                             canvas.snapped(at.y - size.height / 2)));
        if (added !== "") {
            canvas.selectOnly(added);
        }
        return added;
    }

    /// Puts a block after the one that is selected, which is what clicking
    /// an entry in the library does. With nothing selected it goes to the
    /// right of everything there already, so that a session built by
    /// clicking reads left to right.
    function addBlockAfterSelection(typeId: string): string {
        const size = canvas.tileSize();
        const gap = size.width / 2;
        let at = Qt.point(0, 0);
        const last = canvas.selectedIds.length > 0
            ? canvas.selectedIds[canvas.selectedIds.length - 1]
            : "";
        if (last !== "") {
            const from = SessionModel.block(last).position;
            at = Qt.point(from.x + size.width + gap, from.y);
        } else {
            const bounds = canvas.contentBounds();
            at = bounds.width > 0 ? Qt.point(bounds.x + bounds.width + gap, bounds.y) : canvas.toSession(gap, gap);
        }
        const added = SessionModel.addBlock(canvas.withStartingEar(typeId), canvas.freeSpot(at, size));
        if (added !== "") {
            // **What it is wired to is what *was* selected**, kept here
            // before the new block takes the selection.
            canvas.selectOnly(added);
            canvas.awaitingArrival = canvas.awaitingArrival.concat([{ added: added, after: last }]);
            canvas.settleArrivals();
        }
        return added;
    }

    /// Blocks added after another, as `{ added, after }`, that the session
    /// does not have yet. **The engine answers an edit when it answers**:
    /// the one in reverblabrd does so over the bus, after addBlock() has
    /// returned, and a wire asked for at once was asked for to a block the
    /// session had never heard of, and refused. So the wire is drawn, and
    /// the view moved to the block, when the block arrives. The engine's
    /// history merges a wire that soon after into the block it came with,
    /// so both still go back in one Ctrl+Z.
    property var awaitingArrival: []

    /// Wires and shows each awaited block the session now has.
    function settleArrivals() {
        const waiting = [];
        for (const entry of canvas.awaitingArrival) {
            if (SessionModel.blockType(entry.added) === "") {
                waiting.push(entry);
                continue;
            }
            canvas.autoConnect(entry.added, entry.after);
            canvas.bringIntoView(entry.added);
        }
        canvas.awaitingArrival = waiting;
    }

    Connections {
        target: SessionModel
        enabled: canvas.awaitingArrival.length > 0

        function onChanged() {
            canvas.settleArrivals();
        }

        // **Not across sessions**: a block that was on its way to the
        // session that was showing is not one to wire in the next.
        function onSessionChanged() {
            canvas.awaitingArrival = [];
        }
    }

    /// Pans just far enough that \a blockId is on screen whole, with a
    /// margin, and not at all when it already is.
    ///
    /// **For a block the canvas placed, not one the pointer did**. A
    /// click adds a block beside the last one, and a lane of six from a
    /// library click at a time ran off the right-hand edge: the person who
    /// had just added it could not see it, or open it, until they found it.
    /// A block that was dropped is where it was put, and moving the view
    /// under it would be moving it away from the pointer.
    function bringIntoView(blockId: string) {
        if (canvas.listShown) {
            sessionList.showRow(blockId);
            return;
        }
        const shown = canvas.blockViewportRect(blockId);
        const margin = Kirigami.Units.gridUnit;
        const shift = (start, extent, room) => {
            if (extent + margin * 2 > room || start < margin) {
                return margin - start;
            }
            return start + extent > room - margin ? room - margin - (start + extent) : 0;
        };
        canvas.originX += shift(shown.x, shown.width, canvas.width);
        canvas.originY += shift(shown.y, shown.height, canvas.height);
    }

    /// How big a tile is. Taken from one that exists where there is one, so
    /// that the size is written down in the tile and nowhere else.
    function tileSize(): size {
        const first = tileRepeater.count > 0 ? tileRepeater.itemAt(0) : null;
        return first === null
            ? Qt.size(AppMetrics.tileOuterWidth, AppMetrics.tileOuterHeight)
            : Qt.size(first.width, first.height);
    }

    // --- where the settings panel goes ---
    //
    // **In viewport pixels, from the session.** The panel is not inside the
    // zoom transform — scaled it would be unreadable at 50 % and blurred at
    // 200 % — so where its block has got to has to be worked out rather than
    // inherited: the session position, times the zoom, plus wherever the
    // view has been panned to.

    /// Where a block is drawn, in the viewport. **Where it is drawn, not
    /// where it is saved**: a tile being dragged has not been written to the
    /// session yet, and a panel that stayed behind while its block moved
    /// would be a panel pointing at nothing.
    function blockViewportRect(blockId: string): rect {
        const block = SessionModel.block(blockId);
        const size = canvas.tileSize();
        const moving = canvas.dragging && canvas.isSelected(blockId);
        const at = block.position ?? Qt.point(0, 0);
        const x = at.x + (moving ? canvas.dragX : 0);
        const y = at.y + (moving ? canvas.dragY : 0);
        return Qt.rect(canvas.originX + x * canvas.zoom, canvas.originY + y * canvas.zoom,
                       size.width * canvas.zoom, size.height * canvas.zoom);
    }

    // **Beside the row, in the list**: the tile it would be beside
    // is not showing.
    readonly property rect settingsAnchor: settingsPanel.blockId !== "" && SessionModel.revision >= 0
        ? (canvas.listShown ? sessionList.rowRect(settingsPanel.blockId) : canvas.blockViewportRect(settingsPanel.blockId))
        : Qt.rect(0, 0, 0, 0)

    /// Whether the panel has had to go to the left of its block because
    /// there was no room on the right.
    readonly property bool settingsFlipped: canvas.settingsAnchor.x + canvas.settingsAnchor.width
                                          + canvas.settingsGap + settingsPanel.width
                                          > canvas.width - canvas.settingsMargin

    /// Keeps a panel inside the viewport, beside its block or pulled off it.
    /// **One pair of clamps for every panel**, so a detached panel cannot be
    /// dragged somewhere a docked one would never be put, and none is left
    /// outside when the window is resized.
    function clampPanelX(value: real, width: real): real {
        return Math.max(canvas.settingsMargin, Math.min(canvas.width - width - canvas.settingsMargin, value));
    }

    function clampPanelY(value: real, height: real): real {
        return Math.max(canvas.settingsMargin, Math.min(canvas.height - height - canvas.settingsMargin, value));
    }

    function clampSettingsX(value: real): real {
        return canvas.clampPanelX(value, settingsPanel.width);
    }

    function clampSettingsY(value: real): real {
        return canvas.clampPanelY(value, settingsPanel.height);
    }

    // --- settings pulled off their blocks ---
    //
    // **A detached panel lives on its own.** Pulling one off a block is for
    // keeping those settings in view while working on the rest of the
    // canvas, so it does not close when another block is clicked, does not
    // go back beside its block when its block is, and stays where it was
    // put, across runs. Any number can be out at once, one per block; the
    // docked panel beside the block in hand is a separate, single thing.
    //
    // Positions are in the viewport, like the docked panel's, because none
    // of them is in the zoom transform.

    /// One row per detached panel: `panelBlock`, `px`, `py`, and `order`,
    /// which says which is on top.
    ListModel {
        id: detachedModel
    }

    /// The detached panels, for a test that works one as a person would.
    readonly property alias detachedPanels: detachedRepeater
    /// How many are out.
    readonly property int detachedCount: detachedModel.count
    property int detachedOrder: 0
    /// Panels remembered for this session whose blocks have not arrived
    /// yet: the session's blocks come a moment after the session does.
    property var pendingDetached: []

    function detachedIndexOf(blockId: string): int {
        for (let index = 0; index < detachedModel.count; ++index) {
            if (detachedModel.get(index).panelBlock === blockId) {
                return index;
            }
        }
        return -1;
    }

    /// The detached panel showing \a blockId, or null.
    function detachedPanelFor(blockId: string): var {
        // A repeater's items are its model's rows, in the same order.
        const index = canvas.detachedIndexOf(blockId);
        return index < 0 ? null : detachedRepeater.itemAt(index);
    }

    /// **Asking for a block's settings**, which a double-click, Enter and
    /// the menu's Settings all are, and a single click is not.
    function activateBlock(blockId: string) {
        canvas.openSettings(blockId);
        canvas.blockActivated(blockId);
    }

    /// Opens the settings of \a blockId beside it, in the one docked panel.
    ///
    /// **A block whose settings are out already gets those**, brought to the
    /// top where they are, not a second panel and not docked again.
    /// Only the panel's own attach control puts it back.
    function openSettings(blockId: string) {
        const index = canvas.detachedIndexOf(blockId);
        if (index >= 0) {
            canvas.raiseDetached(index);
            return;
        }
        settingsPanel.open(blockId);
    }

    /// Closes the docked panel. **Only that one**: a press on nothing, or
    /// Escape, is about what is beside the block in hand, and a detached
    /// panel closes from its own close button.
    function closeSettings() {
        settingsPanel.close();
    }

    /// Pulls the docked panel off its block.
    ///
    /// **It stays where it was, give or take.** It has not been reopened, so
    /// it must not jump across the canvas; it is nudged clear of where it was
    /// docked so that something visibly happened, and kept inside the
    /// viewport.
    function detachSettings() {
        const blockId = settingsPanel.blockId;
        if (blockId === "" || canvas.detachedIndexOf(blockId) >= 0) {
            return;
        }
        const nudge = Kirigami.Units.gridUnit * 1.5;
        detachedModel.append({
            panelBlock: blockId,
            px: canvas.clampSettingsX(settingsPanel.x + nudge),
            py: canvas.clampSettingsY(settingsPanel.y - nudge),
            order: ++canvas.detachedOrder
        });
        settingsPanel.close();
        canvas.saveDetached();
    }

    /// Puts a detached panel back beside its block, in place of whatever
    /// the docked panel was showing.
    function attachSettings(blockId: string) {
        const index = canvas.detachedIndexOf(blockId);
        if (index < 0) {
            return;
        }
        detachedModel.remove(index);
        settingsPanel.open(blockId);
        canvas.saveDetached();
    }

    function closeDetached(blockId: string) {
        const index = canvas.detachedIndexOf(blockId);
        if (index >= 0) {
            detachedModel.remove(index);
            canvas.saveDetached();
        }
    }

    function raiseDetached(index: int) {
        detachedModel.setProperty(index, "order", ++canvas.detachedOrder);
    }

    /// A detached panel's header was dragged. **In viewport pixels**: the
    /// panel is not in the zoom transform, so a drag moves it exactly as far
    /// as the pointer went.
    function dragDetachedBy(index: int, dx: real, dy: real, width: real, height: real) {
        const row = detachedModel.get(index);
        detachedModel.setProperty(index, "px", canvas.clampPanelX(row.px + dx, width));
        detachedModel.setProperty(index, "py", canvas.clampPanelY(row.py + dy, height));
    }

    /// Writes down which panels are out and where, for this session.
    function saveDetached() {
        if (SessionModel.sessionId === "") {
            return;
        }
        const panels = [];
        for (let index = 0; index < detachedModel.count; ++index) {
            const row = detachedModel.get(index);
            panels.push({ blockId: row.panelBlock, x: row.px, y: row.py });
        }
        for (const entry of canvas.pendingDetached) {
            panels.push(entry);
        }
        WindowState.setDetachedPanels(SessionModel.sessionId, panels);
    }

    /// Forgets the panels on the canvas and brings back the ones remembered
    /// for the session showing: when the session changes, and when the
    /// canvas is made with a session already showing, as it is at start.
    function loadDetached() {
        detachedModel.clear();
        canvas.pendingDetached = SessionModel.sessionId === "" ? [] : WindowState.detachedPanels(SessionModel.sessionId);
        canvas.restoreDetached();
    }

    Component.onCompleted: canvas.loadDetached()

    /// Puts back the remembered panels whose blocks are here now.
    function restoreDetached() {
        const waiting = [];
        for (const entry of canvas.pendingDetached) {
            if ((SessionModel.block(entry.blockId).type ?? "") === "") {
                waiting.push(entry);
                continue;
            }
            if (canvas.detachedIndexOf(entry.blockId) < 0) {
                detachedModel.append({ panelBlock: entry.blockId, px: entry.x, py: entry.y,
                                       order: ++canvas.detachedOrder });
            }
        }
        // **A session that has arrived and does not have the block any more
        // never will**, so what is still waiting then is forgotten rather
        // than carried from save to save.
        canvas.pendingDetached = SessionModel.blockIds.length > 0 ? [] : waiting;
    }

    /// A panel whose block has gone closes itself — removed here, removed by
    /// an undo, or a whole session changed under it. A panel for a block
    /// that is not there any more has nothing to show and nothing to point
    /// at.
    ///
    /// **Watched rather than derived.** A property worked out from
    /// `blockId`, whose handler then clears `blockId`, is a property that
    /// depends on what it changes; QML says so and calls it a binding loop.
    Connections {
        target: SessionModel

        function onChanged() {
            if (settingsPanel.blockId !== "" && (SessionModel.block(settingsPanel.blockId).type ?? "") === "") {
                settingsPanel.close();
            }
            if (canvas.pendingDetached.length > 0) {
                canvas.restoreDetached();
            }
            // A detached panel whose block has gone goes with it.
            let dropped = false;
            for (let index = detachedModel.count - 1; index >= 0; --index) {
                if ((SessionModel.block(detachedModel.get(index).panelBlock).type ?? "") === "") {
                    detachedModel.remove(index);
                    dropped = true;
                }
            }
            if (dropped) {
                canvas.saveDetached();
            }
        }

        // **Each session has its own panels out.** Switching tabs puts this
        // one's away, as they were, and brings the next one's back.
        function onSessionChanged() {
            canvas.loadDetached();
        }
    }

    function removeSelected() {
        // Del means the wire when a wire is what is selected; a wire and a
        // tile are never both.
        if (canvas.selectedWire !== null) {
            canvas.removeSelectedWire();
            return;
        }
        if (canvas.selectedIds.length === 0) {
            return;
        }
        const going = canvas.selectedIds;
        canvas.clearSelection();
        SessionModel.removeBlocks(going);
    }

    /// Starts renaming the one selected block, which F2 and the context
    /// menu both do. With several selected there is no one name to type.
    /// Starts renaming the one selected tile, and says whether there was
    /// one. **F2 renames whatever is in front of the person pressing it**,
    /// so the window needs to know when the canvas had nothing to rename
    /// and the session is what was meant.
    function startRenaming(): bool {
        if (canvas.selectedIds.length !== 1) {
            return false;
        }
        canvas.renamingId = canvas.selectedIds[0];
        return true;
    }

    BlockModel {
        id: blocks

        session: SessionModel
    }

    WireModel {
        id: wires

        session: SessionModel
        inputLaneWidths: canvas.inputLaneWidths
    }

    // --- where a port is ---
    //
    // **The canvas's arithmetic, not the tile's.** A wire has to be drawn
    // to a port whether that tile has been built yet or not — a tile
    // scrolled out of the view, or one added in the same frame — so where a
    // port sits has to be something the canvas can work out from the
    // session alone. The tile is handed the same function and draws its
    // ports where it says, so the two cannot disagree.

    /// How far apart two connectors on the same edge of a tile sit. They
    /// are spread about the middle of the edge, so a block with one lane
    /// has it in the middle and a block with two has them either side.
    readonly property real lanePitch: Math.round(Kirigami.Units.gridUnit)

    function laneOffset(index: int, count: int, output: bool, size: size): point {
        return Qt.point(output ? size.width : 0,
                        size.height / 2 + (index - (count - 1) / 2) * canvas.lanePitch);
    }

    /// What shape each block type is and which category it is in, read
    /// from the catalogue once.
    ///
    /// **Not asked per wire per frame.** A wire's two ends are worked out
    /// again on every frame of a drag, and describing a block type builds
    /// the whole of its parameter table to answer a question about two
    /// numbers. The catalogue does not change while the engine runs, so it
    /// is read when it arrives and answered from here after that.
    readonly property var portShapes: {
        const shapes = {};
        if (!BlockCatalogue.ready) {
            return shapes;
        }
        for (const type of BlockCatalogue.types) {
            const info = BlockCatalogue.describe(type);
            const least = (side) => side?.minimum ?? 0;
            shapes[type] = {
                category: info.category ?? "",
                inputsLeast: least(info.inputs),
                inputsMost: Math.max(least(info.inputs), info.inputs?.maximum ?? 0),
                outputsLeast: least(info.outputs),
                outputsMost: Math.max(least(info.outputs), info.outputs?.maximum ?? 0),
                // Which of its ports somebody else fills or takes.
                inputsAreTheAudioSystems: info.inputs?.audioSystem === true,
                outputsAreTheAudioSystems: info.outputs?.audioSystem === true,
                // How many ports make one connector, and what each is for:
                // nought is one connector however many ports.
                inputsLaneWidth: info.inputs?.laneWidth ?? 0,
                outputsLaneWidth: info.outputs?.laneWidth ?? 0,
                inputLanes: info.inputs?.lanes ?? [],
                outputLanes: info.outputs?.lanes ?? []
            };
        }
        return shapes;
    }

    /// How many **channels** a block carries on one edge: one port each,
    /// which is what the session holds a wire for. **The fewest it can
    /// have, unless a wire asks for more** — which is the rule the graph
    /// compiler applies when it decides how many ports to give a node, so
    /// what is drawn and what is compiled agree.
    ///
    /// This is not how many connectors are drawn. A link carries a stereo
    /// signal and shows as one dot; see laneCountOf().
    function channelCountOf(blockId: string, output: bool): int {
        // Read at the top so that a binding follows the session, whatever
        // the lookups below happen to touch.
        const revision = SessionModel.revision;
        const shape = canvas.portShapes[SessionModel.blockType(blockId)];
        if (shape === undefined) {
            return 0; // a block type this build does not have draws no ports
        }
        // **Ports the audio system owns are not drawn**: nothing can
        // ever be wired to them, and a dot that looks like somewhere a wire
        // goes and never is, is worse than no dot. They are still counted by
        // portLimit(), which is what tells a drop on one why it was refused.
        if (canvas.portsAreTheAudioSystems(blockId, output)) {
            return 0;
        }
        const least = output ? shape.outputsLeast : shape.inputsLeast;
        const most = output ? shape.outputsMost : shape.inputsMost;
        const needed = Math.max(least, SessionModel.portsInUse(blockId, output));
        return revision >= 0 ? Math.min(needed, most) : least;
    }

    /// How many connectors a block shows on one edge.
    ///
    /// **A link carries a stereo signal, so a tile has one connector a
    /// side, not one a channel**. The channels are how the signal is
    /// carried — a link only ever joins two ports of the same channel
    /// position ([ADR-0007]) — and that is the engine's business, not
    /// something to make somebody wire up twice.
    ///
    /// It is counted rather than fixed at one because a block may have
    /// several *lanes* on a side, which is a different thing from several
    /// channels: the Mixer of M2 takes three separate signals. The
    /// catalogue says how many groups of ports a block has, and that is
    /// what is drawn.
    function laneCountOf(blockId: string, output: bool): int {
        const channels = canvas.channelCountOf(blockId, output);
        const width = canvas.laneWidthOf(blockId, output);
        if (channels === 0) {
            return 0;
        }
        return width > 0 ? Math.ceil(channels / width) : 1;
    }

    /// How many ports make one connector on that edge, or nought when the
    /// whole edge is one. The ducker's two inputs are two ports each.
    function laneWidthOf(blockId: string, output: bool): int {
        const shape = canvas.portShapes[SessionModel.blockType(blockId)];
        if (shape === undefined) {
            return 0;
        }
        return output ? shape.outputsLaneWidth : shape.inputsLaneWidth;
    }

    /// How many channels one connector on that edge carries: a lane's
    /// width, or every channel of an edge that is one lane.
    function laneChannelsOf(blockId: string, output: bool): int {
        const width = canvas.laneWidthOf(blockId, output);
        return width > 0 ? width : canvas.channelCountOf(blockId, output);
    }

    /// The first port of connector \a lane.
    function firstPortOf(blockId: string, lane: int, output: bool): int {
        return lane * canvas.laneWidthOf(blockId, output);
    }

    /// The stable id of connector \a lane — "main", "background" — or
    /// empty on an edge that is one lane.
    function laneIdOf(blockId: string, lane: int, output: bool): string {
        const shape = canvas.portShapes[SessionModel.blockType(blockId)];
        const lanes = shape === undefined ? [] : (output ? shape.outputLanes : shape.inputLanes);
        return lane >= 0 && lane < lanes.length ? lanes[lane] : "";
    }

    /// The input lane widths of the types that have more than one, for the
    /// link rows (WireModel).
    readonly property var inputLaneWidths: {
        const widths = {};
        for (const type in canvas.portShapes) {
            if (canvas.portShapes[type].inputsLaneWidth > 0) {
                widths[type] = canvas.portShapes[type].inputsLaneWidth;
            }
        }
        return widths;
    }

    /// Where one connector is, in session pixels — **including how far its
    /// tile has been dragged but not yet dropped**, so that a wire follows
    /// the tile it is attached to rather than waiting for the button to
    /// come up.
    function lanePoint(blockId: string, lane: int, output: bool): point {
        const size = canvas.tileSize();
        const at = SessionModel.blockPosition(blockId);
        const offset = canvas.laneOffset(lane, canvas.laneCountOf(blockId, output), output, size);
        const moved = canvas.dragging && canvas.isSelected(blockId);
        return Qt.point(at.x + offset.x + (moved ? canvas.dragX : 0),
                        at.y + offset.y + (moved ? canvas.dragY : 0));
    }

    /// How far a wire's control points reach out from each end: half the
    /// horizontal distance, and never less than thirty pixels. **Written
    /// down once**, because the curve that is drawn and the curve a click
    /// is measured against have to be the same curve.
    function curvature(fromX: real, toX: real): real {
        return Math.max(30, Math.abs(toX - fromX) / 2);
    }

    /// A point along a wire, at \a t from nought to one.
    function wirePoint(from: point, to: point, k: real, t: real): point {
        const u = 1 - t;
        const c1 = Qt.point(from.x + k, from.y);
        const c2 = Qt.point(to.x - k, to.y);
        return Qt.point(u * u * u * from.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x + t * t * t * to.x,
                        u * u * u * from.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y + t * t * t * to.y);
    }

    /// Whether audio is going through the block a wire leaves. **A wire
    /// takes its colour from its source**, because that is where what is on
    /// it came from: a wire into a bypassed block is still carrying what
    /// the block before it put out.
    function wireIsLive(fromBlock: string): bool {
        return Meters.frame >= 0 && Meters.live && (Meters.levels(fromBlock).active ?? false);
    }

    // --- what a wire may be ---

    /// Why a wire between these two ports would be refused, as a stable
    /// name, or empty when it would be taken.
    ///
    /// **Both halves of the answer.** The session's own rules — a block
    /// wired to itself, a wire that is already there, a loop — are the
    /// engine's, and it is asked for them rather than second-guessed. What
    /// shape a block is, on the other hand, is the block registry's, and
    /// reaches the interface as the catalogue.
    function laneRefusal(fromBlock: string, toBlock: string, toLane = 0): string {
        // **Asked about the first channel, and the answer is the lane's.**
        // What the session refuses — a block wired to itself, a link that
        // is already there, a loop — is about the two blocks and not about
        // which channel of them, so one question answers for all of them.
        // **Of the lane it would arrive at**: a source already wired
        // to a ducker's main may still go to its background.
        const refused = SessionModel.wireRefusal(fromBlock, 0, toBlock,
                                                 canvas.firstPortOf(toBlock, Math.max(0, toLane), false));
        if (refused !== "") {
            return refused;
        }
        // **A capture block's inputs are not canvas ports.** What arrives
        // at one is the application or the device it is following, put
        // there by the audio system, and a wire into the same ports would
        // be two things writing one signal. The same the other way round:
        // an output block's outputs belong to the device.
        //
        // **The block says so, not the sidebar**. This used to read
        // the category, which is where a block is listed; the catalogue now
        // carries the fact itself, and the engine refuses such a wire with
        // these same names whoever asks for it.
        //
        // **Asked before the count**, because an edge the audio system owns
        // has no channels to offer either, and "there is nowhere to put it"
        // is a poorer answer than the one that says why.
        if (canvas.portsAreTheAudioSystems(toBlock, false)) {
            return "wireIntoSource";
        }
        if (canvas.portsAreTheAudioSystems(fromBlock, true)) {
            return "wireOutOfOutput";
        }
        if (canvas.channelCountOf(fromBlock, true) === 0 || canvas.channelCountOf(toBlock, false) === 0) {
            return "wirePortOutOfRange";
        }
        return "";
    }

    /// Whether the audio system fills or takes \a blockId's ports on the
    /// given side, so that no wire can ever reach them. A block type this
    /// build does not have is not second-guessed.
    function portsAreTheAudioSystems(blockId: string, output: bool): bool {
        const shape = canvas.portShapes[SessionModel.blockType(blockId)];
        if (shape === undefined) {
            return false;
        }
        return output ? shape.outputsAreTheAudioSystems : shape.inputsAreTheAudioSystems;
    }

    /// The most ports of one kind a block type can have. A type this build
    /// does not have is not second-guessed: its block was kept exactly as
    /// the file held it, ports and all.
    function portLimit(blockId: string, output: bool): int {
        const shape = canvas.portShapes[SessionModel.blockType(blockId)];
        if (shape === undefined) {
            return Number.MAX_SAFE_INTEGER;
        }
        return output ? shape.outputsMost : shape.inputsMost;
    }

    function categoryOf(blockId: string): string {
        const shape = canvas.portShapes[SessionModel.blockType(blockId)];
        return shape === undefined ? "" : shape.category;
    }

    // --- drawing a wire by hand ---
    //
    // **Ctrl and drag, from a port to a port.** Ctrl because the left
    // button on a tile is already how tiles are selected and moved, and
    // because a drag that began on a four-pixel dot by accident would be a
    // wire nobody asked for.

    property bool wiring: false
    /// The end that stays put while the other is dragged.
    property string wiringFromBlock: ""
    property int wiringFromLane: 0
    /// The block the link is being taken off, when the drag began on an
    /// input that already had one, and which of its lanes. Empty while a new
    /// link is being drawn.
    property string wiringOffBlock: ""
    property int wiringOffLane: 0
    /// Where the loose end is, in session pixels, and the lane it is over.
    property point wiringEnd: Qt.point(0, 0)
    property string wiringOverBlock: ""
    property int wiringOverLane: -1

    /// Whether this port would take the wire being drawn. **Only the ports
    /// that would take it light up**: a drag that cannot end well should
    /// say so while the pointer is still moving, rather than when the
    /// button comes up and nothing happens.
    function isWireCandidate(blockId: string, lane: int, output: bool): bool {
        if (!canvas.wiring || output) {
            return false;
        }
        return canvas.laneRefusal(canvas.wiringFromBlock, blockId, lane) === ""
            || blockId === canvas.wiringOffBlock;
    }

    /// A Ctrl-drag began on a connector. An output starts a new link; an
    /// input with exactly one link on it takes that link off and carries
    /// it, which is how a link is moved from one block to another. An input
    /// with several is a sum, and there is no one link there to pick up.
    function beginWire(blockId: string, lane: int, output: bool) {
        canvas.cancelWire();
        if (output) {
            canvas.wiringFromBlock = blockId;
            canvas.wiringFromLane = lane;
        } else {
            // **The links into this lane**, not every link into the block:
            // a ducker's background is picked up without its voice.
            const width = canvas.laneWidthOf(blockId, false);
            const first = canvas.firstPortOf(blockId, lane, false);
            const sources = [];
            for (const wire of SessionModel.wiresTouching(blockId)) {
                const inLane = width === 0 || (wire.toPort >= first && wire.toPort < first + width);
                if (wire.toBlock === blockId && inLane && sources.indexOf(wire.fromBlock) < 0) {
                    sources.push(wire.fromBlock);
                }
            }
            if (sources.length !== 1) {
                return;
            }
            canvas.wiringFromBlock = sources[0];
            canvas.wiringFromLane = 0;
            canvas.wiringOffBlock = blockId;
            canvas.wiringOffLane = lane;
        }
        canvas.wiringEnd = canvas.lanePoint(blockId, lane, output);
        canvas.wiring = true;
    }

    /// The loose end is here now, in scene coordinates — which is what a
    /// port reports, because what is between it and the canvas is several
    /// items and one scale factor.
    function dragWireTo(sceneX: real, sceneY: real) {
        if (!canvas.wiring) {
            return;
        }
        canvas.wiringEnd = content.mapFromItem(null, sceneX, sceneY);
        const over = canvas.inputLaneAt(canvas.wiringEnd);
        canvas.wiringOverBlock = over === null ? "" : over.blockId;
        canvas.wiringOverLane = over === null ? -1 : over.lane;
    }

    /// The button came up. A wire that lands on a port it is allowed on is
    /// drawn; anything else leaves the session exactly as it was.
    function dropWire(sceneX: real, sceneY: real): bool {
        if (!canvas.wiring) {
            return false;
        }
        canvas.dragWireTo(sceneX, sceneY);
        const toBlock = canvas.wiringOverBlock;
        const toLane = Math.max(0, canvas.wiringOverLane);
        const fromBlock = canvas.wiringFromBlock;
        const offBlock = canvas.wiringOffBlock;
        const offLane = canvas.wiringOffLane;
        canvas.cancelWire();
        if (toBlock === "" || canvas.laneRefusal(fromBlock, toBlock, toLane) !== "") {
            // Let go over the lane it came off: the link stays where it
            // was, which is what putting something back has to mean.
            return offBlock !== "" && toBlock === offBlock;
        }
        return canvas.commitWire(fromBlock, offBlock, offLane, toBlock, toLane);
    }

    /// Draws the link from \a fromBlock into \a toBlock's lane \a toLane,
    /// taking it off \a offBlock's lane \a offLane first when it is being
    /// moved. **The pointer and the keyboard both end here**, so a wire
    /// aimed with the arrows is exactly the wire a Ctrl-drag draws.
    function commitWire(fromBlock: string, offBlock: string, offLane: int, toBlock: string, toLane: int): bool {
        // **Every channel both ends have.** A link carries a stereo signal,
        // and a chain wired only down the left is a chain that goes quietly
        // mono with nothing saying why. **Into the lane it was let go
        // over**, the lane's ports and no others.
        const channels = Math.min(canvas.laneChannelsOf(fromBlock, true), canvas.laneChannelsOf(toBlock, false));
        const toFirst = canvas.firstPortOf(toBlock, toLane, false);
        if (offBlock === "") {
            return SessionModel.connectLanes(fromBlock, toBlock, channels, toFirst);
        }
        const offWidth = canvas.laneWidthOf(offBlock, false);
        return SessionModel.moveLane(fromBlock, offBlock, toBlock, channels,
                                     offWidth > 0 ? canvas.firstPortOf(offBlock, offLane, false) : -1,
                                     offWidth, toFirst);
    }

    function cancelWire() {
        canvas.wiring = false;
        canvas.keyWiring = false;
        canvas.keyTargets = [];
        canvas.keyTarget = -1;
        canvas.wiringFromBlock = "";
        canvas.wiringFromLane = 0;
        canvas.wiringOffBlock = "";
        canvas.wiringOffLane = 0;
        canvas.wiringOverBlock = "";
        canvas.wiringOverLane = -1;
    }

    /// The input connector nearest \a at, in session pixels, or nothing.
    /// **Nearest within reach rather than under the pointer**: the dot is a
    /// few pixels across, and a link that had to be dropped on it exactly
    /// is a link most people cannot draw.
    function inputLaneAt(at: point): var {
        let nearest = Kirigami.Units.gridUnit * 1.5;
        let best = null;
        for (let row = 0; row < blocks.count; ++row) {
            const blockId = blocks.at(row).id;
            const count = canvas.laneCountOf(blockId, false);
            for (let lane = 0; lane < count; ++lane) {
                const where = canvas.lanePoint(blockId, lane, false);
                const distance = Math.hypot(where.x - at.x, where.y - at.y);
                if (distance < nearest) {
                    nearest = distance;
                    best = {blockId: blockId, lane: lane};
                }
            }
        }
        return best;
    }

    // --- wiring from the keyboard ---
    //
    // **C on a block, the arrows, then Enter.** The wire a Ctrl-drag draws,
    // aimed from the keyboard: the loose end goes from one input that would
    // take it to the next, in reading order, and the ports light up as they
    // do for the pointer. **D** goes through the wires already on the block
    // the same way, and Enter or Del takes out the one in hand. Esc leaves
    // everything as it was. While either is under way the keys come to the
    // canvas, and the focus goes back to the block afterwards.
    //
    // **Each step is said out loud** (`Accessible.announce`), because the
    // only other sign of where the loose end is has to be seen.

    /// Whether the wire being drawn is being aimed from the keyboard.
    property bool keyWiring: false
    /// Every input lane the wire would be taken by, in reading order, as
    /// `{blockId, lane}`, and which of them the loose end is on.
    property var keyTargets: []
    property int keyTarget: -1
    /// Whether D is going through a block's wires, the wires, and which of
    /// them is in hand.
    property bool keyUnwiring: false
    property var keyWires: []
    property int keyWire: -1
    /// The block C or D was pressed on, where the focus goes back to, and
    /// what was selected then, which is put back.
    property string keyHome: ""
    property var keySelection: []
    /// What was last said to a screen reader, for a test to read.
    property string lastAnnouncement: ""

    function announce(message: string) {
        canvas.lastAnnouncement = message;
        canvas.Accessible.announce(message);
    }

    /// The block the keyboard is on: the tile with the focus, or the one
    /// selected block while the canvas itself has it. **Not a block behind
    /// a panel's control**: a letter pressed on a slider in a settings panel
    /// is about that slider, and must not start a wire.
    function keyboardBlockId(): string {
        if (canvas.listShown) {
            const focused = sessionList.focusedId();
            if (focused !== "") {
                return focused;
            }
        }
        for (let row = 0; row < tileRepeater.count; ++row) {
            const tile = tileRepeater.itemAt(row);
            if (tile !== null && tile.activeFocus) {
                return tile.blockId;
            }
        }
        if (canvas.Window.activeFocusItem === canvas && canvas.selectedIds.length === 1) {
            return canvas.selectedIds[0];
        }
        return "";
    }

    /// Gives the keyboard back to \a blockId's tile, or its row in the list.
    function focusBlock(blockId: string) {
        if (canvas.listShown) {
            sessionList.focusRow(blockId);
            return;
        }
        for (let row = 0; row < tileRepeater.count; ++row) {
            const tile = tileRepeater.itemAt(row);
            if (tile !== null && tile.blockId === blockId) {
                tile.forceActiveFocus(Qt.TabFocusReason);
                return;
            }
        }
    }

    /// \a blockId's tile, or nothing while it has not been built.
    function tileFor(blockId: string): var {
        for (let row = 0; row < tileRepeater.count; ++row) {
            const tile = tileRepeater.itemAt(row);
            if (tile !== null && tile.blockId === blockId) {
                return tile;
            }
        }
        return null;
    }

    function blockName(blockId: string): string {
        const block = SessionModel.block(blockId);
        return (block.nickname ?? "") !== "" ? block.nickname : BlockPresentation.name(block.type ?? "");
    }

    /// An input lane, in words: the block, and which of its inputs when it
    /// has more than one.
    function laneName(blockId: string, lane: int): string {
        const count = canvas.laneCountOf(blockId, false);
        return count > 1
            ? KI18n.i18nc("@info a block's input, 'Ducking, input 2 of 2'", "%1, input %2 of %3",
                          canvas.blockName(blockId), lane + 1, count)
            : canvas.blockName(blockId);
    }

    /// The blocks in reading order: top to bottom, then left to right.
    function blocksInReadingOrder(): var {
        const ids = [];
        for (let row = 0; row < blocks.count; ++row) {
            ids.push(blocks.at(row).id);
        }
        ids.sort((a, b) => {
            // A block on its way out has no position any more.
            const first = SessionModel.block(a).position ?? Qt.point(0, 0);
            const second = SessionModel.block(b).position ?? Qt.point(0, 0);
            return first.y - second.y || first.x - second.x;
        });
        return ids;
    }

    /// C: starts a wire from the block the keyboard is on, aimed at the
    /// first input that would take it. Answers whether one was started.
    function beginKeyWiring(): bool {
        const from = canvas.keyboardBlockId();
        if (from === "") {
            return false;
        }
        const targets = [];
        if (!canvas.portsAreTheAudioSystems(from, true) && canvas.laneCountOf(from, true) > 0) {
            for (const blockId of canvas.blocksInReadingOrder()) {
                const count = canvas.laneCountOf(blockId, false);
                for (let lane = 0; lane < count; ++lane) {
                    if (canvas.laneRefusal(from, blockId, lane) === "") {
                        targets.push({blockId: blockId, lane: lane});
                    }
                }
            }
        }
        if (targets.length === 0) {
            canvas.announce(KI18n.i18nc("@info spoken: no block can take a wire from this one",
                                        "Nothing can be connected to from %1", canvas.blockName(from)));
            return true;
        }
        canvas.cancelWire();
        canvas.keySelection = canvas.selectedIds;
        canvas.keyHome = from;
        canvas.wiringFromBlock = from;
        canvas.wiringFromLane = 0;
        canvas.wiring = true;
        canvas.keyWiring = true;
        canvas.keyTargets = targets;
        canvas.forceActiveFocus(Qt.OtherFocusReason);
        canvas.aimKeyWire(0);
        return true;
    }

    /// Puts the loose end on target \a index, going round at either end.
    function aimKeyWire(index: int) {
        const count = canvas.keyTargets.length;
        canvas.keyTarget = ((index % count) + count) % count;
        const target = canvas.keyTargets[canvas.keyTarget];
        canvas.wiringOverBlock = target.blockId;
        canvas.wiringOverLane = target.lane;
        canvas.wiringEnd = canvas.lanePoint(target.blockId, target.lane, false);
        canvas.bringIntoView(target.blockId);
        canvas.announce(KI18n.i18nc("@info spoken: where a wire drawn from the keyboard would go, 'Connect to Volume, 2 of 5'",
                                    "Connect to %1, %2 of %3", canvas.laneName(target.blockId, target.lane),
                                    canvas.keyTarget + 1, count));
    }

    /// Enter: draws the wire where the loose end is.
    function finishKeyWiring(): bool {
        const target = canvas.keyTargets[canvas.keyTarget];
        const from = canvas.wiringFromBlock;
        const home = canvas.keyHome;
        canvas.cancelWire();
        const drawn = canvas.commitWire(from, "", 0, target.blockId, target.lane);
        canvas.endKeyboardEdit(home);
        canvas.announce(drawn
            ? KI18n.i18nc("@info spoken: a wire was drawn", "Connected %1 to %2", canvas.blockName(from),
                          canvas.laneName(target.blockId, target.lane))
            : KI18n.i18nc("@info spoken: the wire was refused", "Could not connect %1 to %2", canvas.blockName(from),
                          canvas.laneName(target.blockId, target.lane)));
        return drawn;
    }

    /// A wire, in words.
    function wireName(wire: var): string {
        return KI18n.i18nc("@info a wire, 'Microphone to Volume'", "%1 to %2", canvas.blockName(wire.fromBlock),
                           canvas.laneName(wire.toBlock, wire.toLane ?? 0));
    }

    /// D: goes through the wires on the block the keyboard is on, starting
    /// with the first. Answers whether the key was taken.
    function beginKeyUnwiring(): bool {
        const on = canvas.keyboardBlockId();
        if (on === "") {
            return false;
        }
        const list = [];
        for (let row = 0; row < wires.count; ++row) {
            const wire = wires.at(row);
            if (wire.fromBlock === on || wire.toBlock === on) {
                list.push(wire);
            }
        }
        if (list.length === 0) {
            canvas.announce(KI18n.i18nc("@info spoken: a block with no wires on it", "%1 has no wires",
                                        canvas.blockName(on)));
            return true;
        }
        canvas.keySelection = canvas.selectedIds;
        canvas.keyHome = on;
        canvas.keyWires = list;
        canvas.keyUnwiring = true;
        canvas.forceActiveFocus(Qt.OtherFocusReason);
        canvas.aimKeyUnwire(0);
        return true;
    }

    function aimKeyUnwire(index: int) {
        const count = canvas.keyWires.length;
        canvas.keyWire = ((index % count) + count) % count;
        const wire = canvas.keyWires[canvas.keyWire];
        canvas.selectWire(wire);
        canvas.announce(KI18n.i18nc("@info spoken: the wire in hand, 'Wire Microphone to Volume, 1 of 2'",
                                    "Wire %1, %2 of %3", canvas.wireName(wire), canvas.keyWire + 1, count));
    }

    /// Enter or Del takes the wire in hand out; Esc leaves it.
    function finishKeyUnwiring(remove: bool) {
        const home = canvas.keyHome;
        const wire = canvas.selectedWire;
        if (remove && wire !== null) {
            canvas.removeSelectedWire();
            canvas.announce(KI18n.i18nc("@info spoken: a wire was taken out", "Disconnected %1",
                                        canvas.wireName(wire)));
        } else {
            canvas.selectWire(null);
        }
        canvas.keyUnwiring = false;
        canvas.keyWires = [];
        canvas.keyWire = -1;
        canvas.endKeyboardEdit(home);
    }

    // **Tab out of it and it is off.** A wire half-aimed from a canvas
    // the keyboard has left is one nobody is aiming any more.
    Connections {
        target: canvas.Window

        function onActiveFocusItemChanged() {
            if ((canvas.keyWiring || canvas.keyUnwiring) && canvas.Window.activeFocusItem !== canvas) {
                if (canvas.keyWiring) {
                    canvas.cancelWire();
                } else {
                    canvas.selectWire(null);
                    canvas.keyUnwiring = false;
                    canvas.keyWires = [];
                    canvas.keyWire = -1;
                }
                canvas.keySelection = [];
                canvas.keyHome = "";
            }
        }
    }

    /// Puts the selection and the focus back where they were before C or D.
    function endKeyboardEdit(home: string) {
        const kept = canvas.keySelection.filter(blockId => SessionModel.block(blockId).type !== undefined);
        canvas.selectedIds = kept;
        canvas.keySelection = [];
        canvas.keyHome = "";
        canvas.focusBlock(home);
    }

    // --- what is selected, when it is a wire ---

    function isWireSelected(fromBlock: string, toBlock: string, toLane: int): bool {
        const wire = canvas.selectedWire;
        return wire !== null && wire.fromBlock === fromBlock && wire.toBlock === toBlock
            && (wire.toLane ?? 0) === toLane;
    }

    /// The wire under a point in the viewport, or nothing. **Sampled rather
    /// than solved**: what a click has to land on is the curve as it is
    /// drawn, and twenty points along it is finer than anybody can aim.
    function wireAt(x: real, y: real): var {
        const at = canvas.toSession(x, y);
        // The reach is in session pixels, so a canvas zoomed out still
        // takes a click near a wire rather than needing one on it.
        let nearest = Kirigami.Units.gridUnit / 2 / canvas.zoom;
        let best = null;
        for (let row = 0; row < wires.count; ++row) {
            const wire = wires.at(row);
            const from = canvas.lanePoint(wire.fromBlock, 0, true);
            const to = canvas.lanePoint(wire.toBlock, wire.toLane ?? 0, false);
            const k = canvas.curvature(from.x, to.x);
            for (let step = 0; step <= 20; ++step) {
                const where = canvas.wirePoint(from, to, k, step / 20);
                const distance = Math.hypot(where.x - at.x, where.y - at.y);
                if (distance < nearest) {
                    nearest = distance;
                    best = wire;
                }
            }
        }
        return best;
    }

    function selectWire(wire: var) {
        canvas.selectedWire = wire;
        if (wire !== null) {
            canvas.selectedIds = [];
        }
    }

    /// Takes the selected link out — **every channel of it, in one edit**,
    /// because what is selected is the link and half of one is not
    /// something anybody asked for. What Del does when a link is what is
    /// selected, and what its menu item does.
    function removeSelectedWire() {
        const wire = canvas.selectedWire;
        if (wire === null) {
            return;
        }
        canvas.selectedWire = null;
        // **The one link that was selected**: a source wired to both
        // of a ducker's lanes keeps the other.
        const width = canvas.laneWidthOf(wire.toBlock, false);
        SessionModel.disconnectLanes(wire.fromBlock, wire.toBlock,
                                     width > 0 ? canvas.firstPortOf(wire.toBlock, wire.toLane ?? 0, false) : -1, width);
    }

    // --- automatic connections ---

    /// Wires \a addedId up behind \a afterId, if the session's automatic
    /// connections are on. Answers whether anything was drawn.
    ///
    /// **Every channel, not the first.** A stereo block has a port a
    /// channel, and a chain wired only down the left is a chain that goes
    /// quietly mono with nothing saying why.
    ///
    /// Whether to wire at all is the design's rule: from the block that was
    /// selected, unless that block is an output — nothing comes out of the
    /// end of a chain — or the new one is a source, which takes its signal
    /// from the audio system rather than from another block. Both of those
    /// come back from laneRefusal() rather than being asked here, so that
    /// one place knows what may be wired to what.
    function autoConnect(addedId: string, afterId: string): bool {
        if (!SessionModel.autoConnect || addedId === "" || afterId === "" || addedId === afterId) {
            return false;
        }
        if (canvas.laneRefusal(afterId, addedId) !== "") {
            return false;
        }
        // Into the first lane of a block with several: the ducker's main.
        const channels = Math.min(canvas.laneChannelsOf(afterId, true), canvas.laneChannelsOf(addedId, false));
        return SessionModel.connectLanes(afterId, addedId, channels);
    }

    /// Somewhere nothing is sitting already, starting at \a wanted and
    /// moving down a row at a time. **A tile on top of another is a tile
    /// nobody can see**, and adding blocks by clicking is exactly where
    /// that happens: every one of them wants the same spot.
    function freeSpot(wanted: point, size: size): point {
        const step = size.height + Kirigami.Units.gridUnit;
        let at = Qt.point(canvas.snapped(wanted.x), canvas.snapped(wanted.y));
        for (let tries = 0; tries < 64 && canvas.isOccupied(at, size); ++tries) {
            at = Qt.point(at.x, canvas.snapped(at.y + step));
        }
        return at;
    }

    function isOccupied(at: point, size: size): bool {
        for (let row = 0; row < blocks.count; ++row) {
            const other = blocks.at(row).position;
            if (Math.abs(other.x - at.x) < size.width && Math.abs(other.y - at.y) < size.height) {
                return true;
            }
        }
        return false;
    }

    // --- the flow along a live wire ---
    //
    // **One ticker for the whole canvas** ([ADR-0009]): every wire reads the
    // same phase, so they move in step and the canvas costs one timer
    // rather than one per wire. It is also the one expensive thing drawn
    // here — animating the offset marks every live wire's path dirty — so
    // it stops whenever nobody could read it anyway.

    /// How fast the dash travels, in the phase's own units a second. The
    /// prototype's pale dash goes round its 24 pixels in 0.9 seconds.
    readonly property real flowSpeed: AppMetrics.followsSystem ? 40 : AppMetrics.flowSpeed
    /// Below this the wires are too thin to read a moving dash on, and the
    /// session it takes to fill a window at that zoom is too big to spend
    /// the frame on.
    readonly property real flowZoom: 0.75

    property real flowPhase: 0
    readonly property bool flowing: canvas.zoom >= canvas.flowZoom && canvas.Window.active
        && Meters.live && wires.count > 0

    FrameAnimation {
        running: canvas.flowing
        // The dash pattern adds up to ten, so the phase wraps at ten and
        // the dash carries on where it was rather than jumping.
        onTriggered: canvas.flowPhase = (canvas.flowPhase + canvas.flowSpeed * frameTime) % 10
    }

    // --- the dot grid ---
    //
    // **In the viewport, not in the content** ([ADR-0009]). A tiled image
    // scaled with the tiles would blur at 200 %; painted at the zoom's own
    // cell size it stays a grid of round dots at every zoom and on every
    // screen.
    Image {
        id: grid

        /// How far apart the dots are on the screen, as opposed to in the
        /// session.
        readonly property real cell: Math.max(4, canvas.gridCell * canvas.zoom)
        /// Where the dots sit in a cell: at its corner while following the
        /// system, in its middle as the prototype's radial gradient puts
        /// them.
        readonly property real phase: AppMetrics.followsSystem ? 0 : grid.cell / 2 - 1

        // Started one cell back and made two cells wider, so that the tiling
        // covers the viewport whatever the panning has left over. The
        // modulo is taken twice because a negative pan gives a negative
        // remainder, which would leave a strip with no dots in it.
        x: -grid.cell + ((canvas.originX + grid.phase) % grid.cell + grid.cell) % grid.cell
        y: -grid.cell + ((canvas.originY + grid.phase) % grid.cell + grid.cell) % grid.cell
        width: canvas.width + grid.cell * 2
        height: canvas.height + grid.cell * 2
        fillMode: Image.Tile
        source: CanvasGrid.tile(grid.cell, AppColors.grid, Screen.devicePixelRatio)
        cache: false
        Accessible.ignored: true
    }

    // --- panning, zooming and the rubber band ---
    //
    // Under the tiles, so that a press on a tile is a press on that tile.
    MouseArea {
        id: background

        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton

        property point origin: Qt.point(0, 0)
        property bool panning: false
        property bool banding: false

        onPressed: mouse => {
            canvas.forceActiveFocus();
            background.origin = Qt.point(mouse.x, mouse.y);
            if (mouse.button === Qt.RightButton) {
                const pressedWire = canvas.wireAt(mouse.x, mouse.y);
                if (pressedWire !== null) {
                    canvas.selectWire(pressedWire);
                    wireMenu.popup();
                }
                return;
            }
            if (mouse.button === Qt.MiddleButton) {
                // **The middle button pans.** The left one has to stay free
                // for the rubber band, and a modifier that has to be held
                // to move the view is a modifier somebody's other hand is
                // already using.
                background.panning = true;
                return;
            }
            // **A click near a wire is a click on it.** The wires are
            // drawn under the tiles and have no item of their own to be
            // pressed, so the canvas asks which curve the point is nearest
            // before it decides it was a click on nothing.
            const wire = canvas.wireAt(mouse.x, mouse.y);
            if (wire !== null) {
                canvas.selectWire(wire);
                return;
            }
            canvas.selectWire(null);
            // A press on nothing closes the settings: the panel belongs to
            // a block, and this press says the block is not what is wanted.
            canvas.closeSettings();
            background.banding = true;
            if ((mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) === 0) {
                canvas.clearSelection();
            }
            band.from = background.origin;
            band.to = background.origin;
        }

        onPositionChanged: mouse => {
            if (background.panning) {
                canvas.originX += mouse.x - background.origin.x;
                canvas.originY += mouse.y - background.origin.y;
                background.origin = Qt.point(mouse.x, mouse.y);
                return;
            }
            if (background.banding) {
                band.to = Qt.point(mouse.x, mouse.y);
            }
        }

        onReleased: mouse => {
            background.panning = false;
            if (!background.banding) {
                return;
            }
            background.banding = false;
            canvas.selectWithin(band.area, (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) !== 0);
            band.to = band.from;
        }

        onWheel: wheel => {
            if ((wheel.modifiers & Qt.ControlModifier) !== 0) {
                // A notch is 15 degrees, which Qt reports as 120 eighths of
                // a degree; anything finer comes from a touchpad and should
                // zoom by proportionally less.
                canvas.setZoom(canvas.zoom * Math.pow(1.25, wheel.angleDelta.y / 120), wheel.x, wheel.y);
                return;
            }
            canvas.originX += wheel.pixelDelta.x !== 0 ? wheel.pixelDelta.x : wheel.angleDelta.x / 8;
            canvas.originY += wheel.pixelDelta.y !== 0 ? wheel.pixelDelta.y : wheel.angleDelta.y / 8;
        }
    }

    /// What the rubber band covers, in viewport pixels. A band drawn
    /// upwards or to the left is the same band as one drawn the other way.
    QtObject {
        id: band

        property point from: Qt.point(0, 0)
        property point to: Qt.point(0, 0)

        readonly property rect area: Qt.rect(Math.min(band.from.x, band.to.x),
                                             Math.min(band.from.y, band.to.y),
                                             Math.abs(band.to.x - band.from.x),
                                             Math.abs(band.to.y - band.from.y))
    }

    /// Selects every tile the band touched. Touching is enough rather than
    /// containing: a band that has to swallow a tile whole is a band nobody
    /// can draw around a row of them.
    function selectWithin(area: rect, add: bool) {
        const size = canvas.tileSize();
        const found = [];
        for (let row = 0; row < blocks.count; ++row) {
            const block = blocks.at(row);
            const left = block.position.x * canvas.zoom + canvas.originX;
            const top = block.position.y * canvas.zoom + canvas.originY;
            if (left + size.width * canvas.zoom < area.x || left > area.x + area.width) {
                continue;
            }
            if (top + size.height * canvas.zoom < area.y || top > area.y + area.height) {
                continue;
            }
            found.push(block.id);
        }
        canvas.selectedIds = add ? canvas.selectedIds.concat(found.filter(id => !canvas.isSelected(id))) : found;
    }

    // --- the tiles ---

    Item {
        id: content

        // **Out of sight and out of the Tab order** while the list shows:
        // a tile that is not drawn is not a place for the keyboard to go.
        visible: !canvas.listShown
        x: canvas.originX
        y: canvas.originY
        transformOrigin: Item.TopLeft
        scale: canvas.zoom

        // **Under the tiles**, so that a wire runs behind the blocks it
        // joins rather than across their faces. A wire that crossed a
        // meter would hide the reading somebody is watching.
        Repeater {
            id: wireRepeater

            model: wires

            delegate: CanvasWire {
                id: curve

                required property string fromBlock
                required property string toBlock
                required property int toLane

                readonly property point fromEnd: canvas.lanePoint(curve.fromBlock, 0, true)
                readonly property point toEnd: canvas.lanePoint(curve.toBlock, curve.toLane, false)

                fromX: curve.fromEnd.x
                fromY: curve.fromEnd.y
                toX: curve.toEnd.x
                toY: curve.toEnd.y
                curvature: canvas.curvature(curve.fromX, curve.toX)
                live: canvas.wireIsLive(curve.fromBlock)
                flowing: canvas.flowing
                flowPhase: canvas.flowPhase
                selected: canvas.isWireSelected(curve.fromBlock, curve.toBlock, curve.toLane)
            }
        }

        // The wire being drawn, which is not a connection yet and is drawn
        // thinner so that it does not look like one.
        CanvasWire {
            id: looseWire

            visible: canvas.wiring
            provisional: true
            fromX: canvas.wiring ? canvas.lanePoint(canvas.wiringFromBlock, canvas.wiringFromLane, true).x : 0
            fromY: canvas.wiring ? canvas.lanePoint(canvas.wiringFromBlock, canvas.wiringFromLane, true).y : 0
            toX: canvas.wiringEnd.x
            toY: canvas.wiringEnd.y
            curvature: canvas.curvature(looseWire.fromX, looseWire.toX)
        }

        Repeater {
            id: tileRepeater

            model: blocks

            delegate: BlockTile {
                id: tile

                required property point position
                required property int index

                x: tile.position.x + (tile.selected && canvas.dragging ? canvas.dragX : 0)
                y: tile.position.y + (tile.selected && canvas.dragging ? canvas.dragY : 0)

                selected: canvas.isSelected(tile.blockId)
                renaming: canvas.renamingId === tile.blockId

                // **Where the ports go is the canvas's**, because the wires
                // are drawn from the session and have to meet a port
                // whether its tile has been built yet or not.
                inputCount: canvas.laneCountOf(tile.blockId, false)
                outputCount: canvas.laneCountOf(tile.blockId, true)
                portOffset: (index, count, output) => canvas.laneOffset(index, count, output,
                                                                       Qt.size(tile.width, tile.height))
                portCandidate: (index, output) => canvas.isWireCandidate(tile.blockId, index, output)
                portAimedAt: (index, output) => !output && canvas.wiringOverBlock === tile.blockId
                                             && canvas.wiringOverLane === index

                onTilePressed: modifiers => canvas.pressOn(tile.blockId, modifiers)
                onTileDragged: (dx, dy) => canvas.dragBy(dx, dy)
                onTileDropped: canvas.endDrag()
                // **A double click or Enter opens the settings; a single
                // click only selects**, which the press has already
                // done. Opening on every click put a panel up whenever
                // somebody only meant to pick a block, and took over the
                // one they had open.
                onTileActivated: canvas.activateBlock(tile.blockId)
                onRenameFinished: canvas.renamingId = ""
                onMenuRequested: {
                    canvas.pressOn(tile.blockId, Qt.NoModifier);
                    tileMenu.popup();
                }

                onPortWireStarted: (port, output) => canvas.beginWire(tile.blockId, port, output)
                onPortWireMoved: (x, y) => canvas.dragWireTo(x, y)
                onPortWireDropped: (x, y) => canvas.dropWire(x, y)
                onPortWireCancelled: canvas.cancelWire()
            }
        }
    }

    Rectangle {
        id: bandRectangle

        visible: background.banding
        x: band.area.x
        y: band.area.y
        width: band.area.width
        height: band.area.height
        color: Qt.rgba(AppColors.accentMark.r, AppColors.accentMark.g, AppColors.accentMark.b, 0.15)
        border.color: AppColors.accentMark
        border.width: 1
    }

    // --- one block's settings, beside that block ---
    //
    // **A sibling of the tiles, not a child of them.** It is placed from the
    // session in viewport pixels rather than being carried by a tile, so it
    // is not scaled with the canvas and stays the size a person can read and
    // hit at every zoom.

    // --- the session as a list ---
    //
    // **Over the tiles and under the settings panel**, which opens beside a
    // row as it does beside a tile.
    SessionList {
        id: sessionList

        anchors.fill: parent
        z: 2
        visible: canvas.listShown
        canvas: canvas
        topInset: connectionMode.height + AppMetrics.overlayInset * 2
    }

    SettingsPanel {
        id: settingsPanel

        // Above the tiles and the rubber band; the menus, which are windows
        // of their own, still come out over it.
        z: 3
        caret: settingsPanel.blockId === "" ? "" : canvas.settingsFlipped ? "right" : "left"
        maximumHeight: canvas.height - canvas.settingsMargin * 2
        maximumWidth: canvas.width - canvas.settingsMargin * 2

        // To the right of its block, or to its left when there is no room,
        // and kept inside the viewport, so that resizing the window cannot
        // leave it half outside.
        x: canvas.clampSettingsX(canvas.settingsFlipped
            ? canvas.settingsAnchor.x - canvas.settingsGap - settingsPanel.width
            : canvas.settingsAnchor.x + canvas.settingsAnchor.width + canvas.settingsGap)
        // A little above the block's own top, so the caret is level with
        // the tile's header rather than with its face.
        y: canvas.clampSettingsY(canvas.settingsAnchor.y - canvas.settingsMargin)

        onDetachToggled: canvas.detachSettings()
    }

    // **Above the docked panel**, and above one another in the order they
    // were last reached for.
    Repeater {
        id: detachedRepeater

        model: detachedModel

        delegate: SettingsPanel {
            id: floating

            required property int index
            required property string panelBlock
            required property real px
            required property real py
            required property int order

            blockId: floating.panelBlock
            detached: true
            z: 4 + floating.order
            maximumHeight: canvas.height - canvas.settingsMargin * 2
        maximumWidth: canvas.width - canvas.settingsMargin * 2
            x: canvas.clampPanelX(floating.px, floating.width)
            y: canvas.clampPanelY(floating.py, floating.height)

            onPressedOn: canvas.raiseDetached(floating.index)
            onDetachToggled: canvas.attachSettings(floating.panelBlock)
            onDraggedBy: (dx, dy) => canvas.dragDetachedBy(floating.index, dx, dy, floating.width, floating.height)
            onDragFinished: canvas.saveDetached()
            onClosed: canvas.closeDetached(floating.panelBlock)
        }
    }

    // --- what a person can do to a tile ---

    QQC2.Menu {
        id: tileMenu

        QQC2.MenuItem {
            text: KI18n.i18nc("@action:inmenu rename a block", "Rename")
            icon.name: "edit-rename"
            enabled: canvas.selectedIds.length === 1
            onTriggered: canvas.startRenaming()
        }

        QQC2.MenuItem {
            // **What it will do, not what it is.** A menu item reading
            // "Enabled" beside a tick is a question; one reading "Disable"
            // is an answer.
            text: canvas.selectionEnabled
                ? KI18n.i18nc("@action:inmenu take a block out of the signal", "Disable")
                : KI18n.i18nc("@action:inmenu put a block back into the signal", "Enable")
            icon.name: canvas.selectionEnabled ? "media-playback-pause" : "media-playback-start"
            enabled: canvas.selectedIds.length > 0
            onTriggered: canvas.setSelectionEnabled(!canvas.selectionEnabled)
        }

        QQC2.MenuItem {
            text: KI18n.i18nc("@action:inmenu take a block off the canvas", "Remove")
            icon.name: "edit-delete"
            enabled: canvas.selectedIds.length > 0
            onTriggered: canvas.removeSelected()
        }

        QQC2.MenuSeparator {}

        QQC2.MenuItem {
            text: KI18n.i18nc("@action:inmenu open a block's settings", "Settings")
            icon.name: "configure"
            enabled: canvas.selectedIds.length === 1
            // **Opens them**, rather than only saying so: the signal on
            // its own had nobody left listening once the settings moved
            // onto the canvas, and the entry did nothing.
            onTriggered: canvas.activateBlock(canvas.selectedIds[0])
        }
    }

    QQC2.Menu {
        id: wireMenu

        QQC2.MenuItem {
            text: KI18n.i18nc("@action:inmenu take a connection out", "Disconnect")
            icon.name: "edit-delete"
            enabled: canvas.selectedWire !== null
            onTriggered: canvas.removeSelectedWire()
        }
    }

    /// Whether everything selected is switched on. **All of them, not the
    /// first**: with a mixed selection the one thing that is not a surprise
    /// is to switch the rest off too.
    readonly property bool selectionEnabled: {
        // Read once at the top, so that the binding follows the session
        // even when there is nothing selected to look up.
        const revision = SessionModel.revision;
        for (const blockId of canvas.selectedIds) {
            if (SessionModel.block(blockId).enabled === false) {
                return false;
            }
        }
        return revision >= 0 && canvas.selectedIds.length > 0;
    }

    function setSelectionEnabled(enabled: bool) {
        for (const blockId of canvas.selectedIds) {
            SessionModel.setBlockEnabled(blockId, enabled);
        }
    }

    // --- dropping a block from the library ---

    DropArea {
        anchors.fill: parent
        keys: [canvas.blockTypeMimeType]

        onDropped: drop => {
            const typeId = drop.getDataAsString(canvas.blockTypeMimeType);
            if (typeId === "") {
                return;
            }
            canvas.addBlockAt(typeId, drop.x, drop.y);
            drop.acceptProposedAction();
        }
    }

    // --- what an empty canvas says ---

    PlaceholderMessage {
        anchors.centerIn: parent
        width: Math.min(parent.width - Kirigami.Units.gridUnit * 2, Kirigami.Units.gridUnit * 24)
        visible: blocks.count === 0 && !canvas.listShown
        // **What the canvas is following**, not what the window is: they
        // are the same in the window and are not in a test, and a hint that
        // told somebody to open a session they already had open would be a
        // hint about the wrong thing.
        text: SessionModel.sessionId !== ""
            ? KI18n.i18nc("@info:placeholder", "Drag a block from the list on the left, or click one to add it")
            : KI18n.i18nc("@info:placeholder", "Open or create a session to start building one")
    }

    // --- the connection mode ---
    //
    // **A mode that changes what dropping a block does is shown where the
    // block is dropped.** Whether a block added beside another is wired to
    // it by itself was a checkable item in a menu, and somebody surprised
    // by what a drop did had nowhere on the canvas to look. So the canvas
    // says which it is, in words, at its top left, and pressing it changes
    // it. It is the session's own setting, the one the menu item and the
    // Info tab already reach, so the three cannot disagree.
    //
    // Outside the zoom transform, like the zoom control, so it is the same
    // size and in the same place however far the view is zoomed or panned.
    QQC2.AbstractButton {
        id: connectionMode

        readonly property bool automatic: SessionModel.autoConnect

        anchors {
            left: parent.left
            top: parent.top
            margins: AppMetrics.overlayInset
        }
        enabled: SessionModel.sessionId !== ""
        // Over the list, which leaves room for it.
        z: 2.5
        // Not on a click: the canvas keeps the keyboard, so Del and Space
        // go on meaning what they meant before somebody pressed this.
        focusPolicy: Qt.TabFocus
        padding: Kirigami.Units.smallSpacing
        leftPadding: AppMetrics.followsSystem ? Kirigami.Units.largeSpacing : AppMetrics.rowSpacing
        rightPadding: AppMetrics.followsSystem ? Kirigami.Units.largeSpacing : AppMetrics.rowSpacing
        hoverEnabled: true

        // The prototype's chip is a control's height, whatever its words.
        Binding on implicitHeight {
            when: !AppMetrics.followsSystem
            value: AppMetrics.mediumControlHeight
        }

        text: connectionMode.automatic
            ? KI18n.i18nc("@action:button the canvas connects blocks as they are added; press to change",
                          "Automatic connections")
            : KI18n.i18nc("@action:button blocks are connected by hand; press to change",
                          "Manual connections")
        Accessible.role: Accessible.CheckBox
        Accessible.name: KI18n.i18nc("@action:button", "Automatic connections")
        Accessible.checkable: true
        Accessible.checked: connectionMode.automatic

        QQC2.ToolTip.visible: connectionMode.hovered
        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
        QQC2.ToolTip.text: connectionMode.automatic
            ? KI18n.i18nc("@info:tooltip", "A block added beside another is connected to it. Click to connect blocks by hand.")
            : KI18n.i18nc("@info:tooltip", "Blocks are connected by Ctrl-dragging between them. Click to have blocks connected as they are added.")

        onClicked: SessionModel.setAutoConnect(!connectionMode.automatic)

        background: Rectangle {
            radius: AppMetrics.controlRadius
            color: AppColors.surface
            border.width: connectionMode.visualFocus ? 2 : 1
            border.color: connectionMode.visualFocus ? AppColors.accent : AppColors.border
        }

        contentItem: Row {
            spacing: AppMetrics.followsSystem ? Kirigami.Units.smallSpacing : AppMetrics.mediumSpacing

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: AppMetrics.followsSystem ? Kirigami.Units.smallSpacing * 1.5 : AppMetrics.mediumSpacing
                height: width
                radius: width / 2
                color: connectionMode.automatic ? AppColors.okMark : AppColors.muted
            }

            QQC2.Label {
                anchors.verticalCenter: parent.verticalCenter
                text: connectionMode.text
                font: AppFonts.small
                color: connectionMode.hovered ? AppColors.text : AppColors.muted
            }
        }
    }

    // --- the zoom control ---

    QQC2.Pane {
        // Nothing to zoom while the list shows.
        visible: !canvas.listShown
        // The same distance from its corner as the connection mode is from
        // the opposite one.
        anchors {
            right: parent.right
            bottom: parent.bottom
            margins: AppMetrics.overlayInset
        }
        padding: AppMetrics.followsSystem ? Kirigami.Units.smallSpacing : AppMetrics.hairSpacing

        contentItem: Row {
            spacing: AppMetrics.followsSystem ? Kirigami.Units.smallSpacing : AppMetrics.hairSpacing

            // **In, the level, then out, in the prototype**: the
            // desktop's zoom controls run the other way, out first, and
            // following the system keeps to them.
            QQC2.ToolButton {
                readonly property bool zoomsIn: !AppMetrics.followsSystem

                icon.name: zoomsIn ? "" : "zoom-out"
                icon.source: zoomsIn ? AppGlyphs.zoomIn : ""
                display: QQC2.AbstractButton.IconOnly
                enabled: zoomsIn ? canvas.zoom < canvas.maximumZoom : canvas.zoom > canvas.minimumZoom
                Accessible.name: zoomsIn ? KI18n.i18nc("@action:button", "Zoom in")
                                         : KI18n.i18nc("@action:button", "Zoom out")
                onClicked: zoomsIn ? canvas.zoomIn() : canvas.zoomOut()

                // The prototype's 26 by 24; the desktop style's own size
                // while following the system.
                Binding on implicitWidth {
                    when: !AppMetrics.followsSystem
                    value: AppMetrics.mediumControlHeight
                }
                Binding on implicitHeight {
                    when: !AppMetrics.followsSystem
                    value: AppMetrics.smallControlHeight
                }
            }

            QQC2.Label {
                anchors.verticalCenter: parent.verticalCenter
                width: AppMetrics.followsSystem
                    ? Math.max(Kirigami.Units.gridUnit * 3, implicitWidth)
                    : Math.max(AppMetrics.zoomLabelWidth, implicitWidth + AppMetrics.mediumSpacing * 2)
                horizontalAlignment: Text.AlignHCenter
                font: AppFonts.mono
                color: AppColors.muted
                text: KI18n.i18nc("@info:status how far the canvas is zoomed in", "%1 %",
                                  Math.round(canvas.zoom * 100))
            }

            QQC2.ToolButton {
                readonly property bool zoomsIn: AppMetrics.followsSystem

                icon.name: zoomsIn ? "zoom-in" : ""
                icon.source: zoomsIn ? "" : AppGlyphs.zoomOut
                display: QQC2.AbstractButton.IconOnly
                enabled: zoomsIn ? canvas.zoom < canvas.maximumZoom : canvas.zoom > canvas.minimumZoom
                Accessible.name: zoomsIn ? KI18n.i18nc("@action:button", "Zoom in")
                                         : KI18n.i18nc("@action:button", "Zoom out")
                onClicked: zoomsIn ? canvas.zoomIn() : canvas.zoomOut()

                // The prototype's 26 by 24; the desktop style's own size
                // while following the system.
                Binding on implicitWidth {
                    when: !AppMetrics.followsSystem
                    value: AppMetrics.mediumControlHeight
                }
                Binding on implicitHeight {
                    when: !AppMetrics.followsSystem
                    value: AppMetrics.smallControlHeight
                }
            }
        }
    }

    // --- the keyboard ---
    //
    // On the canvas rather than on each tile: a key that was not handled by
    // the tile that has the focus comes up here, so Del and Esc work
    // wherever the focus is inside the canvas.

    // **C and D** start a wire from the keyboard, and while one is under
    // way the arrows, Enter, Del and Esc are about it and nothing else.
    // Pressed on a tile, they come up here because the tile does
    // not take them.
    Keys.onPressed: event => {
        if (canvas.keyWiring || canvas.keyUnwiring) {
            const step = event.key === Qt.Key_Right || event.key === Qt.Key_Down ? 1
                : event.key === Qt.Key_Left || event.key === Qt.Key_Up ? -1 : 0;
            if (step !== 0) {
                if (canvas.keyWiring) {
                    canvas.aimKeyWire(canvas.keyTarget + step);
                } else {
                    canvas.aimKeyUnwire(canvas.keyWire + step);
                }
                event.accepted = true;
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                if (canvas.keyWiring) {
                    canvas.finishKeyWiring();
                } else {
                    canvas.finishKeyUnwiring(true);
                }
                event.accepted = true;
            } else if (event.key === Qt.Key_Delete && canvas.keyUnwiring) {
                canvas.finishKeyUnwiring(true);
                event.accepted = true;
            }
            return;
        }
        if (event.modifiers !== Qt.NoModifier || canvas.renamingId !== "") {
            return;
        }
        if (event.key === Qt.Key_C) {
            event.accepted = canvas.beginKeyWiring();
        } else if (event.key === Qt.Key_D) {
            event.accepted = canvas.beginKeyUnwiring();
        }
    }

    Keys.onDeletePressed: canvas.removeSelected()

    Keys.onEscapePressed: {
        if (canvas.keyWiring) {
            const home = canvas.keyHome;
            canvas.cancelWire();
            canvas.endKeyboardEdit(home);
            canvas.announce(KI18n.i18nc("@info spoken: the wire drawn from the keyboard was abandoned", "Not connected"));
            return;
        }
        if (canvas.keyUnwiring) {
            canvas.finishKeyUnwiring(false);
            return;
        }
        if (canvas.wiring) {
            canvas.cancelWire();
            return;
        }
        if (canvas.dragging) {
            canvas.cancelDrag();
            return;
        }
        if (settingsPanel.blockId !== "") {
            canvas.closeSettings();
            return;
        }
        canvas.renamingId = "";
        canvas.selectWire(null);
        canvas.clearSelection();
    }
}
