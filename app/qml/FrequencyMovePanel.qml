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
import QtQuick.Shapes
import org.kde.kirigami as Kirigami
import org.kde.ki18n

// The settings of a Frequency Lowering or Frequency Raising
// block: which one is typeId; FrequencyLoweringPanel and
// FrequencyRaisingPanel set it.
//
// **Two ways to see the moves.** Arrows (the first): a frequency axis with
// each move's source range shaded, as what the ear does not use, its
// destination outlined, and an arrow from one to the other; the picked
// move's four ends can be dragged. Behind them, while the session runs, the
// sound before the block and after it, on the block's own ear. Map:
// the frequency that goes in, across, against the one that comes out, up —
// the diagonal is sound left where it was, a squeezed move bends it, and a
// copied one adds a branch beside it.
//
// **The destination stays on the block's side of its source**, below it for
// a lowering and above it for a raising: an end dragged or slid into the
// other range stops at its edge.
//
// **Moved sound only** plays what the moves add and nothing else, to set a
// move's level by ear.
ColumnLayout {
    id: panel

    /// Which block this is the panel of.
    property alias typeId: moves.typeId
    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""
    /// The move being edited: a slot, or -1.
    property int selected: -1
    /// "arrows" or "map".
    property string view: "arrows"
    readonly property bool showingMap: panel.view === "map"
    /// Which of the picked move's ends a drag is moving — "SourceLow",
    /// "SourceHigh", "DestinationLow", "DestinationHigh" — or "".
    property string dragging: ""

    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(panel.blockId) : ({})
    readonly property color tint: AppColors.categoryTint(BlockPresentation.category(moves.typeId))
    /// The ear, as the spectrum's channel: left 0, right 1.
    readonly property string ear: panel.params.ear
        ?? (BlockCatalogue.ready ? BlockCatalogue.describeParam(moves.typeId, "ear").default : "left")
    readonly property int channel: panel.ear === "right" ? 1 : 0
    readonly property bool movedOnly: panel.params.movedOnly === true
    readonly property real panelWidth: AppMetrics.followsSystem ? Kirigami.Units.gridUnit * 24 : 420
    readonly property real grabRadius: Kirigami.Units.gridUnit * 0.75
    /// Neither range may be narrower than this, as a ratio: a sixth of an
    /// octave.
    readonly property real narrowest: Math.pow(2, 1 / 6)
    /// Which moves are moving sound now (the block's band values).
    readonly property var movingNow: Meters.frame >= 0 && Meters.live ? Meters.bandValues(panel.blockId) : []
    readonly property var picked: panel.selected >= 0 ? moves.move(panel.selected) : null

    /// The parts a test drives, as a person would.
    readonly property alias moves: moves
    readonly property alias words: words
    readonly property alias plotArea: plotArea
    readonly property alias chips: chipRepeater
    readonly property alias addButton: addControl
    readonly property alias deleteButton: deleteControl
    readonly property alias arrowsButton: arrowsChoice
    readonly property alias mapButton: mapChoice
    readonly property alias movedOnlySwitch: movedOnlyControl
    readonly property alias onSwitch: onControl
    readonly property alias methodChoices: methodRepeater
    readonly property alias sourceSlider: sourceControl
    readonly property alias destinationSlider: destinationControl
    readonly property alias levelSlider: levelControl
    readonly property alias sourceChoices: sourceRepeater
    readonly property alias whenChoices: whenRepeater
    readonly property alias inputSpectrumShape: inputShape
    readonly property alias outputSpectrumShape: outputShape
    readonly property alias mapShape: mapView

    spacing: Kirigami.Units.largeSpacing

    onBlockIdChanged: {
        panel.selected = -1;
        Qt.callLater(panel.selectSomething);
        panel.askForSpectrum();
    }
    onChannelChanged: panel.askForSpectrum()
    onVisibleChanged: panel.askForSpectrum()
    Component.onCompleted: panel.askForSpectrum()
    Component.onDestruction: panel.letGoOfSpectrum()

    /// Asks for this block's spectra, on its own ear, while the panel is
    /// shown, and lets go of them while it is not.
    function askForSpectrum() {
        if (panel.visible && panel.blockId !== "") {
            Meters.analyse(panel.blockId, panel.channel);
        } else {
            panel.letGoOfSpectrum();
        }
    }

    /// Lets go only of this block's: another panel may have asked since.
    function letGoOfSpectrum() {
        if (panel.blockId !== "" && Meters.analysed() === panel.blockId) {
            Meters.analyse("");
        }
    }

    function selectSomething() {
        if (moves.used.indexOf(panel.selected) < 0) {
            panel.selected = moves.used.length > 0 ? moves.used[0] : -1;
        }
    }

    Connections {
        target: moves

        function onUsedChanged() {
            panel.selectSomething();
        }
    }

    /// How far along the block's range \a hz is, from 0 to 1, in octaves;
    /// and back. The plot and the range sliders share them.
    function along(hz: real): real {
        return Math.log(hz / moves.lowestHz) / Math.log(moves.highestHz / moves.lowestHz);
    }
    function hzAlong(fraction: real): real {
        return moves.lowestHz * Math.pow(moves.highestHz / moves.lowestHz, Math.max(0, Math.min(1, fraction)));
    }
    /// Where \a hz sits across the plot, and back.
    function xOf(hz: real): real {
        return plotArea.width * panel.along(hz);
    }
    function hzAt(x: real): real {
        return panel.hzAlong(x / Math.max(1, plotArea.width));
    }
    /// Where \a hz sits up the map, the lowest at the bottom.
    function yOf(hz: real): real {
        return plotArea.height * (1 - Math.log(hz / moves.lowestHz) / Math.log(moves.highestHz / moves.lowestHz));
    }

    /// What end \a which of move \a slot may be set to: inside the block's
    /// range, its range no narrower than a sixth of an octave, and the
    /// destination below the source for a lowering, above it for a raising.
    function limited(slot: int, which: string, hz: real): real {
        const move = moves.move(slot);
        let low = moves.lowestHz;
        let high = moves.highestHz;
        switch (which) {
        case "SourceLow":
            if (!moves.raises) {
                low = move.destinationHigh;
            }
            high = move.sourceHigh / panel.narrowest;
            break;
        case "SourceHigh":
            low = move.sourceLow * panel.narrowest;
            if (moves.raises) {
                high = move.destinationLow;
            }
            break;
        case "DestinationLow":
            if (moves.raises) {
                low = move.sourceHigh;
            }
            high = move.destinationHigh / panel.narrowest;
            break;
        case "DestinationHigh":
            low = move.destinationLow * panel.narrowest;
            if (!moves.raises) {
                high = move.sourceLow;
            }
            break;
        }
        return Math.round(Math.max(low, Math.min(high, hz)));
    }

    /// Sets one of move \a slot's ends, within limited(), while a drag or
    /// a slide goes on.
    function setEnd(slot: int, which: string, hz: real) {
        SessionModel.setParam(panel.blockId, moves.paramId(slot, which), panel.limited(slot, which, hz), true);
    }

    /// Takes the first free slot into use, a copy of the picked move an
    /// octave lower where there is room, and picks it.
    function addMove() {
        const slot = moves.free;
        if (slot < 0) {
            return;
        }
        SessionModel.setParam(panel.blockId, moves.paramId(slot, "Used"), true);
        panel.selected = slot;
    }

    function deleteMove() {
        if (panel.selected < 0) {
            return;
        }
        SessionModel.setParam(panel.blockId, moves.paramId(panel.selected, "Used"), false);
    }

    FrequencyMoves {
        id: moves

        params: panel.params
    }

    FrequencyMoveWords {
        id: words
    }

    // **One row that gives way**: in a narrow window's card the choice
    // and the switch shrink, their words eliding, rather than running past
    // its edge or folding and moving the plot under the pointer.
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        SegmentedFrame {
            QQC2.ButtonGroup {
                id: viewGroup
            }

            SegmentedChoice {
                id: arrowsChoice

                text: KI18n.i18nc("@option:radio show the moves as arrows along a frequency axis", "Arrows")
                checked: panel.view === "arrows"
                QQC2.ButtonGroup.group: viewGroup
                onClicked: panel.view = "arrows"
            }

            SegmentedChoice {
                id: mapChoice

                text: KI18n.i18nc("@option:radio show the moves as a map of frequency in against frequency out", "Map")
                checked: panel.view === "map"
                QQC2.ButtonGroup.group: viewGroup
                onClicked: panel.view = "map"
            }
        }

        Item {
            Layout.fillWidth: true
        }

        QQC2.Switch {
            id: movedOnlyControl

            // Its words elide in a narrow card, and are whole on hover.
            // Filling up to its own width, so the row shrinks it too; a
            // control that does not fill is never given less than it asks.
            Layout.fillWidth: true
            Layout.minimumWidth: Kirigami.Units.gridUnit * 3
            Layout.maximumWidth: implicitWidth
            Layout.preferredWidth: implicitWidth
            QQC2.ToolTip.text: movedOnlyControl.text
            QQC2.ToolTip.visible: movedOnlyControl.hovered && movedOnlyControl.implicitContentWidth > movedOnlyControl.availableWidth
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            text: KI18n.i18nc("@option:check hear only the sound the moves add", "Moved sound only")
            onToggled: SessionModel.setParam(panel.blockId, "movedOnly", checked)

            SessionBound {
                control: movedOnlyControl
                property: "checked"
                value: panel.movedOnly
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: Kirigami.Units.gridUnit * 9
        color: AppColors.well
        radius: AppMetrics.controlRadius
        border.color: AppColors.border

        Item {
            id: plotArea

            anchors.fill: parent
            anchors.margins: Kirigami.Units.smallSpacing
            clip: true

            /// The sound before and after the block, on its ear, as
            /// points: [x, y] from -90 dB at the bottom to 0 at the top.
            function spectrumPoints(bands: var): var {
                const centres = Meters.spectrumBandsHz();
                const points = [];
                for (let band = 0; band < bands.length && band < centres.length; ++band) {
                    const hz = centres[band];
                    if (hz < moves.lowestHz || hz > moves.highestHz) {
                        continue;
                    }
                    const db = Math.max(-90, Math.min(0, bands[band]));
                    points.push(Qt.point(panel.xOf(hz), plotArea.height * (-db / 90)));
                }
                return points;
            }

            readonly property var inputPoints: panel.view === "arrows" && Meters.frame >= 0
                ? plotArea.spectrumPoints(Meters.inputSpectrum(panel.blockId)) : []
            readonly property var outputPoints: panel.view === "arrows" && Meters.frame >= 0
                ? plotArea.spectrumPoints(Meters.spectrum(panel.blockId)) : []

            // ---------------------------------------------------- arrows
            Shape {
                id: inputShape

                anchors.fill: parent
                visible: plotArea.inputPoints.length > 1
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    strokeColor: AppColors.muted
                    strokeWidth: 1
                    strokeStyle: ShapePath.DashLine
                    fillColor: "transparent"

                    PathPolyline {
                        path: plotArea.inputPoints
                    }
                }
            }

            Shape {
                id: outputShape

                anchors.fill: parent
                visible: plotArea.outputPoints.length > 1
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    strokeColor: AppColors.muted
                    strokeWidth: 1.5
                    fillColor: "transparent"

                    PathPolyline {
                        path: plotArea.outputPoints
                    }
                }
            }

            Repeater {
                model: panel.view === "arrows" ? moves.used : []

                delegate: Item {
                    id: arrow

                    required property int modelData
                    readonly property var move: moves.move(arrow.modelData)
                    readonly property bool picked: arrow.modelData === panel.selected
                    readonly property real sourceX: panel.xOf(arrow.move.sourceLow)
                    readonly property real sourceWidth: panel.xOf(arrow.move.sourceHigh) - arrow.sourceX
                    readonly property real destinationX: panel.xOf(arrow.move.destinationLow)
                    readonly property real destinationWidth: panel.xOf(arrow.move.destinationHigh) - arrow.destinationX
                    readonly property color colour: arrow.move.active ? panel.tint : AppColors.muted

                    anchors.fill: parent
                    opacity: arrow.picked ? 1.0 : 0.55

                    // What the ear does not use: the source, shaded.
                    Rectangle {
                        x: arrow.sourceX
                        width: arrow.sourceWidth
                        height: parent.height
                        color: Qt.alpha(arrow.colour, arrow.move.source === "keep" ? 0.12 : 0.28)
                    }

                    // Where the sound goes: the destination, outlined.
                    Rectangle {
                        x: arrow.destinationX
                        y: parent.height * 0.35
                        width: arrow.destinationWidth
                        height: parent.height * 0.65
                        color: "transparent"
                        border.color: arrow.colour
                        border.width: arrow.picked ? 2 : 1
                    }

                    Shape {
                        anchors.fill: parent
                        preferredRendererType: Shape.CurveRenderer

                        ShapePath {
                            strokeColor: arrow.colour
                            strokeWidth: 2
                            fillColor: "transparent"
                            startX: arrow.sourceX + arrow.sourceWidth / 2
                            startY: arrow.height * 0.3

                            PathQuad {
                                x: arrow.destinationX + arrow.destinationWidth / 2
                                y: arrow.height * 0.3
                                controlX: (arrow.sourceX + arrow.sourceWidth / 2 + arrow.destinationX + arrow.destinationWidth / 2) / 2
                                controlY: 0
                            }
                        }

                        ShapePath {
                            strokeColor: arrow.colour
                            strokeWidth: 2
                            fillColor: arrow.colour
                            startX: arrow.destinationX + arrow.destinationWidth / 2 - 5
                            startY: arrow.height * 0.3 - 8

                            PathLine {
                                x: arrow.destinationX + arrow.destinationWidth / 2
                                y: arrow.height * 0.3
                            }
                            PathLine {
                                x: arrow.destinationX + arrow.destinationWidth / 2 + 5
                                y: arrow.height * 0.3 - 8
                            }
                        }
                    }

                    // A move that is moving sound now shows it on its
                    // arrow's head.
                    Rectangle {
                        visible: (panel.movingNow[arrow.modelData] ?? 0) > 0.5
                        x: arrow.destinationX + arrow.destinationWidth / 2 - width / 2
                        y: arrow.height * 0.3 + 2
                        width: Kirigami.Units.smallSpacing * 2
                        height: width
                        radius: width / 2
                        color: AppColors.liveMark
                    }
                }
            }

            // The picked move's four ends, which a drag moves.
            Repeater {
                model: panel.view === "arrows" && panel.picked !== null
                    ? ["SourceLow", "SourceHigh", "DestinationLow", "DestinationHigh"] : []

                delegate: Rectangle {
                    id: handle

                    required property string modelData

                    readonly property real hz: panel.picked[handle.modelData.charAt(0).toLowerCase()
                                                            + handle.modelData.slice(1)]
                    x: panel.xOf(handle.hz) - width / 2
                    y: handle.modelData.startsWith("Source") ? 0 : parent.height * 0.35
                    width: 4
                    height: handle.modelData.startsWith("Source") ? parent.height : parent.height * 0.65
                    color: panel.dragging === handle.modelData ? AppColors.accentMark : panel.tint
                }
            }

            MouseArea {
                id: plotGrab

                anchors.fill: parent
                enabled: panel.view === "arrows"
                acceptedButtons: Qt.LeftButton
                // The gesture is ours once it has begun, or the scroll view
                // the panel sits in takes the drag away.
                preventStealing: true

                /// The picked move's end nearest \a x, within reach, or "".
                function endAt(x: real): string {
                    if (panel.picked === null) {
                        return "";
                    }
                    let nearest = "";
                    let best = panel.grabRadius;
                    for (const which of ["SourceLow", "SourceHigh", "DestinationLow", "DestinationHigh"]) {
                        const key = which.charAt(0).toLowerCase() + which.slice(1);
                        const distance = Math.abs(panel.xOf(panel.picked[key]) - x);
                        if (distance <= best) {
                            best = distance;
                            nearest = which;
                        }
                    }
                    return nearest;
                }

                /// The move whose source or destination is under \a x, the
                /// picked one first.
                function moveAt(x: real): int {
                    const hz = panel.hzAt(x);
                    const order = [panel.selected].concat(moves.used.filter(slot => slot !== panel.selected));
                    for (const slot of order) {
                        if (slot < 0) {
                            continue;
                        }
                        const move = moves.move(slot);
                        if ((hz >= move.sourceLow && hz <= move.sourceHigh)
                            || (hz >= move.destinationLow && hz <= move.destinationHigh)) {
                            return slot;
                        }
                    }
                    return -1;
                }

                onPressed: mouse => {
                    const end = plotGrab.endAt(mouse.x);
                    if (end !== "") {
                        panel.dragging = end;
                        return;
                    }
                    const slot = plotGrab.moveAt(mouse.x);
                    if (slot >= 0) {
                        panel.selected = slot;
                    } else {
                        mouse.accepted = false;
                    }
                }
                onPositionChanged: mouse => {
                    if (panel.dragging !== "" && panel.selected >= 0) {
                        panel.setEnd(panel.selected, panel.dragging, panel.hzAt(mouse.x));
                    }
                }
                onReleased: {
                    if (panel.dragging !== "" && panel.selected >= 0) {
                        SessionModel.commitParam(panel.blockId, moves.paramId(panel.selected, panel.dragging));
                    }
                    panel.dragging = "";
                }
                onCanceled: {
                    if (panel.dragging !== "" && panel.selected >= 0) {
                        SessionModel.commitParam(panel.blockId, moves.paramId(panel.selected, panel.dragging));
                    }
                    panel.dragging = "";
                }
            }

            // ------------------------------------------------------- map
            Shape {
                id: mapView

                /// The lines of the map: the diagonal where sound stays,
                /// and for each move that acts, a line from where its
                /// source starts to where what it carries ends — or, for
                /// harmonics, one for each harmonic (FrequencyMoves::
                /// branches()).
                readonly property var lines: {
                    const found = [];
                    for (const slot of moves.used) {
                        const move = moves.move(slot);
                        if (!move.active) {
                            continue;
                        }
                        for (const branch of moves.branches(move)) {
                            found.push({
                                "slot": slot,
                                "from": Qt.point(panel.xOf(branch.from), panel.yOf(branch.fromOut)),
                                "to": Qt.point(panel.xOf(branch.to), panel.yOf(branch.toOut))
                            });
                        }
                    }
                    return found;
                }

                anchors.fill: parent
                visible: panel.view === "map"
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    strokeColor: AppColors.muted
                    strokeWidth: 1
                    strokeStyle: ShapePath.DashLine
                    fillColor: "transparent"
                    startX: 0
                    startY: plotArea.height

                    PathLine {
                        x: plotArea.width
                        y: 0
                    }
                }
            }

            Repeater {
                model: panel.view === "map" ? mapView.lines : []

                delegate: Shape {
                    id: branch

                    required property var modelData

                    anchors.fill: parent
                    preferredRendererType: Shape.CurveRenderer
                    opacity: branch.modelData.slot === panel.selected ? 1.0 : 0.55

                    ShapePath {
                        strokeColor: panel.tint
                        strokeWidth: 2
                        fillColor: "transparent"
                        startX: branch.modelData.from.x
                        startY: branch.modelData.from.y

                        PathLine {
                            x: branch.modelData.to.x
                            y: branch.modelData.to.y
                        }
                    }
                }
            }

            QQC2.Label {
                anchors {
                    left: parent.left
                    bottom: parent.bottom
                }
                text: panel.showingMap
                    ? KI18n.i18nc("@info:tooltip what the map's axes are", "In → across, out ↑")
                    : words.shortHz(moves.lowestHz)
                font: AppFonts.small
                color: AppColors.muted
            }

            QQC2.Label {
                anchors {
                    right: parent.right
                    bottom: parent.bottom
                }
                visible: panel.view === "arrows"
                text: words.shortHz(moves.highestHz)
                font: AppFonts.small
                color: AppColors.muted
            }
        }
    }

    // The moves, one chip each, and a move to add or take away.
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Repeater {
            id: chipRepeater

            model: moves.used

            delegate: QQC2.Button {
                id: chip

                required property int modelData

                // Four chips and two buttons in a narrow window's card: each
                // gives way, its name eliding and whole on hover.
                Layout.fillWidth: true
                Layout.minimumWidth: Kirigami.Units.gridUnit * 2
                Layout.maximumWidth: implicitWidth
                Layout.preferredWidth: implicitWidth
                QQC2.ToolTip.text: chip.text
                QQC2.ToolTip.visible: chip.hovered && chip.implicitContentWidth > chip.availableWidth
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                text: words.moveName(chip.modelData)
                checkable: true
                checked: chip.modelData === panel.selected
                highlighted: chip.checked
                icon.name: (panel.movingNow[chip.modelData] ?? 0) > 0.5 ? "media-playback-start-symbolic" : ""
                onClicked: panel.selected = chip.modelData
            }
        }

        QQC2.ToolButton {
            id: addControl

            icon.name: "list-add-symbolic"
            enabled: moves.free >= 0
            display: QQC2.AbstractButton.IconOnly
            text: KI18n.i18nc("@action:button add a move to a Frequency Lowering or Raising block", "Add a move")
            QQC2.ToolTip.text: addControl.text
            QQC2.ToolTip.visible: addControl.hovered
            onClicked: panel.addMove()
        }

        Item {
            Layout.fillWidth: true
        }

        QQC2.ToolButton {
            id: deleteControl

            icon.name: "edit-delete-remove-symbolic"
            enabled: panel.selected >= 0
            display: QQC2.AbstractButton.IconOnly
            text: KI18n.i18nc("@action:button take a Frequency Lowering or Raising block's picked move away",
                              "Remove this move")
            QQC2.ToolTip.text: deleteControl.text
            QQC2.ToolTip.visible: deleteControl.hovered
            onClicked: panel.deleteMove()
        }
    }

    Kirigami.FormLayout {
        Layout.fillWidth: true
        visible: panel.picked !== null

        QQC2.Switch {
            id: onControl

            Kirigami.FormData.label: KI18n.i18nc("@label:checkbox whether a Frequency Lowering or Raising move acts",
                                                 "On:")
            // The form's label is drawn beside it, not given to it.
            Accessible.name: KI18n.i18nc("@label:checkbox whether the chosen move acts", "Move on")
            onToggled: SessionModel.setParam(panel.blockId, moves.paramId(panel.selected, "On"), checked)

            SessionBound {
                control: onControl
                property: "checked"
                value: panel.selected >= 0 && moves.value(panel.selected, "On") !== false
            }
        }

        SegmentedFrame {
            Kirigami.FormData.label: KI18n.i18nc("@label how a Frequency Lowering or Raising move carries sound",
                                                 "Method:")

            QQC2.ButtonGroup {
                id: methodGroup
            }

            Repeater {
                id: methodRepeater

                model: moves.raises ? [
                    {
                        "id": "compress",
                        "text": KI18n.i18nc("@option:radio squeeze the source range into the destination", "Squeeze")
                    },
                    {
                        "id": "transpose",
                        "text": KI18n.i18nc("@option:radio copy the source range up into the destination", "Copy up")
                    },
                    {
                        "id": "harmonics",
                        "text": KI18n.i18nc("@option:radio give each sound of the source range its harmonics in the destination, keeping its pitch",
                                            "Add harmonics")
                    }
                ] : [
                    {
                        "id": "compress",
                        "text": KI18n.i18nc("@option:radio squeeze the source range into the destination", "Squeeze")
                    },
                    {
                        "id": "transpose",
                        "text": KI18n.i18nc("@option:radio copy the source range down into the destination", "Copy down")
                    }
                ]

                delegate: SegmentedChoice {
                    required property var modelData

                    text: modelData.text
                    checked: panel.picked !== null && panel.picked.method === modelData.id
                    QQC2.ButtonGroup.group: methodGroup
                    onClicked: SessionModel.setParam(panel.blockId, moves.paramId(panel.selected, "Method"), modelData.id)
                }
            }
        }

        ColumnLayout {
            Kirigami.FormData.label: KI18n.i18nc("@label:slider the range a Frequency Lowering or Raising move takes sound from",
                                                 "From:")
            Layout.fillWidth: true
            spacing: 0

            QQC2.Label {
                text: panel.picked !== null ? words.range(panel.picked.sourceLow, panel.picked.sourceHigh) : ""
                color: AppColors.accentText
                font.family: AppFonts.monoFamily
            }

            QQC2.RangeSlider {
                id: sourceControl

                Layout.fillWidth: true
                from: 0
                to: 1
                Accessible.name: KI18n.i18nc("@label:slider", "Range the sound comes from")

                first.onMoved: panel.setEnd(panel.selected, "SourceLow", panel.hzAlong(first.value))
                second.onMoved: panel.setEnd(panel.selected, "SourceHigh", panel.hzAlong(second.value))
                first.onPressedChanged: if (!first.pressed) {
                    SessionModel.commitParam(panel.blockId, moves.paramId(panel.selected, "SourceLow"));
                }
                second.onPressedChanged: if (!second.pressed) {
                    SessionModel.commitParam(panel.blockId, moves.paramId(panel.selected, "SourceHigh"));
                }

                SessionBound {
                    control: sourceControl.first
                    property: "value"
                    value: panel.picked !== null ? panel.along(panel.picked.sourceLow) : 0
                    suspended: sourceControl.first.pressed
                }
                SessionBound {
                    control: sourceControl.second
                    property: "value"
                    value: panel.picked !== null ? panel.along(panel.picked.sourceHigh) : 1
                    suspended: sourceControl.second.pressed
                }
            }
        }

        ColumnLayout {
            Kirigami.FormData.label: KI18n.i18nc("@label:slider the range a Frequency Lowering or Raising move puts sound into",
                                                 "Into:")
            Layout.fillWidth: true
            spacing: 0

            QQC2.Label {
                text: panel.picked !== null ? words.range(panel.picked.destinationLow, panel.picked.destinationHigh) : ""
                color: AppColors.accentText
                font.family: AppFonts.monoFamily
            }

            QQC2.RangeSlider {
                id: destinationControl

                Layout.fillWidth: true
                from: 0
                to: 1
                Accessible.name: KI18n.i18nc("@label:slider", "Range the sound goes into")

                first.onMoved: panel.setEnd(panel.selected, "DestinationLow",
                                            panel.hzAlong(first.value))
                second.onMoved: panel.setEnd(panel.selected, "DestinationHigh",
                                             panel.hzAlong(second.value))
                first.onPressedChanged: if (!first.pressed) {
                    SessionModel.commitParam(panel.blockId, moves.paramId(panel.selected, "DestinationLow"));
                }
                second.onPressedChanged: if (!second.pressed) {
                    SessionModel.commitParam(panel.blockId, moves.paramId(panel.selected, "DestinationHigh"));
                }

                SessionBound {
                    control: destinationControl.first
                    property: "value"
                    value: panel.picked !== null ? panel.along(panel.picked.destinationLow) : 0
                    suspended: destinationControl.first.pressed
                }
                SessionBound {
                    control: destinationControl.second
                    property: "value"
                    value: panel.picked !== null ? panel.along(panel.picked.destinationHigh) : 1
                    suspended: destinationControl.second.pressed
                }
            }
        }

        RowLayout {
            Kirigami.FormData.label: KI18n.i18nc("@label:slider the level of the sound a Frequency Lowering or Raising move adds",
                                                 "Level:")
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Slider {
                id: levelControl

                Layout.fillWidth: true
                from: -24
                to: 12
                stepSize: 0.5
                snapMode: QQC2.Slider.SnapAlways
                Accessible.name: KI18n.i18nc("@label:slider", "Level of the moved sound")
                Accessible.description: levelText.text

                onMoved: SessionModel.setParam(panel.blockId, moves.paramId(panel.selected, "Level"), value, true)
                onPressedChanged: if (!pressed) {
                    SessionModel.commitParam(panel.blockId, moves.paramId(panel.selected, "Level"));
                }

                SessionBound {
                    control: levelControl
                    property: "value"
                    value: panel.picked !== null ? panel.picked.level : 0
                    suspended: levelControl.pressed
                }
            }

            QQC2.Label {
                id: levelText

                text: words.decibels(levelControl.value)
                color: AppColors.accentText
                font.family: AppFonts.monoFamily
            }
        }

        SegmentedFrame {
            Kirigami.FormData.label: KI18n.i18nc("@label what becomes of the range a Frequency Lowering or Raising move takes sound from",
                                                 "Leave the source:")

            QQC2.ButtonGroup {
                id: sourceGroup
            }

            Repeater {
                id: sourceRepeater

                model: [
                    {
                        "id": "keep",
                        "text": KI18n.i18nc("@option:radio leave the source range as it was", "As it was")
                    },
                    {
                        "id": "lower",
                        "text": KI18n.i18nc("@option:radio make the source range quieter", "Quieter")
                    },
                    {
                        "id": "remove",
                        "text": KI18n.i18nc("@option:radio take the sound out of the source range", "Empty")
                    }
                ]

                delegate: SegmentedChoice {
                    required property var modelData

                    text: modelData.text
                    checked: panel.picked !== null && panel.picked.source === modelData.id
                    QQC2.ButtonGroup.group: sourceGroup
                    onClicked: SessionModel.setParam(panel.blockId, moves.paramId(panel.selected, "Source"), modelData.id)
                }
            }
        }

        SegmentedFrame {
            Kirigami.FormData.label: KI18n.i18nc("@label when a Frequency Lowering or Raising move acts", "Acts:")

            QQC2.ButtonGroup {
                id: whenGroup
            }

            Repeater {
                id: whenRepeater

                model: [
                    {
                        "id": "always",
                        "text": KI18n.i18nc("@option:radio a Frequency Lowering or Raising move acts all the time", "Always")
                    },
                    {
                        "id": "needed",
                        "text": moves.raises
                            ? KI18n.i18nc("@option:radio a Frequency Raising move acts only while its low range is louder than what is above it, as with a voice or a hum",
                                          "On low sounds")
                            : KI18n.i18nc("@option:radio a Frequency Lowering move acts only on hissing sounds such as s and f",
                                          "On s, sh, f")
                    }
                ]

                delegate: SegmentedChoice {
                    required property var modelData

                    text: modelData.text
                    checked: panel.picked !== null && panel.picked.when === modelData.id
                    QQC2.ButtonGroup.group: whenGroup
                    onClicked: SessionModel.setParam(panel.blockId, moves.paramId(panel.selected, "When"), modelData.id)
                }
            }
        }
    }

    InfoLine {
        text: moves.raises
            ? KI18n.i18nc("@info what the Frequency Raising block does",
                          "Moves sound from a low or middle range the ear makes no use of into a higher range it hears. Copy up adds a copy two octaves or so higher; Squeeze fits the whole range into another; Add harmonics (experimental) gives each low sound its harmonics higher up, so a voice keeps its pitch. It holds its ear back by about 85 ms. Put it before any block that sets the level of each range.")
            : KI18n.i18nc("@info what the Frequency Lowering block does",
                          "Moves sound from a range the ear makes no use of into a lower range it hears: the hiss of s, sh and f, which tells \"sit\" from \"fit\". Squeeze fits the whole range into a smaller one; Copy down adds a copy an octave or so lower. Put it before any block that sets the level of each range, so the moved sound gets the level of where it lands.")
    }
}
