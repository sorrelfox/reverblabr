// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.ki18n

// One block on the canvas.
//
// **A tile is glanced at, not read.** Somebody looking at it is listening to
// something else, so the three things it has to answer without being studied
// are which block this is, whether it is doing anything, and what it reads.
// That is the header, the state dot and the face, in that order of size.
//
// **It tells the canvas what happened; it decides nothing.** Which tiles are
// selected, where a drag ends up and what Del removes are the canvas's to
// know, because they are about more than one tile. The tile reports a press,
// a drag and a double click and lets the canvas answer.
//
// **The words are never the engine's.** The name, the description and the
// icon come from the presentation table, keyed by the block's type id; the
// engine has no idea what language anybody speaks.
Item {
    id: tile

    /// The block this tile is. Everything else about it is looked up.
    required property string blockId
    required property string typeId
    /// What the user called it, or empty for the block's own name.
    required property string nickname
    /// False means bypassed, which the graph cross-fades. **Not called
    /// `enabled`**: that is QQuickItem's own property, and a tile that
    /// stopped taking clicks when its block was bypassed could not be
    /// switched back on from the canvas.
    required property bool blockEnabled
    /// Set when this build has no such block, which is what a session
    /// written by a later version leads to.
    required property bool unknownType

    /// Whether the canvas has this tile in its selection.
    property bool selected: false
    /// Whether a rename is being typed. The canvas turns it on; the field
    /// turns it off when the typing is over, whichever way it ended.
    property bool renaming: false

    /// What the block is called: what somebody typed, or its type's own
    /// name.
    readonly property string displayName: tile.nickname !== ""
        ? tile.nickname
        : BlockPresentation.name(tile.typeId)
    readonly property string category: BlockPresentation.category(tile.typeId)

    /// What the catalogue says this kind of block is. Read through `ready`
    /// so that a tile drawn before the engine has answered draws itself
    /// again when it does.
    readonly property var typeInfo: BlockCatalogue.ready ? BlockCatalogue.describe(tile.typeId) : ({})
    /// What the block asked its tile to show, which is all the canvas needs
    /// to know about a block it has never heard of.
    readonly property string faceKind: tile.typeInfo.face ?? "icon"
    /// How many ports it has and what they are called. A block whose shape
    /// follows what is connected to it shows the fewest it can have, which
    /// is what it has until something is connected. **The canvas counts
    /// them**, because a wire has to be drawn to a port whether the
    /// tile has been built yet or not; on its own a tile shows the fewest.
    ///
    /// **Only the ports a wire can reach**. A capture block's inputs
    /// are filled by the application or device it follows and an output
    /// block's outputs belong to the device, so neither is somewhere a wire
    /// goes: drawing a dot there is drawing somewhere a wire goes and never
    /// does. The block says which those are, so a tile is right
    /// about it wherever it is drawn — on the canvas, in the library, or on
    /// its own in a test.
    ///
    /// **Connectors, not ports**. A link carries a stereo signal, so
    /// a tile shows one connector on an edge and not one a channel; on its
    /// own, a block with any ports on a side has one.
    property int inputCount: tile.typeInfo.inputs?.audioSystem === true
        ? 0
        : ((tile.typeInfo.inputs?.minimum ?? 0) > 0
            ? ((tile.typeInfo.inputs?.laneWidth ?? 0) > 0
                ? Math.ceil(tile.typeInfo.inputs.minimum / tile.typeInfo.inputs.laneWidth)
                : 1)
            : 0)
    property int outputCount: tile.typeInfo.outputs?.audioSystem === true
        ? 0
        : ((tile.typeInfo.outputs?.minimum ?? 0) > 0 ? 1 : 0)

    /// Where a port sits on the tile's edge, in the tile's own
    /// coordinates. **The canvas's arithmetic, handed down**: the wires are
    /// drawn from the session rather than from the tiles, so one of them
    /// has to own where a port is, and it cannot be the tile. On its own a
    /// tile puts every port at the middle of its edge.
    property var portOffset: (index, count, output) => Qt.point(output ? tile.width : 0, tile.height / 2)
    /// Whether a wire being drawn could be dropped on one of its ports, and
    /// which port it is over. The canvas knows; a tile has no idea what
    /// else is in the session.
    property var portCandidate: (index, output) => false
    property var portAimedAt: (index, output) => false
    /// What connector \a index is for, on an edge with more than one lane,
    /// from the lane ids the catalogue gives. Empty otherwise.
    property var portName: (index, output) => {
        const side = output ? tile.typeInfo.outputs : tile.typeInfo.inputs;
        const lanes = side?.lanes ?? [];
        return (side?.laneWidth ?? 0) > 0 && index < lanes.length ? BlockPresentation.laneName(lanes[index]) : "";
    }

    /// Every wire with this block at either end. **What lets a tile say
    /// what it is joined to** — which the first tiles could not, because
    /// the session model had no wires in it to read.
    readonly property var connections: SessionModel.revision >= 0
        ? SessionModel.wiresTouching(tile.blockId)
        : []

    /// A Ctrl-drag on one of the tile's ports, on its way to the canvas.
    /// The coordinates are the scene's.
    signal portWireStarted(int port, bool output)
    signal portWireMoved(real sceneX, real sceneY)
    signal portWireDropped(real sceneX, real sceneY)
    signal portWireCancelled()

    /// Whether audio is going through it at this moment.
    readonly property var levels: Meters.frame >= 0 ? Meters.levels(tile.blockId) : ({})
    readonly property bool live: Meters.live && (tile.levels.active ?? false)
    /// Whether the tile *shows* it live: a bypassed block is off, whatever
    /// its meter reads. **The state dot and every connector read this one
    /// value**, so they cannot disagree.
    readonly property bool showsLive: tile.blockEnabled && tile.live
    /// What the engine last said went wrong here, or empty. **An error that
    /// names a block belongs on that block's tile**, where the thing that
    /// went wrong is.
    readonly property string badge: ErrorSurface.flaggedBlocks.indexOf(tile.blockId) >= 0
        ? ErrorSurface.badge(tile.blockId)
        : ""

    /// **Which ear the block works on** — "left" or "right" — or nothing
    /// for a block without an Ear. The session's setting, or the
    /// block's own default while nobody has set it: a block added for the
    /// left ear says so before anything is changed.
    readonly property string ear: {
        const described = BlockCatalogue.ready ? BlockCatalogue.describeParam(tile.typeId, "ear") : ({});
        if (described.id === undefined) {
            return "";
        }
        const set = SessionModel.revision >= 0 ? SessionModel.param(tile.blockId, "ear") : undefined;
        return set === undefined || set === null || String(set) === "" ? String(described["default"] ?? "")
                                                                         : String(set);
    }

    /// The parts, for a test and for whatever has to reach into a tile it
    /// did not write.
    readonly property alias earBadge: earBadge
    readonly property alias face: faceLoader
    readonly property alias header: header
    readonly property alias headerRule: headerRule
    readonly property alias stateDot: stateDot
    readonly property alias selectionHalo: selectionHalo
    readonly property alias focusRing: focusRing
    readonly property alias nameField: renameField
    readonly property alias inputPorts: inputPortRepeater
    readonly property alias outputPorts: outputPortRepeater

    /// A press, with the modifiers that were held: the canvas decides what
    /// that means for the selection.
    signal tilePressed(int modifiers)
    /// How far the pointer has come since the press, in canvas pixels.
    signal tileDragged(real dx, real dy)
    /// The button came up after a drag.
    signal tileDropped()
    /// The button came up and the press never became a drag: a click.
    ///
    /// **Separate from the press**, because a press is what decides
    /// the selection and has to act at once, while a click must not fire
    /// for a gesture that turned out to be a drag. Only the left button,
    /// and only when nothing moved. The canvas does nothing more with it
    /// than the press already did: a click selects, and a double click or
    /// Enter opens the settings.
    signal tileClicked()
    /// A double click or Enter: open the settings panel.
    signal tileActivated()
    /// A right click.
    signal menuRequested()
    /// The rename ended, whichever way: the canvas is the one that knows a
    /// rename was under way, so it is the one that has to be told it is over.
    signal renameFinished()

    // The design's 132 by 96: wide enough for a name that is two
    // words and tall enough for a meter with a reading under it.
    implicitWidth: AppMetrics.tileOuterWidth
    implicitHeight: AppMetrics.tileOuterHeight
    width: implicitWidth
    height: implicitHeight

    activeFocusOnTab: true

    // **One button, whatever is drawn on it.** Everything inside the tile is
    // a picture of the block; what a screen reader has to be able to say is
    // which block it is and what it is doing, and what pressing it does.
    Accessible.role: Accessible.Button
    Accessible.name: tile.ear === ""
        ? KI18n.i18nc("@info:whatsthis a block tile: what it is and whether it is running",
                      "%1, %2", tile.displayName, tile.stateText)
        : KI18n.i18nc("@info:whatsthis a block tile for one ear: what it is, which ear, and whether it is running",
                      "%1, %2, %3", tile.displayName, tile.earText, tile.stateText)
    /// The ear, in words, for a screen reader.
    readonly property string earText: tile.ear === "right"
        ? KI18n.i18nc("@info the block works on the right ear", "right ear")
        : tile.ear === "left" ? KI18n.i18nc("@info the block works on the left ear", "left ear") : ""
    // **What it is joined to, not only what it is.** Somebody who cannot
    // see the canvas cannot see the wires either, and a block's place in
    // the signal is most of what there is to know about it.
    Accessible.description: tile.badge !== ""
        ? tile.badge
        : KI18n.i18nc("@info:whatsthis a block tile: what it does, then what it is wired to",
                      "%1 %2", BlockPresentation.description(tile.typeId), tile.connectionText)
    Accessible.focusable: true
    Accessible.onPressAction: tile.tileActivated()

    /// How late the block makes the sound, in milliseconds, while it runs:
    /// a limiter's lookahead, a plugin's own. Nought for most.
    readonly property real latencyMs: tile.levels.latencyMs ?? 0
    /// The same in words, or nothing when there is none.
    readonly property string latencyText: tile.latencyMs > 0
        ? KI18n.i18nc("@info:tooltip how much later a block makes the sound, 'Adds 1.5 ms'", "Adds %1 ms",
                      Math.round(tile.latencyMs * 10) / 10)
        : ""

    /// What a face has to say beyond the line it shows, or nothing — and
    /// how late the block makes the sound, where it does.
    function detailOf(face: var): string {
        const detail = face?.detail ?? "";
        if (tile.latencyText === "") {
            return detail;
        }
        return detail === "" ? tile.latencyText
                             : KI18n.i18nc("@info:tooltip a face's detail, then the block's latency", "%1\n%2",
                                           detail, tile.latencyText);
    }

    /// What somebody calls the block \a blockId, for the sentence below.
    function nameOf(blockId: string): string {
        const block = SessionModel.block(blockId);
        return (block.nickname ?? "") !== "" ? block.nickname : BlockPresentation.name(block.type ?? "");
    }

    /// The wires, in words. **Names, not port numbers**: which port of a
    /// stereo pair a wire lands on is not what somebody needs to hear, and
    /// hearing it twice for every block would bury what they do need.
    readonly property string connectionText: {
        const feeding = [];
        const fed = [];
        for (const wire of tile.connections) {
            const other = wire.toBlock === tile.blockId ? wire.fromBlock : wire.toBlock;
            const names = wire.toBlock === tile.blockId ? fed : feeding;
            const name = tile.nameOf(other);
            if (names.indexOf(name) < 0) {
                names.push(name);
            }
        }
        if (fed.length === 0 && feeding.length === 0) {
            return KI18n.i18nc("@info a block with no wires on it", "Not connected");
        }
        if (feeding.length === 0) {
            return KI18n.i18nc("@info what feeds a block", "Fed by %1", fed.join(", "));
        }
        if (fed.length === 0) {
            return KI18n.i18nc("@info what a block feeds", "Feeding %1", feeding.join(", "));
        }
        return KI18n.i18nc("@info what feeds a block and what it feeds",
                           "Fed by %1, feeding %2", fed.join(", "), feeding.join(", "));
    }

    /// The state the dot draws, in words. **Three states, not two**: a block
    /// that is switched on and a block with sound going through it look
    /// different and are different, and "on" would flatten them together.
    readonly property string stateText: !tile.blockEnabled
        ? KI18n.i18nc("@info a block that is bypassed", "Off")
        : tile.live
            ? KI18n.i18nc("@info a block with audio going through it", "Live")
            : KI18n.i18nc("@info a block that is switched on", "On")

    // **The field is filled and focused when the rename starts**, not when
    // the tile is built: a TextField that exists on every tile from the
    // beginning is thirty text fields in the Tab order of a session with
    // thirty blocks in it.
    onRenamingChanged: {
        if (!tile.renaming) {
            return;
        }
        renameField.text = tile.nickname;
        renameField.forceActiveFocus();
        renameField.selectAll();
    }

    // **Selected is a halo outside the tile, not a thicker border**:
    // the prototype's `box-shadow: 0 0 0 2px var(--accent-soft)`. A border
    // that grew when the tile was selected moved everything inside it in by
    // a pixel each side, and back again when it was deselected.
    Rectangle {
        id: selectionHalo

        anchors.fill: parent
        anchors.margins: -AppMetrics.selectionHaloWidth
        visible: tile.selected
        radius: AppMetrics.tileRadius + AppMetrics.selectionHaloWidth
        color: "transparent"
        border.width: AppMetrics.selectionHaloWidth
        border.color: AppColors.accentSoft
        Accessible.ignored: true
    }

    Rectangle {
        id: body

        anchors.fill: parent
        radius: AppMetrics.tileRadius
        color: AppColors.surface
        // **Selected is a border's colour, not a fill.** The face is the
        // point of the tile and a wash of colour over it would hide the
        // reading somebody selected the tile to look at. **One width in
        // every state**, so that nothing inside it moves.
        border.color: tile.selected ? AppColors.accentMark : AppColors.border
        border.width: 1
        // A bypassed block is still there and still says what it is set to;
        // it is just not in the signal any more.
        opacity: tile.blockEnabled ? 1.0 : 0.55

        // **Two parts, and they look like two**: a header saying
        // which block this is, a rule, and under it the face with the whole
        // width of the tile to itself. The face used to share a margin with
        // the header all the way round, and the name and the reading ran
        // together into one heap with less room than the face was drawn
        // for. Inset by the border only, so the rule meets it at both ends.
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: body.border.width
            spacing: 0

            // --- header: which block, what it is called, what it is doing ---
            RowLayout {
                id: header

                Layout.fillWidth: true
                // The design's 24 pixels, and more only while the rename
                // field is up, which is taller than a line of text.
                Layout.preferredHeight: Math.max(AppMetrics.tileBandHeight, header.implicitHeight)
                // **The band keeps its height**, and the face gives way: a
                // face asks for more than a tile has, and the layout took
                // the pixel it was short from the header too. Minus
                // one is the layout's own minimum, as while following the
                // system it always was.
                Layout.minimumHeight: AppMetrics.followsSystem ? -1 : AppMetrics.tileBandHeight
                Layout.leftMargin: AppMetrics.tileHeaderInset - body.border.width
                Layout.rightMargin: AppMetrics.tileHeaderInset - body.border.width
                spacing: AppMetrics.tileHeaderSpacing

                // **A hand over the part a block is carried by**, and
                // the ordinary arrow over the face, whose controls are for
                // working rather than moving.
                HoverHandler {
                    cursorShape: pointer.moving ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                }

                Kirigami.Icon {
                    source: BlockPresentation.icon(tile.typeId)
                    // **The one thing on the tile that says which kind of
                    // block this is** without being read, which is why the
                    // category's colour goes here and nowhere else.
                    color: AppColors.categoryTint(tile.category)
                    isMask: true
                    implicitWidth: AppMetrics.tileIconSize
                    implicitHeight: AppMetrics.tileIconSize
                    Accessible.ignored: true
                }

                // **Which ear, beside what kind**: L or R on the ear's
                // own colour, as an audiogram draws them — blue for the left,
                // red for the right. The letter is always there, so the
                // colour is never the only thing saying which.
                Rectangle {
                    id: earBadge

                    readonly property string letter: tile.ear === "right"
                        ? KI18n.i18nc("@label:chip short for the right ear, on a block's tile", "R")
                        : KI18n.i18nc("@label:chip short for the left ear, on a block's tile", "L")

                    visible: tile.ear !== ""
                    implicitHeight: AppMetrics.tileIconSize
                    implicitWidth: Math.max(implicitHeight, earLetter.implicitWidth + Kirigami.Units.smallSpacing)
                    radius: AppMetrics.smallRadius
                    color: tile.ear === "right" ? AppColors.earRight : AppColors.earLeft
                    Accessible.ignored: true

                    QQC2.Label {
                        id: earLetter

                        anchors.centerIn: parent
                        text: earBadge.letter
                        color: AppColors.inkOnEar
                        font: AppFonts.tileCaption
                        Accessible.ignored: true
                    }
                }

                QQC2.Label {
                    id: nameLabel

                    visible: !tile.renaming
                    Layout.fillWidth: true
                    text: tile.displayName
                    color: AppColors.text
                    font: AppFonts.tileTitle
                    // Elided rather than clipped, and the whole of it in the
                    // tooltip: a tile is 132 pixels wide and a name is
                    // whatever somebody typed.
                    elide: Text.ElideRight
                    QQC2.ToolTip.text: tile.displayName
                    QQC2.ToolTip.visible: hoverHandler.hovered && nameLabel.truncated
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    Accessible.ignored: true

                    HoverHandler {
                        id: hoverHandler
                    }
                }

                QQC2.TextField {
                    id: renameField

                    visible: tile.renaming
                    Layout.fillWidth: true
                    font: AppFonts.small
                    placeholderText: BlockPresentation.name(tile.typeId)
                    Accessible.name: KI18n.i18nc("@label:textbox", "Block name")
                    // On editing finished rather than on every keystroke: a
                    // name half typed is not a name, and every letter would
                    // be an edit the engine has to undo separately.
                    onEditingFinished: {
                        SessionModel.renameBlock(tile.blockId, text);
                        tile.renameFinished();
                    }
                    // **Escape puts back what was there**, which is the
                    // whole reason a rename can be started by accident
                    // without costing anything.
                    Keys.onEscapePressed: tile.renameFinished()
                }

                // A block this build does not have, kept exactly as the file
                // held it. **Flagged, never dropped**: opening and saving a
                // session must not lose anything.
                Kirigami.Icon {
                    visible: tile.unknownType
                    source: "dialog-question"
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: Kirigami.Units.iconSizes.small
                    QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip", "This build has no block of this kind")
                    QQC2.ToolTip.visible: unknownHover.hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    Accessible.ignored: true

                    HoverHandler {
                        id: unknownHover
                    }
                }

                Kirigami.Icon {
                    visible: tile.badge !== ""
                    source: "dialog-warning"
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: Kirigami.Units.iconSizes.small
                    QQC2.ToolTip.text: tile.badge
                    QQC2.ToolTip.visible: badgeHover.hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    Accessible.ignored: true

                    HoverHandler {
                        id: badgeHover
                    }
                }

                Rectangle {
                    id: stateDot

                    implicitWidth: AppMetrics.stateDotSize
                    implicitHeight: implicitWidth
                    radius: width / 2
                    color: !tile.blockEnabled
                        ? AppColors.muted
                        : tile.showsLive ? AppColors.liveMark : AppColors.okMark
                    Accessible.ignored: true
                }
            }

            Rectangle {
                id: headerRule

                Layout.fillWidth: true
                implicitHeight: 1
                color: AppColors.border
            }

            // --- face: what the block has to say for itself ---
            Loader {
                id: faceLoader

                Layout.fillWidth: true
                Layout.fillHeight: true

                // **Whatever the face left off, where there is room for it**.
                // A face shows one thing; the rest of what it knows
                // — that a device is the default one, how many streams an
                // application has going — is here, for a face that has
                // more to say than the line it shows.
                // Asked of whatever face is there: which face that is is
                // the register's business, and most of them have nothing
                // to add.
                readonly property string detail: tile.detailOf(faceLoader.item)

                QQC2.ToolTip.text: faceLoader.detail
                QQC2.ToolTip.visible: faceHover.hovered && faceLoader.detail !== "" && !pointer.pressed
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

                HoverHandler {
                    id: faceHover
                }
                // **A block with no face of its own is not a lesser block.**
                // What it asked for — a meter, a number, a line of text — is
                // drawn from what it already says about itself.
                sourceComponent: BlockFaces.faceFor(tile.typeId) ?? genericFace
            }

            // **A face from the register is told which block it is.** The
            // component was created in the register's context and cannot see
            // this tile's properties, so the one thing it needs is handed to
            // it afterwards.
            Binding {
                target: faceLoader.item
                property: "blockId"
                value: tile.blockId
                when: faceLoader.item !== null
            }

            Component {
                id: genericFace

                GenericFace {
                    kind: tile.faceKind
                }
            }
        }
    }

    // --- ports: where wires meet the tile ---
    //
    // The counts come from the catalogue, so a block the canvas has never
    // heard of still gets the right number of them, and a block whose shape
    // follows what is connected to it grows a port when a wire asks for
    // one. **Where each one sits is the canvas's**: the wires are
    // drawn from the session rather than from the tiles, because a wire has
    // to meet a port whether that tile has been built yet or not, so a port
    // that placed itself to its own taste would be one the wires missed.

    Repeater {
        id: inputPortRepeater

        model: tile.inputCount

        delegate: BlockPort {
            required property int index

            // **Above the tile's own pointer**, which fills the tile and
            // would otherwise swallow a Ctrl-press on the half of the dot
            // that is inside the edge.
            z: 2
            attachTo: tile.portOffset(index, tile.inputCount, false)
            candidate: tile.portCandidate(index, false)
            aimedAt: tile.portAimedAt(index, false)
            live: tile.showsLive
            name: tile.portName(index, false)
            // **Links arriving, not wires.** The two channels of one link
            // are one thing arriving; a lane is summing when two different
            // blocks feed it — **this lane**, not the block.
            merging: {
                const width = tile.typeInfo.inputs?.laneWidth ?? 0;
                const first = index * width;
                const sources = [];
                for (const wire of tile.connections) {
                    const inLane = width === 0 || (wire.toPort >= first && wire.toPort < first + width);
                    if (wire.toBlock === tile.blockId && inLane && sources.indexOf(wire.fromBlock) < 0) {
                        sources.push(wire.fromBlock);
                    }
                }
                return sources.length > 1;
            }

            onWireStarted: tile.portWireStarted(index, false)
            onWireMoved: (x, y) => tile.portWireMoved(x, y)
            onWireDropped: (x, y) => tile.portWireDropped(x, y)
            onWireCancelled: tile.portWireCancelled()
        }
    }

    Repeater {
        id: outputPortRepeater

        model: tile.outputCount

        delegate: BlockPort {
            required property int index

            z: 2
            output: true
            attachTo: tile.portOffset(index, tile.outputCount, true)
            candidate: tile.portCandidate(index, true)
            aimedAt: tile.portAimedAt(index, true)
            live: tile.showsLive
            name: tile.portName(index, true)

            onWireStarted: tile.portWireStarted(index, true)
            onWireMoved: (x, y) => tile.portWireMoved(x, y)
            onWireDropped: (x, y) => tile.portWireDropped(x, y)
            onWireCancelled: tile.portWireCancelled()
        }
    }

    /// Whether the focus came from a press rather than the keyboard.
    property bool focusedByPointer: false

    onActiveFocusChanged: if (!tile.activeFocus) {
        tile.focusedByPointer = false;
    }

    // **The focus ring is drawn over everything**, including the face, so
    // that a tile reached with Tab is obvious whatever it happens to be
    // showing at the time. **Not for a click**: the click has just
    // selected the tile, whose halo says where it is, and the two drew two
    // rings round one tile.
    Rectangle {
        id: focusRing

        anchors.fill: parent
        anchors.margins: -Kirigami.Units.smallSpacing / 2
        visible: tile.activeFocus && !tile.focusedByPointer
        color: "transparent"
        radius: AppMetrics.tileRadius
        border.color: AppColors.accentMark
        border.width: 1
    }

    // **Behind everything on the tile, not over it**. Over it, it
    // took every press first, so the controls a face draws — the level
    // sliders on Volume and Wave, the clip light on a meter — were drawn and
    // could not be used. Behind, a control takes its own presses and this
    // gets the rest: the header, the face's empty parts, the border.
    MouseArea {
        id: pointer

        anchors.fill: parent
        z: -1
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        // The rename field is a control in its own right; a press meant for
        // it is not a press on the tile.
        enabled: !tile.renaming

        /// Where the press landed, in the canvas's coordinates rather than
        /// the tile's. **The tile moves while it is dragged**, so a delta
        /// measured against the tile itself would be a delta of nearly
        /// nothing however far the pointer went.
        property point origin: Qt.point(0, 0)
        property bool moving: false
        /// Whether the press was on the header, which is **the only part a
        /// block is carried by**. A press anywhere else still selects
        /// it, and a double click there still opens its settings, but a
        /// drag that starts on the face is somebody reaching for the face.
        property bool fromHeader: false

        onPressed: mouse => {
            tile.focusedByPointer = true;
            tile.forceActiveFocus();
            if (mouse.button === Qt.RightButton) {
                tile.menuRequested();
                return;
            }
            pointer.origin = pointer.mapToItem(tile.parent, mouse.x, mouse.y);
            pointer.moving = false;
            // Everything above the rule, margins and all: the band a person
            // sees as the header, not only the row laid out inside it.
            pointer.fromHeader = mouse.y < headerRule.mapToItem(pointer, 0, 0).y;
            tile.tilePressed(mouse.modifiers);
        }

        onPositionChanged: mouse => {
            if (!pointer.fromHeader || (mouse.buttons & Qt.LeftButton) === 0) {
                return;
            }
            const now = pointer.mapToItem(tile.parent, mouse.x, mouse.y);
            const dx = now.x - pointer.origin.x;
            const dy = now.y - pointer.origin.y;
            // **Not every press with a twitch in it is a drag.** Below the
            // platform's threshold a click is a click, which is what makes
            // selecting a tile possible with a hand that is not steady.
            if (!pointer.moving && Math.hypot(dx, dy) < Application.styleHints.startDragDistance) {
                return;
            }
            pointer.moving = true;
            tile.tileDragged(dx, dy);
        }

        onReleased: mouse => {
            if (mouse.button !== Qt.LeftButton) {
                return;
            }
            if (!pointer.moving) {
                tile.tileClicked();
                return;
            }
            pointer.moving = false;
            tile.tileDropped();
        }

        onDoubleClicked: tile.tileActivated()
    }

    // Enter opens the settings panel, the same as a double click: the tile
    // is a button, and this is what pressing it does.
    Keys.onReturnPressed: tile.tileActivated()
    Keys.onEnterPressed: tile.tileActivated()
}
