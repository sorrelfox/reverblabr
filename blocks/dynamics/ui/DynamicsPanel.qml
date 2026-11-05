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

// The Dynamics Processor's panel: the curve, the four points that shape it,
// and everything else as a number underneath.
//
// **The dashed diagonal is the main line.** The curve is never above it. Each
// point moves along one line only: the first along the floor, the second and
// the third along the diagonal, the fourth up the right edge. What a point can
// do is therefore what its line allows, and there is nothing to drag it into.
//
// **The curve is the control.** What this block does to a level is a shape,
// and a shape is read and aimed at far faster than five numbers that add up
// to it. Every other setting is a row under Details, which is closed when the
// panel opens because it is not what the block is about.
//
// **The line is the engine's line.** It is sampled from the same function
// the block follows sample by sample (DynamicsPlot), and where the points
// stand and what moving one means comes from the same place. Nothing here
// works a gain out for itself.
//
// **The make-up gain is not on the graph.** It moves the sound and not the
// curve, so the line stays on the diagonal whatever it is set to, and the
// In / out reading is where it shows.
//
// **Nothing is reachable by pointer alone.** Each point is a control in the
// tab order whose name reads its value, and the arrow keys move it along its
// line: 2 dB a press, or half a decibel with Shift. A curve that could only
// be dragged would be a block somebody could not use at all.
ColumnLayout {
    id: panel

    /// The block whose settings these are. **Not a required property**: the
    /// panel is made from a Component in the register, which cannot see the
    /// loader's context, so the settings panel binds it once it exists.
    property string blockId: ""

    readonly property string typeId: SessionModel.revision >= 0 ? (SessionModel.block(panel.blockId).type ?? "") : ""
    /// Re-read when the session changes, so a value set anywhere — undo, a
    /// second window, a preset — is the curve that is drawn.
    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(panel.blockId) : ({})

    /// The point a drag has hold of, or empty when nothing is being dragged.
    /// **The plot owns the gesture, not the point**: nothing stops two points
    /// landing on top of each other, so a press on one of them was as likely
    /// to be taken by its neighbour. One grab over the whole plot picks the
    /// nearest, and keeps it for the rest of the gesture, so what is under
    /// the pointer when the button goes down is what moves.
    property string dragging: ""
    /// How far from the point's middle the button went down. **A press takes
    /// hold and does not move anything**, so a point within reach does not
    /// jump to the pointer.
    property point dragOffset: Qt.point(0, 0)
    /// The second and the third point stand in one place, each stopped by
    /// the other, so the direction of the drag says which one is meant.
    property bool dragChoosing: false

    /// What this panel has sent that the session has not told it about yet.
    /// **A dragged value reaches params() once a tick** (SessionModel), up
    /// to a sixtieth of a second after the pointer moved, and the handle
    /// has to be under the pointer, not that far behind it. So the panel
    /// draws what it has sent, and drops it the moment the session does
    /// speak — the tick, an undo, a second window, a preset — because then
    /// the session's word is the newer one.
    property var edits: ({})

    /// The parameters as this panel draws them: the session's, with
    /// whatever is being dragged laid over it.
    readonly property var settings: Object.assign({}, panel.params, panel.edits)

    onParamsChanged: panel.edits = ({})

    readonly property var levels: Meters.frame >= 0 ? Meters.levels(panel.blockId) : ({})
    readonly property bool active: Meters.live && (panel.levels.active ?? false)

    /// The three numbers the block publishes about itself, in decibels.
    readonly property real inputLevelDb: panel.published(curve.inputLevelIndex, curve.lowestDb)
    readonly property real outputLevelDb: panel.published(curve.outputLevelIndex, curve.lowestDb)
    readonly property real reductionDb: panel.published(curve.gainReductionIndex, 0)

    /// What an arrow key is worth. **Two decibels, and half of one with
    /// Shift**: a threshold is aimed at in whole decibels and trimmed in
    /// halves.
    readonly property real stepDb: 2
    readonly property real fineStepDb: 0.5

    /// How much reduction fills the bar beside the meter. Anything past it
    /// is the bar full, which is what "a lot" looks like.
    readonly property real reductionSpanDb: 24

    /// The tint the block's category carries, which is what the curve is
    /// drawn in wherever it appears.
    readonly property color tint: AppColors.categoryTint(BlockPresentation.category(panel.typeId))

    /// How far beyond the plot a press still counts. The first point stands
    /// on the bottom edge and the fourth on the right one, so half of each is
    /// outside it.
    readonly property real edgeReach: Kirigami.Units.gridUnit * 0.75

    /// The parts themselves, so that a test drives what a person drives.
    readonly property alias curve: curve
    readonly property alias handles: handleRepeater
    readonly property alias detailsButton: detailsToggle
    readonly property alias detailRows: detailRepeater
    readonly property alias reductionReadout: reductionLabel
    /// The box that names what a handle is setting while it is dragged.
    readonly property alias dragReadout: readout

    /// The locale the numbers are written in. The reader's, unless a test
    /// says otherwise.
    property var locale: Qt.locale()

    spacing: Kirigami.Units.smallSpacing

    // --- what the block says about itself ---

    /// One of the block's published values, or \a fallback where the block
    /// is not running and there is nothing to read.
    function published(index: int, fallback: real): real {
        const values = panel.levels.values ?? [];
        return index >= 0 && index < values.length ? values[index] : fallback;
    }

    // --- the parameters, read through the catalogue rather than retyped ---

    function descriptor(paramId: string): var {
        return BlockCatalogue.describeParam(panel.typeId, paramId);
    }

    /// What a parameter is now. **A parameter the session does not mention
    /// is at its default**, which is what the block itself would say.
    function value(paramId: string): real {
        const held = panel.settings[paramId];
        return held === undefined ? (panel.descriptor(paramId).default ?? 0) : held;
    }

    /// \a wanted brought inside the range the block declares. The engine
    /// clamps what it is sent anyway; doing it here as well is what keeps a
    /// handle from running off the end of the plot while it is dragged.
    function clamped(paramId: string, wanted: real): real {
        const declared = panel.descriptor(paramId);
        const lowest = declared.minimum ?? wanted;
        const highest = declared.maximum ?? wanted;
        return Math.max(lowest, Math.min(highest, wanted));
    }

    /// Sets a parameter. **While a handle is held, `live` is true**: the
    /// edit is coalesced and sent sixty times a second rather than once per
    /// mouse event, and the whole drag comes back in one undo.
    function edit(paramId: string, wanted: real, live: bool) {
        const value = panel.clamped(paramId, wanted);
        SessionModel.setParam(panel.blockId, paramId, value, live);
        const next = Object.assign({}, panel.edits);
        next[paramId] = value;
        panel.edits = next;
    }

    /// The end of a gesture. The last value always arrives, whatever the
    /// coalescing timer was doing when the button came up.
    function commit(paramId: string) {
        SessionModel.commitParam(panel.blockId, paramId);
    }

    // --- where each point stands, and what moving it means ---

    /// Where a point stands on the plot. Every number comes from the curve
    /// itself, so a point cannot be anywhere but on the line it edits.
    function handlePoint(handleId: string): point {
        switch (handleId) {
        case "cutoff":
            return Qt.point(curve.xOfDb(curve.cutoffDb()), curve.yOfDb(curve.lowestDb));
        case "expanderThreshold": {
            const start = curve.expanderStartDb();
            return Qt.point(curve.xOfDb(start), curve.yOfDb(start));
        }
        case "threshold": {
            const threshold = curve.thresholdDb();
            return Qt.point(curve.xOfDb(threshold), curve.yOfDb(threshold));
        }
        case "height":
            return Qt.point(curve.xOfDb(curve.highestDb), curve.yOfDb(curve.endDb()));
        }
        return Qt.point(0, 0);
    }

    /// The one parameter a point writes. **One each**, so a drag is one
    /// value changing and one step to take back.
    function handleParam(handleId: string): string {
        return handleId === "cutoff" ? "expanderRatio" : handleId;
    }

    /// The two numbers read out while a point is held: the pair its part of
    /// the curve is made of.
    function handleReadout(handleId: string): var {
        if (handleId === "cutoff" || handleId === "expanderThreshold") {
            return ["expanderThreshold", "expanderRatio"];
        }
        return ["threshold", "height"];
    }

    /// Puts a point at \a db along its own line, which is the only number a
    /// point has. **The second and the third stop at each other**, and the
    /// other two ask the curve what that place means.
    function place(handleId: string, db: real, live: bool) {
        switch (handleId) {
        case "cutoff":
            panel.edit("expanderRatio", curve.ratioForCutoff(db), live);
            break;
        case "expanderThreshold":
            panel.edit("expanderThreshold", Math.min(db, curve.thresholdDb()), live);
            break;
        case "threshold":
            panel.edit("threshold", Math.max(db, curve.expanderStartDb()), live);
            break;
        case "height":
            panel.edit("height", curve.heightForEnd(db), live);
            break;
        }
    }

    /// How far along its line a point at \a x, \a y on the plot is. A point
    /// on the diagonal takes the nearest place on it, whichever way the
    /// pointer has wandered off.
    function alongLine(handleId: string, x: real, y: real): real {
        switch (handleId) {
        case "cutoff":
            return curve.dbAtX(x);
        case "height":
            return curve.dbAtY(y);
        }
        return (curve.dbAtX(x) + curve.dbAtY(y)) / 2;
    }

    /// The point nearest \a x, \a y in the plot's own coordinates, or empty
    /// when the press was nowhere near one. **Nearest within reach rather
    /// than under the pointer**, for the same reason the canvas's ports work
    /// that way: a dot is a thing to aim at, not a thing to hit. On a tie the
    /// upper point wins, because where the first two meet in the corner only
    /// the second can move.
    function handleNear(x: real, y: real, reach: real): string {
        let nearest = reach;
        let best = "";
        for (let index = 0; index < handleRepeater.count; ++index) {
            const id = handleRepeater.model[index];
            const at = panel.handlePoint(id);
            const distance = Math.hypot(at.x - x, at.y - y);
            if (distance <= nearest) {
                nearest = distance;
                best = id;
            }
        }
        return best;
    }

    /// A press on the plot takes hold of whatever is nearest.
    function beginDrag(x: real, y: real): bool {
        panel.dragging = panel.handleNear(x, y, Kirigami.Units.gridUnit * 1.5);
        if (panel.dragging === "") {
            return false;
        }
        const at = panel.handlePoint(panel.dragging);
        panel.dragOffset = Qt.point(x - at.x, y - at.y);
        panel.dragChoosing = panel.dragging === "threshold" && curve.thresholdDb() - curve.expanderStartDb() < 0.01;
        return true;
    }

    /// The pointer moved, with the button down.
    function moveDrag(x: real, y: real) {
        const pointX = x - panel.dragOffset.x;
        const pointY = y - panel.dragOffset.y;
        if (panel.dragChoosing) {
            const db = panel.alongLine("threshold", pointX, pointY);
            if (db < curve.thresholdDb()) {
                panel.dragging = "expanderThreshold";
            }
            panel.dragChoosing = false;
        }
        panel.dragTo(panel.dragging, pointX, pointY);
    }

    /// The button came up: the last value arrives, and the whole drag is one
    /// thing to take back.
    function endDrag() {
        if (panel.dragging !== "") {
            panel.commit(panel.handleParam(panel.dragging));
        }
        panel.dragging = "";
        panel.dragChoosing = false;
    }

    /// A point dragged to a place on the plot. Only its own line counts: the
    /// rest of where the pointer went is not a thing a point can do.
    function dragTo(handleId: string, x: real, y: real) {
        panel.place(handleId, panel.alongLine(handleId, x, y), true);
    }

    /// A point moved by the keyboard, up its line for +1 and down for -1.
    /// **One press is one edit**: a key is a settled value rather than a
    /// gesture, so it is sent as it is pressed and every press can be taken
    /// back on its own.
    function nudge(handleId: string, direction: int, fine: bool) {
        const step = direction * (fine ? panel.fineStepDb : panel.stepDb);
        switch (handleId) {
        case "cutoff":
            panel.place(handleId, curve.cutoffDb() + step, false);
            break;
        case "expanderThreshold":
            panel.place(handleId, curve.expanderStartDb() + step, false);
            break;
        case "threshold":
            panel.place(handleId, curve.thresholdDb() + step, false);
            break;
        case "height":
            panel.edit("height", panel.value("height") + step, false);
            break;
        }
    }

    /// Where the box beside a held point goes, for a box and a plot of these
    /// sizes.
    ///
    /// - The line leaves the first point at 45 degrees or steeper, so far
    ///   enough along the floor it has climbed past the box. Without the room
    ///   for that, up and to the left of the point is free.
    /// - The curve comes into the fourth from below, so above it is free.
    /// - To the right of a point on the diagonal and under it is free, and
    ///   so is its left, above the diagonal, when it stands too far right.
    ///   On a plot too narrow for either, under it does, moved along to fit.
    function readoutPlace(handleId: string, at: point, boxWidth: real, boxHeight: real,
                          plotWidth: real, plotHeight: real): point {
        const gap = Kirigami.Units.gridUnit * 1.25;
        const widest = Math.max(0, plotWidth - boxWidth);
        const within = (x, y) => Qt.point(Math.max(0, Math.min(widest, x)),
                                          Math.max(0, Math.min(plotHeight - boxHeight, y)));
        switch (handleId) {
        case "cutoff": {
            const along = gap * 2;
            if (at.x + along + boxWidth <= plotWidth) {
                return within(at.x + along, plotHeight - boxHeight);
            }
            return within(at.x - boxWidth, at.y - gap - boxHeight);
        }
        case "height":
            return within(at.x - boxWidth, at.y - gap - boxHeight >= 0 ? at.y - gap - boxHeight : at.y + gap);
        }
        if (at.x + gap + boxWidth <= plotWidth && at.y + gap + boxHeight <= plotHeight) {
            return within(at.x + gap, at.y + gap);
        }
        if (at.x - gap - boxWidth >= 0) {
            return within(at.x - gap - boxWidth, at.y - boxHeight / 2);
        }
        return within(at.x + gap, at.y + gap <= plotHeight - boxHeight ? at.y + gap : at.y - gap - boxHeight);
    }

    /// A parameter's row under Details. **The two thresholds stop at each
    /// other there as well**, so a value typed in cannot put them in the
    /// wrong order on the graph.
    function boundedDescriptor(descriptor: var): var {
        if (descriptor.id === "threshold") {
            return Object.assign({}, descriptor, {minimum: panel.value("expanderThreshold")});
        }
        if (descriptor.id === "expanderThreshold") {
            return Object.assign({}, descriptor, {maximum: panel.value("threshold")});
        }
        return descriptor;
    }

    // --- the words ---

    function handleName(handleId: string): string {
        switch (handleId) {
        case "cutoff":
            return KI18n.i18nc("@label:slider a point on the transfer curve: anything quieter comes out as nothing",
                               "Cut-off");
        case "expanderThreshold":
            return KI18n.i18nc("@label:slider a point on the transfer curve", "Expander threshold");
        case "threshold":
            return KI18n.i18nc("@label:slider a point on the transfer curve", "Compressor threshold");
        case "height":
            return KI18n.i18nc("@label:slider a point on the transfer curve: how high the curve can rise",
                               "Height");
        }
        return "";
    }

    /// What a point is set to, said as a person would say it.
    function handleValue(handleId: string): string {
        let db = 0;
        switch (handleId) {
        case "cutoff":
            db = curve.tessellations >= 0 ? curve.cutoffDb() : 0;
            break;
        case "expanderThreshold":
            db = curve.tessellations >= 0 ? curve.expanderStartDb() : 0;
            break;
        default:
            db = panel.value(handleId);
        }
        return KI18n.i18nc("@info a level in decibels", "%1 decibels",
                           Number(db).toLocaleString(panel.locale, 'f', 0));
    }

    /// The name a point is read out under: what it is, what it is set to,
    /// and that the arrow keys work on it. **Somebody who cannot see the
    /// curve has to be told all three.**
    function handleAccessibleName(handleId: string): string {
        return KI18n.i18nc("@label:slider a curve point, its value, and how to move it", "%1, %2. Arrow keys adjust.",
                           panel.handleName(handleId), panel.handleValue(handleId));
    }

    /// One parameter's value, said the way a person would say it. **Not
    /// the same as handleValue()**, which answers for a whole point; while
    /// a point is being dragged two numbers are read out, and each has to
    /// say what it is in its own units.
    function paramValueText(paramId: string): string {
        if (paramId === "expanderRatio") {
            const ratio = panel.value(paramId);
            return KI18n.i18nc("@info how steeply the quiet end falls away", "%1 to 1",
                               Number(ratio).toLocaleString(panel.locale, 'f', ratio < 10 ? 2 : 1));
        }
        return panel.formatDb(panel.value(paramId));
    }

    /// The name a person reads for one parameter. **Not from the engine**:
    /// the catalogue holds stable ids and the words are the interface's,
    /// translated and keyed by them.
    function paramLabel(paramId: string): string {
        switch (paramId) {
        case "threshold":
            return KI18n.i18nc("@label where the curve starts bending away from the diagonal", "Threshold");
        case "height":
            return KI18n.i18nc("@label how high the curve can rise above the threshold", "Height");
        case "expanderThreshold":
            return KI18n.i18nc("@label where the expander starts working", "Expander threshold");
        case "expanderRatio":
            return KI18n.i18nc("@label how steeply the quiet end is pushed down", "Expander ratio");
        case "expanderHold":
            return KI18n.i18nc("@label how long the expander stays open", "Expander hold");
        case "attack":
            return KI18n.i18nc("@label how fast the block reacts to a sound getting louder", "Attack");
        case "release":
            return KI18n.i18nc("@label how fast the block lets go again", "Release");
        case "makeup":
            return KI18n.i18nc("@label gain added after the curve", "Make-up gain");
        case "detector":
            return KI18n.i18nc("@label what the block follows: the loudest sample or a short average", "Detector");
        }
        return paramId;
    }

    function formatDb(db: real): string {
        return KI18n.i18nc("@info a level in decibels", "%1 dB", Number(db).toLocaleString(panel.locale, 'f', 1));
    }

    /// The gain reduction as the panel writes it: **negative, because it is
    /// what is being taken away**, and a tenth of a decibel because that is
    /// what a person can act on.
    function reductionText(): string {
        if (!panel.active) {
            return KI18n.i18nc("@info nothing is running, so there is nothing to report", "—");
        }
        return panel.formatDb(Math.min(0, panel.reductionDb));
    }

    // --- the plot ---

    // **Reading `tessellations` is what makes a placing follow the curve.**
    // A binding that only calls xOfDb() or pointAt() has nothing to depend
    // on, so it would be worked out once and then stay where it was through
    // every resize and every drag; the count changes whenever the mapping
    // does, and is the dependency that brings the binding back.
    DynamicsPlot {
        id: curve

        params: panel.settings
        plotWidth: plotArea.width
        plotHeight: plotArea.height
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        // The output scale, beside the plot it belongs to.
        Item {
            Layout.preferredWidth: Kirigami.Units.gridUnit * 2
            Layout.preferredHeight: plotArea.height

            Repeater {
                model: [40, 0, -40, -80, -120]

                delegate: QQC2.Label {
                    required property real modelData

                    text: Math.round(modelData)
                    font: AppFonts.small
                    color: AppColors.muted
                    horizontalAlignment: Text.AlignRight
                    width: parent.width
                    // The same mapping the curve is drawn with: a scale
                    // drawn with another one would be a scale that lies.
                    y: Math.max(0, Math.min(parent.height - height,
                                            (curve.tessellations >= 0 ? curve.yOfDb(modelData) : 0) - height / 2))
                }
            }
        }

        Item {
            id: plotArea

            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 14
            Layout.minimumHeight: Kirigami.Units.gridUnit * 8
            // **Not clipped, and in front of what is beside it.** The curve
            // stays inside the graph by itself, and two of the points stand
            // on its edges, half outside it.
            z: 1

            Rectangle {
                anchors.fill: parent
                color: AppColors.well
                border.color: AppColors.border
                border.width: 1
            }

            // --- the grid, and the line where nothing is changed ---
            Repeater {
                model: [-100, -80, -60, -40, -20, 0, 20]

                delegate: Item {
                    id: gridLine

                    required property real modelData

                    anchors.fill: parent

                    Rectangle {
                        x: curve.tessellations >= 0 ? curve.xOfDb(gridLine.modelData) : 0
                        width: 1
                        height: parent.height
                        color: AppColors.grid
                    }

                    Rectangle {
                        y: curve.tessellations >= 0 ? curve.yOfDb(gridLine.modelData) : 0
                        height: 1
                        width: parent.width
                        color: AppColors.grid
                    }
                }
            }

            Shape {
                anchors.fill: parent
                // Shaded rather than flattened, so the curve stays a curve
                // at any size and at any screen scaling (ADR-0009).
                preferredRendererType: Shape.CurveRenderer

                // What the block would do if it did nothing: the line every
                // part of the curve is read against.
                ShapePath {
                    strokeColor: AppColors.muted
                    strokeWidth: 1
                    strokeStyle: ShapePath.DashLine
                    fillColor: "transparent"

                    startX: curve.tessellations >= 0 ? curve.xOfDb(curve.lowestDb) : 0
                    startY: curve.tessellations >= 0 ? curve.yOfDb(curve.lowestDb) : 0

                    PathLine {
                        x: curve.tessellations >= 0 ? curve.xOfDb(curve.highestDb) : 0
                        y: curve.tessellations >= 0 ? curve.yOfDb(curve.highestDb) : 0
                    }
                }

                ShapePath {
                    strokeColor: "transparent"
                    fillColor: Qt.rgba(panel.tint.r, panel.tint.g, panel.tint.b, 0.14)

                    PathPolyline {
                        path: curve.filled
                    }
                }

                ShapePath {
                    strokeColor: panel.tint
                    strokeWidth: Math.max(1, Kirigami.Units.smallSpacing / 2)
                    fillColor: "transparent"
                    joinStyle: ShapePath.RoundJoin

                    PathPolyline {
                        path: curve.curve
                    }
                }
            }

            // --- where the sound is on the curve, this frame ---
            Rectangle {
                visible: panel.active
                x: curve.tessellations >= 0 ? curve.xOfDb(panel.inputLevelDb) : 0
                width: 1
                height: parent.height
                color: AppColors.liveMark
                opacity: 0.5
            }

            Rectangle {
                id: marker

                // **The marker moves and the curve does not.** It is its own
                // item, bound to the telemetry, so a frame costs two numbers
                // and no re-tessellation (ADR-0009).
                readonly property point at: curve.tessellations >= 0 ? curve.pointAt(panel.inputLevelDb) : Qt.point(0, 0)

                visible: panel.active
                x: marker.at.x - width / 2
                y: marker.at.y - height / 2
                width: Kirigami.Units.gridUnit * 0.5
                height: width
                radius: width / 2
                color: AppColors.liveMark
            }

            // --- the four points ---
            Repeater {
                id: handleRepeater

                // Bottom to top, which is the tab order as well.
                model: ["cutoff", "expanderThreshold", "threshold", "height"]

                delegate: Item {
                    id: handle

                    required property string modelData

                    /// Where it stands on the curve.
                    readonly property point at: curve.tessellations >= 0
                        ? panel.handlePoint(handle.modelData)
                        : Qt.point(0, 0)
                    /// Whether a drag has hold of it.
                    readonly property bool held: panel.dragging === handle.modelData

                    x: handle.at.x - width / 2
                    y: handle.at.y - height / 2
                    width: Kirigami.Units.gridUnit
                    height: width

                    // **A control, not a decoration**: in the tab order, and
                    // named with what it is set to.
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Slider
                    Accessible.focusable: true
                    Accessible.name: panel.handleAccessibleName(handle.modelData)

                    // What is held stands out from the three that are not.
                    Rectangle {
                        anchors.centerIn: parent
                        width: handle.held ? Kirigami.Units.gridUnit * 2 : parent.width
                        height: width
                        visible: handle.held || handle.activeFocus
                        radius: width / 2
                        color: handle.held ? Qt.rgba(AppColors.accent.r, AppColors.accent.g, AppColors.accent.b, 0.2)
                                           : "transparent"
                        border.color: AppColors.accentMark
                        border.width: 2
                    }

                    Rectangle {
                        anchors.centerIn: parent
                        width: Kirigami.Units.gridUnit * (handle.held ? 0.8 : 0.65)
                        height: width
                        radius: width / 2
                        color: handle.held ? AppColors.accentMark
                             : handle.modelData === "cutoff" || handle.modelData === "expanderThreshold"
                             ? AppColors.okMark : panel.tint
                        border.color: AppColors.well
                        border.width: 2
                    }

                    QQC2.ToolTip.text: panel.handleAccessibleName(handle.modelData)
                    QQC2.ToolTip.visible: handleHover.hovered && !handle.held
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

                    // **Hovering is the point's; dragging is the plot's.**
                    // A pointer area of its own here is what let the wrong
                    // point take a press.
                    HoverHandler {
                        id: handleHover

                        // The way this point can go.
                        cursorShape: handle.modelData === "cutoff" ? Qt.SizeHorCursor
                                   : handle.modelData === "height" ? Qt.SizeVerCursor : Qt.SizeBDiagCursor
                    }

                    // **Every arrow goes along the point's one line**: right
                    // and up towards the loud end, left and down away from it.
                    Keys.onLeftPressed: event => {
                        panel.nudge(handle.modelData, -1, event.modifiers & Qt.ShiftModifier);
                        event.accepted = true;
                    }
                    Keys.onRightPressed: event => {
                        panel.nudge(handle.modelData, 1, event.modifiers & Qt.ShiftModifier);
                        event.accepted = true;
                    }
                    Keys.onUpPressed: event => {
                        panel.nudge(handle.modelData, 1, event.modifiers & Qt.ShiftModifier);
                        event.accepted = true;
                    }
                    Keys.onDownPressed: event => {
                        panel.nudge(handle.modelData, -1, event.modifiers & Qt.ShiftModifier);
                        event.accepted = true;
                    }
                }
            }

            // --- what the point being dragged is setting ---
            //
            // **Both numbers, while the hand is on it.** Each end of the
            // curve is two numbers, so a person aiming at one of them has to
            // be able to read where it has got to without letting go and
            // looking somewhere else.
            Item {
                id: readout

                readonly property point at: panel.dragging !== "" && curve.tessellations >= 0
                    ? panel.handlePoint(panel.dragging)
                    : Qt.point(0, 0)

                visible: panel.dragging !== ""
                z: 1

                // The guides, down to the two scales the numbers are read
                // against.
                Rectangle {
                    x: readout.at.x
                    y: 0
                    width: 1
                    height: plotArea.height
                    color: AppColors.accent
                    opacity: 0.4
                }

                Rectangle {
                    x: 0
                    y: readout.at.y
                    width: plotArea.width
                    height: 1
                    color: AppColors.accent
                    opacity: 0.4
                }

                Rectangle {
                    id: readoutBox

                    // **Beside the point, on the side the curve is not**: a
                    // box over the line would hide what is being aimed at,
                    // and a box running off the plot could not be read.
                    readonly property point place: curve.tessellations >= 0
                        ? panel.readoutPlace(panel.dragging, readout.at, width, height, plotArea.width, plotArea.height)
                        : Qt.point(0, 0)

                    x: readoutBox.place.x
                    y: readoutBox.place.y
                    width: readoutRows.width + Kirigami.Units.smallSpacing * 2
                    height: readoutRows.height + Kirigami.Units.smallSpacing * 2
                    radius: Kirigami.Units.smallSpacing
                    color: AppColors.surface2
                    border.color: AppColors.border
                    border.width: 1

                    Column {
                        id: readoutRows

                        anchors.centerIn: parent
                        spacing: 0

                        Repeater {
                            model: panel.dragging !== "" ? panel.handleReadout(panel.dragging) : []

                            delegate: Row {
                                required property string modelData

                                spacing: Kirigami.Units.smallSpacing

                                QQC2.Label {
                                    text: panel.paramLabel(parent.modelData)
                                    font: AppFonts.small
                                    color: AppColors.muted
                                }

                                QQC2.Label {
                                    text: panel.paramValueText(parent.modelData)
                                    font: AppFonts.small
                                    color: AppColors.text
                                }
                            }
                        }
                    }
                }
            }

            // --- the one grab, over the whole plot ---
            //
            // **It picks the point and then keeps it.** Whatever the
            // pointer is nearest when the button goes down is what moves
            // until it comes up again, so a handle cannot be lost to its
            // neighbour halfway through a drag, and the pointer is read in
            // the plot's own coordinates — which do not move with the thing
            // being dragged.
            MouseArea {
                id: plotGrab

                anchors.fill: parent
                anchors.margins: -panel.edgeReach
                // Under the points, so their hover and their tooltips still
                // work, and over everything else on the plot.
                z: -1
                acceptedButtons: Qt.LeftButton
                // **The gesture is ours once it has begun.** The settings
                // panel puts every block's panel in a scroll view, and a
                // flickable takes a drag away from whatever is under it as
                // soon as it has moved far enough — so dragging a handle
                // scrolled the panel instead, and the curve stopped
                // following after about ten pixels.
                preventStealing: true

                onPressed: mouse => {
                    if (!panel.beginDrag(mouse.x + plotGrab.x, mouse.y + plotGrab.y)) {
                        mouse.accepted = false;
                        return;
                    }
                    for (let index = 0; index < handleRepeater.count; ++index) {
                        if (handleRepeater.model[index] === panel.dragging) {
                            handleRepeater.itemAt(index).forceActiveFocus();
                        }
                    }
                }

                onPositionChanged: mouse => {
                    if (panel.dragging !== "") {
                        panel.moveDrag(mouse.x + plotGrab.x, mouse.y + plotGrab.y);
                    }
                }

                onReleased: panel.endDrag()
                onCanceled: panel.endDrag()
            }
        }

        // How much is being taken off, beside how much is coming out.
        Item {
            Layout.preferredWidth: Kirigami.Units.gridUnit * 0.7
            Layout.preferredHeight: plotArea.height

            Rectangle {
                anchors.fill: parent
                color: AppColors.well
                border.color: AppColors.border
                border.width: 1
            }

            Rectangle {
                width: parent.width
                // Downwards from the top, because reduction is what is taken
                // off the top of the signal.
                height: parent.height * Math.max(0, Math.min(1, -Math.min(0, panel.reductionDb) / panel.reductionSpanDb))
                color: panel.tint
                opacity: panel.active ? 0.75 : 0
            }

            Accessible.role: Accessible.ProgressBar
            Accessible.name: KI18n.i18nc("@info:whatsthis", "Gain reduction")
            Accessible.description: panel.reductionText()
        }

        LevelMeter {
            blockId: panel.blockId
            showScale: true
            Layout.preferredWidth: Kirigami.Units.gridUnit * 3.5
            Layout.preferredHeight: plotArea.height
        }
    }

    // --- the input scale, under the plot it belongs to ---
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Item {
            Layout.preferredWidth: Kirigami.Units.gridUnit * 2
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: inputScaleMetrics.height

            TextMetrics {
                id: inputScaleMetrics

                font: AppFonts.small
                text: "0"
            }

            Repeater {
                model: [-120, -80, -40, 0, 40]

                delegate: QQC2.Label {
                    required property real modelData

                    text: Math.round(modelData)
                    font: AppFonts.small
                    color: AppColors.muted
                    x: Math.max(0, Math.min(parent.width - width,
                                            (curve.tessellations >= 0 ? curve.xOfDb(modelData) : 0) - width / 2))
                }
            }
        }

        Item {
            Layout.preferredWidth: Kirigami.Units.gridUnit * 4.2
        }
    }

    // --- what it is doing about it ---
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: reductionColumn.implicitHeight + Kirigami.Units.smallSpacing * 2
            color: AppColors.surface2
            border.color: AppColors.border
            border.width: 1

            ColumnLayout {
                id: reductionColumn

                anchors.fill: parent
                anchors.margins: Kirigami.Units.smallSpacing
                spacing: 0

                QQC2.Label {
                    text: KI18n.i18nc("@label how much the block is holding the sound back", "Gain reduction")
                    font: AppFonts.small
                    color: AppColors.muted
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                QQC2.Label {
                    id: reductionLabel

                    text: panel.reductionText()
                    font: AppFonts.mono
                    color: panel.active && panel.reductionDb < -0.05 ? panel.tint : AppColors.muted
                    Layout.fillWidth: true
                    Accessible.name: KI18n.i18nc("@label how much the block is holding the sound back", "Gain reduction")
                    Accessible.description: text
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: levelsColumn.implicitHeight + Kirigami.Units.smallSpacing * 2
            color: AppColors.surface2
            border.color: AppColors.border
            border.width: 1

            ColumnLayout {
                id: levelsColumn

                anchors.fill: parent
                anchors.margins: Kirigami.Units.smallSpacing
                spacing: 0

                QQC2.Label {
                    text: KI18n.i18nc("@label the level going in and the level coming out", "In / out")
                    font: AppFonts.small
                    color: AppColors.muted
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                QQC2.Label {
                    id: levelsLabel

                    text: panel.active
                        ? KI18n.i18nc("@info the level going in and the level coming out", "%1 / %2",
                                      panel.formatDb(panel.inputLevelDb), panel.formatDb(panel.outputLevelDb))
                        : KI18n.i18nc("@info nothing is running, so there is nothing to report", "—")
                    font: AppFonts.mono
                    color: AppColors.text
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Accessible.name: KI18n.i18nc("@label the level going in and the level coming out", "In / out")
                    Accessible.description: text
                }
            }
        }
    }

    // --- everything the curve does not show ---
    Kirigami.Separator {
        Layout.fillWidth: true
    }

    QQC2.AbstractButton {
        id: detailsToggle

        /// **Closed when the panel opens.** Nine numbers are what somebody
        /// comes back for, not what they open the block to see.
        checkable: true
        checked: false
        Layout.fillWidth: true
        implicitHeight: detailsRow.implicitHeight + Kirigami.Units.smallSpacing
        Accessible.name: KI18n.i18nc("@action:button every setting written out as a number", "Details")
        Accessible.description: detailsToggle.checked
            ? KI18n.i18nc("@info the details section is open", "Open")
            : KI18n.i18nc("@info the details section is closed", "Closed")

        contentItem: RowLayout {
            id: detailsRow

            spacing: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                source: detailsToggle.checked ? "arrow-down" : "arrow-right"
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: Kirigami.Units.iconSizes.small
                Accessible.ignored: true
            }

            QQC2.Label {
                text: KI18n.i18nc("@action:button every setting written out as a number", "Details")
                color: AppColors.text
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            QQC2.Label {
                text: KI18n.i18ncp("@info how many settings are under Details", "%1 setting", "%1 settings",
                                   detailRepeater.count)
                font: AppFonts.small
                color: AppColors.muted
            }
        }
    }

    ColumnLayout {
        id: details

        visible: detailsToggle.checked
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Repeater {
            id: detailRepeater

            model: BlockCatalogue.params(panel.typeId)

            // **The same row as the generic panel's.** A parameter is a
            // parameter, and a second kind of row here would be a second set
            // of rules about how a number is written and sent.
            delegate: ParamRow {
                required property var modelData

                // A layout leaves a hidden row out by itself; a height set
                // here as well fought it for the row (a binding loop).
                visible: modelData.hidden !== true

                blockId: panel.blockId
                descriptor: panel.boundedDescriptor(modelData)
                label: panel.paramLabel(modelData.id)
                currentValue: panel.settings[modelData.id]
                locale: panel.locale
                Layout.fillWidth: true
            }
        }
    }

    // **The presets row is the settings panel's**, not this one's.
    // Every block gets the same footer, wherever its own panel ends, so the
    // placeholder that stood here has gone rather than being filled in.
}
