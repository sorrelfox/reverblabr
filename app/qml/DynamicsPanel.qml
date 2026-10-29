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

// The Dynamics Processor's panel: the curve, the handles that shape it, and
// everything else as a number underneath.
//
// **The curve is the control.** What this block does to a level is a shape,
// and a shape is read and aimed at far faster than five numbers that add up
// to it. Four handles carry the four things that bend the line — where the
// expander starts, where the compressor starts, how wide the bend is and how
// steep it gets — and every other setting is a row under Details, which is
// closed when the panel opens because it is not what the block is about.
//
// **The line is the engine's line.** It is sampled from the same function
// the block follows sample by sample (DynamicsPlot), so what is drawn and
// what is heard cannot drift apart. Nothing here works a gain out for
// itself.
//
// **Nothing is reachable by pointer alone.** Each handle is a control in the
// tab order whose name reads its value, and the arrow keys move it: 2 dB a
// press, or half a decibel with Shift. A curve that could only be dragged
// would be a block somebody could not use at all.
ColumnLayout {
    id: panel

    /// The block whose settings these are. **Not a required property**: the
    /// panel is made from a Component in the register, which cannot see the
    /// loader's context, so the settings panel binds it once it exists.
    property string blockId: ""

    readonly property string typeId: SessionModel.revision >= 0 ? (SessionModel.block(panel.blockId).type ?? "") : ""
    /// Re-read when the session changes, so a value set anywhere — undo, a
    /// second window, a preset — is the curve that is drawn.
    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(panel.blockId) : ({})

    /// The handle a drag has hold of, or empty when nothing is being
    /// dragged. **The plot owns the gesture, not the handle**: the
    /// handles sit a few pixels apart — the knee is half a knee from the
    /// threshold, and nothing stops them landing on top of each other — so
    /// a press on one of them was as likely to be taken by its neighbour,
    /// which then ran into its own limit and looked like a curve that could
    /// not be dragged at all. One grab over the whole plot picks the
    /// nearest, and keeps it for the rest of the gesture, so what is under
    /// the pointer when the button goes down is what moves.
    property string dragging: ""

    /// What this panel has sent that the session has not told it about yet.
    /// **A knob being turned is not news** the rest of the window hears
    /// about until the gesture ends (SessionModel), which is right for a
    /// tile across the canvas and wrong for the curve being dragged: the
    /// handle has to be under the pointer and the line has to bend as it
    /// moves. So the panel draws what it has sent, and drops it the moment
    /// the session does speak — an undo, a second window, a preset — because
    /// then the session's word is the newer one.
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

    /// How far above the threshold the ratio handle sits. Far enough that
    /// the knee has finished and the straight part has started, whatever
    /// width the knee is set to, and near enough to stay on the plot.
    readonly property real ratioHandleOffsetDb: 28

    /// What an arrow key is worth. **Two decibels, and half of one with
    /// Shift**: a threshold is aimed at in whole decibels and trimmed in
    /// halves.
    readonly property real stepDb: 2
    readonly property real fineStepDb: 0.5
    /// A ratio is a multiplication, so a key press is a multiple rather than
    /// an amount: 4:1 is as far from 2:1 as 8:1 is from 4:1.
    readonly property real ratioStepFactor: 1.6

    /// How much reduction fills the bar beside the meter. Anything past it
    /// is the bar full, which is what "a lot" looks like.
    readonly property real reductionSpanDb: 24

    /// The tint the block's category carries, which is what the curve is
    /// drawn in wherever it appears.
    readonly property color tint: AppColors.categoryTint(BlockPresentation.category(panel.typeId))

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

    // --- where each handle sits, and what moving it means ---

    /// The input level a handle stands at. Everything else about its
    /// position follows from the curve, so a handle cannot be anywhere but
    /// on the line it edits.
    function handleInputDb(handleId: string): real {
        switch (handleId) {
        case "expanderThreshold":
            return panel.value("expanderThreshold");
        case "threshold":
            return panel.value("threshold");
        case "knee":
            // The top of the knee: the point where the bend finishes and the
            // straight, compressed part begins.
            return panel.value("threshold") + panel.value("knee") / 2;
        case "ratio":
            return Math.min(panel.value("threshold") + panel.ratioHandleOffsetDb, panel.curve.highestDb - 10);
        }
        return 0;
    }

    /// What the ratio is when its handle is at \a outputDb. Above the knee
    /// the curve is `threshold + (input − threshold) / ratio + makeup`, so
    /// the height of the handle says what the ratio is; dragging it down
    /// steepens the curve.
    function ratioAt(outputDb: real): real {
        const threshold = panel.value("threshold");
        const above = panel.handleInputDb("ratio") - threshold;
        const over = outputDb - threshold - panel.value("makeup");
        if (above <= 0 || over <= 0) {
            return panel.descriptor("ratio").maximum ?? 20;
        }
        return above / over;
    }

    /// **Putting a number on the ratio is turning limiting off.** The
    /// infinite ratio is the click past the end of the slider, so a curve
    /// flattened by it would not answer the handle at all, which reads as a
    /// handle that is broken.
    function stopLimiting() {
        if (panel.settings.infiniteRatio === true) {
            SessionModel.setParam(panel.blockId, "infiniteRatio", false);
        }
    }

    /// Which parameters a handle writes, across and up.
    ///
    /// **A handle is a place on the curve, and a place has two numbers.**
    /// Where the expander starts and how steeply it pushes down are the two
    /// things about its corner; where the compressor starts and how high the
    /// line sits afterwards are the two about its. Each was a separate
    /// control before, and the handle answered only one of them, so half of
    /// every drag did nothing.
    ///
    /// The other two handles have one axis and mean it: the knee is a width
    /// measured across, and the ratio is the steepness of a line whose place
    /// across follows the threshold.
    function handleParams(handleId: string): var {
        switch (handleId) {
        case "expanderThreshold":
            // **The lower one rides the line where nothing is changed**, so
            // across is where the expander starts and down is how far the
            // quiet end falls away — which takes both the range, the most it
            // may take off, and the ratio, how steeply it gets there.
            return {across: "expanderThreshold", up: "expanderRange", also: "expanderRatio"};
        case "threshold":
            return {across: "threshold", up: "makeup"};
        case "knee":
            return {across: "knee", up: ""};
        case "ratio":
            // **The upper one is the far end of the compressed part.**
            // Across drags the bend with it; up and down is how steep the
            // line past the bend is, which is what the ratio says.
            return {across: "threshold", up: "ratio"};
        }
        return {across: "", up: ""};
    }

    /// Every parameter a handle writes, for the readout and for committing.
    function handleParamList(handleId: string): var {
        const pair = panel.handleParams(handleId);
        return [pair.across, pair.up, pair.also ?? ""].filter(id => id !== "");
    }

    /// Where the drag began, and what the expander's ratio was then.
    ///
    /// **A ratio is not a place on the plot.** The other axes are: the
    /// threshold handle really sits at the threshold, and the end of the
    /// compressed part really sits where the ratio puts it, so those can be
    /// read straight off the pointer. The expander's handle rides the line
    /// where nothing is changed, whatever its ratio — so reading its ratio
    /// off the pointer would set it to nothing the moment the handle was
    /// grabbed, and dragging it sideways would flatten the curve for no
    /// reason. It moves by how far the pointer has come instead.
    property real dragFromY: 0
    property real dragFromRatio: 1
    property real dragFromRange: 0
    property real dragFromOutputDb: 0

    /// How much of a drag down doubles the expander's ratio. A whole grid
    /// unit for a doubling: far enough that the number does not run away
    /// under the hand, near enough that the range is crossed in one gesture.
    readonly property real expanderRatioPerPixel: Kirigami.Units.gridUnit

    function expanderRatioAfter(y: real): real {
        const moved = (y - panel.dragFromY) / panel.expanderRatioPerPixel;
        return panel.dragFromRatio * Math.pow(2, moved);
    }

    /// How far down the expander is allowed to pull, after the handle has
    /// been dragged to \a outputDb.
    ///
    /// **A ratio on its own bends nothing.** The range is the most the
    /// expander may take off, and it starts at nought — so however steep
    /// the ratio, the curve stays where it is until the range is opened.
    /// Pulling the handle down is asking for the quiet end to fall away, so
    /// it opens the range by as many decibels as it was pulled.
    function expanderRangeAfter(outputDb: real): real {
        return panel.dragFromRange + (panel.dragFromOutputDb - outputDb);
    }

    /// The handle nearest \a x, \a y in the plot's own coordinates, or
    /// empty when the press was nowhere near one. **Nearest within reach
    /// rather than under the pointer**, for the same reason the canvas's
    /// ports work that way: a dot is a thing to aim at, not a thing to hit.
    function handleNear(x: real, y: real, reach: real): string {
        let nearest = reach;
        let best = "";
        for (let index = 0; index < handleRepeater.count; ++index) {
            const id = handleRepeater.model[index];
            const at = panel.curve.pointAt(panel.handleInputDb(id));
            const distance = Math.hypot(at.x - x, at.y - y);
            if (distance < nearest) {
                nearest = distance;
                best = id;
            }
        }
        return best;
    }

    /// A press on the plot takes hold of whatever is nearest.
    function beginDrag(x: real, y: real): bool {
        panel.dragging = panel.handleNear(x, y, Kirigami.Units.gridUnit * 1.5);
        panel.dragFromY = y;
        panel.dragFromRatio = panel.value("expanderRatio");
        panel.dragFromRange = panel.value("expanderRange");
        panel.dragFromOutputDb = panel.curve.dbAtY(y);
        return panel.dragging !== "";
    }

    /// The button came up: the last value of every parameter the handle
    /// writes arrives, and the whole drag is one thing to take back.
    function endDrag() {
        for (const paramId of panel.handleParamList(panel.dragging)) {
            panel.commit(paramId);
        }
        panel.dragging = "";
    }

    /// A handle dragged to a point on the plot.
    function dragTo(handleId: string, x: real, y: real) {
        const inputDb = panel.curve.dbAtX(x);
        const outputDb = panel.curve.dbAtY(y);
        switch (handleId) {
        case "expanderThreshold":
            panel.edit("expanderThreshold", inputDb, true);
            panel.edit("expanderRatio", panel.expanderRatioAfter(y), true);
            panel.edit("expanderRange", panel.expanderRangeAfter(outputDb), true);
            break;
        case "threshold":
            panel.edit("threshold", inputDb, true);
            // Up lifts the line after the bend, which is the make-up gain:
            // how far the corner sits above where it would be without one.
            panel.edit("makeup", outputDb - inputDb, true);
            break;
        case "knee":
            // The handle stands half a knee above the threshold, so the
            // width is twice the distance it was dragged to.
            panel.edit("knee", (inputDb - panel.value("threshold")) * 2, true);
            break;
        case "ratio":
            panel.stopLimiting();
            // Across drags the bend; up and down is how steep the line past
            // it is. The handle stands a fixed distance above the threshold,
            // so it comes along with it.
            panel.edit("threshold", inputDb - panel.ratioHandleOffsetDb, true);
            panel.edit("ratio", panel.ratioAt(outputDb), true);
            break;
        }
    }

    /// A handle moved by the keyboard. **One press is one edit**: a key is
    /// a settled value rather than a gesture, so it is sent as it is pressed
    /// and every press can be taken back on its own.
    function nudge(handleId: string, across: int, up: int, fine: bool) {
        const step = fine ? panel.fineStepDb : panel.stepDb;
        if (handleId === "ratio" && up !== 0) {
            panel.stopLimiting();
            // Up steepens: a bigger ratio holds more back.
            panel.edit("ratio", panel.value("ratio") * Math.pow(panel.ratioStepFactor, up * step * 0.25), false);
            return;
        }
        const pair = panel.handleParams(handleId);
        // **Up is the handle's other parameter**, where it has one: the
        // keyboard reaches everything the pointer does, which is what keeps
        // the curve usable without one.
        if (up !== 0) {
            if (pair.up === "") {
                return;
            }
            if (pair.up === "expanderRatio") {
                panel.edit("expanderRatio", panel.value("expanderRatio") * Math.pow(panel.ratioStepFactor,
                                                                                   up * step * 0.25), false);
                return;
            }
            panel.edit(pair.up, panel.value(pair.up) + up * step, false);
            return;
        }
        if (across === 0 || pair.across === "") {
            return;
        }
        // **The handle ids are the parameter ids**, so a handle writes the
        // parameter it is named after. The knee handle reads its width
        // rather than its place, so that is what a key press changes.
        panel.edit(pair.across, panel.value(pair.across) + across * step, false);
    }

    // --- the words ---

    function handleName(handleId: string): string {
        switch (handleId) {
        case "expanderThreshold":
            return KI18n.i18nc("@label:slider a handle on the transfer curve", "Expander threshold");
        case "threshold":
            return KI18n.i18nc("@label:slider a handle on the transfer curve", "Compressor threshold");
        case "knee":
            return KI18n.i18nc("@label:slider a handle on the transfer curve", "Knee width");
        case "ratio":
            return KI18n.i18nc("@label:slider a handle on the transfer curve", "Ratio above the threshold");
        }
        return "";
    }

    /// What a handle is set to, said as a person would say it.
    function handleValue(handleId: string): string {
        if (handleId === "ratio") {
            if (panel.settings.infiniteRatio === true) {
                return KI18n.i18nc("@info a ratio with no number: nothing gets past the threshold", "limiting");
            }
            return KI18n.i18nc("@info how much of what goes over the threshold comes out", "%1 to 1",
                               Number(panel.value("ratio")).toLocaleString(panel.locale, 'f', 1));
        }
        if (handleId === "knee") {
            return KI18n.i18nc("@info how wide the bend around the threshold is", "%1 decibels wide",
                               Number(panel.value("knee")).toLocaleString(panel.locale, 'f', 0));
        }
        return KI18n.i18nc("@info a level in decibels", "%1 decibels",
                           Number(panel.value(handleId)).toLocaleString(panel.locale, 'f', 0));
    }

    /// The name a handle is read out under: what it is, what it is set to,
    /// and that the arrow keys work on it. **Somebody who cannot see the
    /// curve has to be told all three.**
    function handleAccessibleName(handleId: string): string {
        return KI18n.i18nc("@label:slider a curve handle, its value, and how to move it", "%1, %2. Arrow keys adjust.",
                           panel.handleName(handleId), panel.handleValue(handleId));
    }

    /// One parameter's value, said the way a person would say it. **Not
    /// the same as handleValue()**, which answers for a whole handle; while
    /// a handle is being dragged both of its parameters are read out, and
    /// each has to say what it is in its own units.
    function paramValueText(paramId: string): string {
        if (paramId === "ratio" || paramId === "expanderRatio") {
            return KI18n.i18nc("@info how much of what goes over the threshold comes out", "%1 to 1",
                               Number(panel.value(paramId)).toLocaleString(panel.locale, 'f', 1));
        }
        if (paramId === "knee") {
            return KI18n.i18nc("@info how wide the bend around the threshold is", "%1 decibels wide",
                               Number(panel.value(paramId)).toLocaleString(panel.locale, 'f', 0));
        }
        return panel.formatDb(panel.value(paramId));
    }

    /// The name a person reads for one parameter. **Not from the engine**:
    /// the catalogue holds stable ids and the words are the interface's,
    /// translated and keyed by them.
    function paramLabel(paramId: string): string {
        switch (paramId) {
        case "threshold":
            return KI18n.i18nc("@label where the compressor starts working", "Threshold");
        case "ratio":
            return KI18n.i18nc("@label how much of what goes over the threshold comes out", "Ratio");
        case "infiniteRatio":
            return KI18n.i18nc("@label:checkbox a ratio of infinity: nothing gets past the threshold",
                               "Limit at the threshold");
        case "knee":
            return KI18n.i18nc("@label how wide the bend around the threshold is", "Knee");
        case "expanderThreshold":
            return KI18n.i18nc("@label where the expander starts working", "Expander threshold");
        case "expanderRatio":
            return KI18n.i18nc("@label how steeply the quiet end is pushed down", "Expander ratio");
        case "expanderRange":
            return KI18n.i18nc("@label the most the expander may take off", "Expander range");
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
            // **The curve is not clamped, the plot is clipped.** A line bent
            // right off the bottom by an expander is what that setting does;
            // flattening it against the edge would draw something the block
            // does not do.
            clip: true

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

            // --- the four handles ---
            Repeater {
                id: handleRepeater

                // **The four things that bend the line**, named by the
                // parameters they write.
                model: ["expanderThreshold", "threshold", "knee", "ratio"]

                delegate: Item {
                    id: handle

                    required property string modelData

                    /// Where it stands on the curve.
                    readonly property point at: curve.tessellations >= 0
                        ? curve.pointAt(panel.handleInputDb(handle.modelData))
                        : Qt.point(0, 0)

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

                    Rectangle {
                        anchors.centerIn: parent
                        width: Kirigami.Units.gridUnit * 0.65
                        height: width
                        radius: width / 2
                        color: handle.modelData === "expanderThreshold" ? AppColors.okMark
                             : handle.modelData === "knee" ? AppColors.muted : panel.tint
                        border.color: AppColors.well
                        border.width: 2
                    }

                    // The focus ring, drawn over the curve so that it can be
                    // seen against it.
                    Rectangle {
                        anchors.fill: parent
                        visible: handle.activeFocus
                        color: "transparent"
                        radius: width / 2
                        border.color: AppColors.accentMark
                        border.width: 2
                    }

                    QQC2.ToolTip.text: panel.handleAccessibleName(handle.modelData)
                    QQC2.ToolTip.visible: handleHover.hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

                    // **Hovering is the handle's; dragging is the plot's.**
                    // A pointer area of its own here is what let the wrong
                    // handle take a press.
                    HoverHandler {
                        id: handleHover

                        cursorShape: Qt.SizeAllCursor
                    }

                    Keys.onLeftPressed: event => {
                        panel.nudge(handle.modelData, -1, 0, event.modifiers & Qt.ShiftModifier);
                        event.accepted = true;
                    }
                    Keys.onRightPressed: event => {
                        panel.nudge(handle.modelData, 1, 0, event.modifiers & Qt.ShiftModifier);
                        event.accepted = true;
                    }
                    Keys.onUpPressed: event => {
                        panel.nudge(handle.modelData, 0, 1, event.modifiers & Qt.ShiftModifier);
                        event.accepted = true;
                    }
                    Keys.onDownPressed: event => {
                        panel.nudge(handle.modelData, 0, -1, event.modifiers & Qt.ShiftModifier);
                        event.accepted = true;
                    }
                }
            }

            // --- what the handle being dragged is setting ---
            //
            // **Both numbers, while the hand is on it.** A handle carries
            // two parameters and a drag changes both, so a person aiming at
            // one of them has to be able to read where it has got to
            // without letting go and looking somewhere else.
            Item {
                id: readout

                readonly property point at: panel.dragging !== "" && curve.tessellations >= 0
                    ? curve.pointAt(panel.handleInputDb(panel.dragging))
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

                    // Beside the handle, and flipped to its other side when
                    // there is no room: a readout that ran off the plot
                    // would be a readout nobody could read.
                    readonly property bool toTheLeft: readout.at.x + width + Kirigami.Units.gridUnit > plotArea.width

                    x: readoutBox.toTheLeft
                        ? Math.max(0, readout.at.x - width - Kirigami.Units.smallSpacing)
                        : readout.at.x + Kirigami.Units.smallSpacing
                    y: Math.max(0, Math.min(plotArea.height - height, readout.at.y - height / 2))
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
                            model: panel.dragging !== "" ? panel.handleParamList(panel.dragging) : []

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
            // **It picks the handle and then keeps it.** Whatever the
            // pointer is nearest when the button goes down is what moves
            // until it comes up again, so a handle cannot be lost to its
            // neighbour halfway through a drag, and the pointer is read in
            // the plot's own coordinates — which do not move with the thing
            // being dragged.
            MouseArea {
                id: plotGrab

                anchors.fill: parent
                // Under the handles, so their hover and their tooltips
                // still work, and over everything else on the plot.
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
                    if (!panel.beginDrag(mouse.x, mouse.y)) {
                        mouse.accepted = false;
                        return;
                    }
                    for (let index = 0; index < handleRepeater.count; ++index) {
                        if (handleRepeater.model[index] === panel.dragging) {
                            handleRepeater.itemAt(index).forceActiveFocus();
                        }
                    }
                    panel.dragTo(panel.dragging, mouse.x, mouse.y);
                }

                onPositionChanged: mouse => {
                    if (panel.dragging !== "") {
                        panel.dragTo(panel.dragging, mouse.x, mouse.y);
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

        /// **Closed when the panel opens.** Twelve numbers are what somebody
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
                descriptor: modelData
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
