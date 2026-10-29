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

// A Hearing Compensation block's settings.
//
// - **The gain view:** gain against frequency. The soft, average and loud
//   curves and the ceiling, in the block's colour, with a handle on each band
//   of the curve being edited; the gain being applied now in each band (the
//   "now" line, from the telemetry's band values), moving between the soft
//   and loud curves; the sound behind; and the other ear's curve, faint,
//   when the session has a block for it.
// - **The levels view:** where the sound sits against the ear. The quietest
//   level heard and each band's ceiling, with the sound going in and the
//   sound coming out: the processed sound is seen rising into the
//   space between them.
// - **A band's inspector:** its four settings, and its input/output curve
//   with the level it is at now.
// - **The audiogram**, and **Fit from audiogram**: one edit, one
//   undo. A band where the fit could not help is marked, with a pointer to
//   Frequency Lowering.
// - **Copy from the other ear**, and a **before/after** button that holds
//   the block off while it is pressed.
//
// **Hearing Compensation 64** has the same panel with control points
// in place of a handle a band: a handle on each point, dragged across to
// move it and up or down for the curve being edited; a double click adds a
// point where the curves already are, so nothing changes until it is
// moved; the inspector shows the chosen point, and Remove Point takes it
// out. The 64 bands follow the curves through the points.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""
    /// The band the inspector shows, or -1; through points, the point.
    property int selected: -1
    /// The band or point a drag is moving, or -1.
    property int dragging: -1
    /// Which curve the handles move: "Soft", "Average", "Loud" or "Ceiling".
    property string editing: "Average"
    /// "gain" or "levels".
    property string view: "gain"
    /// What the last fit said about each band ({band, noResponse,
    /// beyondMaximum}), for the marks on the curve.
    property var lastFit: []
    /// Where a drag started, and how far from the handle's centre.
    property real pressedX: 0
    property real pressedY: 0
    property real grabOffset: 0
    property real grabOffsetX: 0

    /// Which block type this is the panel of: 16 bands, or 64 through points.
    property string typeId: "hearing.compensation16"
    readonly property bool throughPoints: bands.throughPoints
    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(panel.blockId) : ({})
    readonly property color tint: AppColors.categoryTint(BlockPresentation.category(panel.typeId))
    readonly property real panelWidth: AppMetrics.followsSystem ? Kirigami.Units.gridUnit * 26 : 460

    /// The audiogram's level boxes: room for "−140" and the stepper, no more.
    readonly property real audiogramNumberWidth: Kirigami.Units.gridUnit * 6
    readonly property real grabRadius: Kirigami.Units.gridUnit

    readonly property string ear: panel.params.ear ?? "left"
    /// The channel a spectrum is asked for: the block's own ear.
    readonly property int earChannel: panel.ear === "right" ? 1 : 0

    /// **The other ear's block**: another of this type in the session, set
    /// for the other side. The first found, or nothing.
    readonly property string otherBlock: {
        if (SessionModel.revision < 0 || panel.blockId === "") {
            return "";
        }
        for (const id of SessionModel.blockIds) {
            if (id !== panel.blockId && SessionModel.blockType(id) === panel.typeId
                    && (SessionModel.param(id, "ear") ?? "left") !== panel.ear) {
                return id;
            }
        }
        return "";
    }
    readonly property var otherParams: panel.otherBlock !== "" && SessionModel.revision >= 0
        ? SessionModel.params(panel.otherBlock) : ({})

    // --- live ---

    /// The band values the block publishes while analysed: each band's
    /// gain now, then its level.
    readonly property var bandValues: Meters.frame >= 0 ? Meters.bandValues(panel.blockId) : []
    readonly property bool live: panel.bandValues.length >= 2 * bands.count
    readonly property var nowGains: panel.live ? panel.bandValues.slice(0, bands.count) : []
    readonly property var nowLevels: panel.live ? panel.bandValues.slice(bands.count, 2 * bands.count) : []
    readonly property var spectrum: Meters.frame >= 0 ? Meters.spectrum(panel.blockId) : []
    readonly property var inputSpectrum: Meters.frame >= 0 ? Meters.inputSpectrum(panel.blockId) : []

    // --- axes ---

    readonly property real lowestHz: 80
    readonly property real highestHz: 18000
    readonly property real topDb: 60
    readonly property real bottomDb: -20
    /// The levels view's scale, and the spectrum's behind the gains.
    readonly property real loudestDbfs: 0
    readonly property real quietestDbfs: -100

    /// The parts a test drives, as a person would.
    readonly property alias bands: bands
    readonly property alias otherBands: otherBands
    readonly property alias plotArea: plotArea
    readonly property alias nowLine: nowPolyline
    readonly property alias averageLine: averagePolyline
    readonly property alias otherLine: otherPolyline
    readonly property alias fitButton: fitControl
    readonly property alias copyButton: copyControl
    readonly property alias beforeButton: beforeControl
    readonly property alias fitMessage: fitWords
    readonly property alias handles: handleRepeater
    readonly property alias audiogramSection: audiogramSection
    readonly property alias bandCurve: bandCurvePolyline
    readonly property alias bandCurveArea: bandCurveArea
    readonly property alias bandCurveDot: bandCurveDot
    readonly property alias removeButton: removeControl

    spacing: Kirigami.Units.largeSpacing

    onBlockIdChanged: {
        panel.selected = -1;
        panel.lastFit = [];
        panel.askForSpectrum();
    }
    onEarChannelChanged: panel.askForSpectrum()
    onVisibleChanged: panel.askForSpectrum()
    Component.onCompleted: panel.askForSpectrum()
    Component.onDestruction: {
        panel.letGoOfSpectrum();
        panel.endComparing();
    }

    HearingBands {
        id: bands

        params: panel.params
        typeId: panel.typeId
    }

    HearingBands {
        id: otherBands

        params: panel.otherParams
        typeId: panel.typeId
    }

    /// Asks for this block's band values and spectra while the panel is
    /// shown, of the block's own ear, and lets go while it is not.
    function askForSpectrum() {
        if (panel.visible && panel.blockId !== "") {
            Meters.analyse(panel.blockId, panel.earChannel);
        } else {
            panel.letGoOfSpectrum();
        }
    }

    function letGoOfSpectrum() {
        if (panel.blockId !== "" && Meters.analysed() === panel.blockId) {
            Meters.analyse("");
        }
    }

    // --- coordinates ---

    function xOfHz(hz: real): real {
        const at = Math.log(Math.max(hz, panel.lowestHz) / panel.lowestHz) / Math.log(panel.highestHz / panel.lowestHz);
        return plotArea.width * Math.max(0, Math.min(1, at));
    }

    function yOfGain(db: real): real {
        const clamped = Math.max(panel.bottomDb, Math.min(panel.topDb, db));
        return plotArea.height * (panel.topDb - clamped) / (panel.topDb - panel.bottomDb);
    }

    function gainAtY(y: real): real {
        return panel.topDb - y / plotArea.height * (panel.topDb - panel.bottomDb);
    }

    function hzAtX(x: real): real {
        return panel.lowestHz * Math.pow(panel.highestHz / panel.lowestHz, Math.max(0, Math.min(1, x / plotArea.width)));
    }

    function yOfLevel(dbfs: real): real {
        const clamped = Math.max(panel.quietestDbfs, Math.min(panel.loudestDbfs, dbfs));
        return plotArea.height * (panel.loudestDbfs - clamped) / (panel.loudestDbfs - panel.quietestDbfs);
    }

    /// A band setting as the gain view draws it: the gains as they are, the
    /// ceiling as the most gain a loud sound may have under it.
    function drawnGain(band: int, which: string): real {
        return panel.asDrawn(bands.value(band, which), which);
    }

    /// A control point's setting, drawn the same way.
    function drawnPointGain(point: int, which: string): real {
        return panel.asDrawn(bands.pointValue(point, which), which);
    }

    function asDrawn(setting: real, which: string): real {
        return which === "Ceiling" ? bands.ceilingDbfs(setting) - (bands.averageInputDbfs + bands.levelStepDb) : setting;
    }

    /// The setting a gain at \a y stands for, on the curve being edited.
    function settingAtY(which: string, y: real): real {
        const db = panel.gainAtY(y);
        if (which === "Ceiling") {
            const ceiling = db + bands.averageInputDbfs + bands.levelStepDb + bands.listeningOffsetDb;
            return Math.round(Math.max(40, Math.min(140, ceiling)) * 10) / 10;
        }
        return Math.round(Math.max(-30, Math.min(60, db)) * 10) / 10;
    }

    function line(source: var, which: string): var {
        const points = [];
        if (plotArea.width <= 0) {
            return points;
        }
        for (let band = 0; band < source.count; ++band) {
            const db = which === "Ceiling"
                ? source.ceilingDbfs(source.value(band, "Ceiling")) - (source.averageInputDbfs + source.levelStepDb)
                : source.value(band, which);
            points.push(Qt.point(panel.xOfHz(source.centresHz[band]), panel.yOfGain(db)));
        }
        return points;
    }

    /// The "now" line: the gain each band applies at this moment.
    readonly property var nowPoints: {
        const points = [];
        if (plotArea.width <= 0) {
            return points;
        }
        for (let band = 0; band < panel.nowGains.length; ++band) {
            points.push(Qt.point(panel.xOfHz(bands.centresHz[band]), panel.yOfGain(panel.nowGains[band])));
        }
        return points;
    }

    /// A spectrum (Meters' bands, dB) as a closed area on the levels scale.
    function spectrumPoints(levels: var, closed: bool): var {
        const hz = Meters.spectrumBandsHz();
        const points = [];
        if (plotArea.width <= 0 || levels.length === 0 || hz.length !== levels.length) {
            return points;
        }
        if (closed) {
            points.push(Qt.point(panel.xOfHz(hz[0]), plotArea.height));
        }
        for (let band = 0; band < levels.length; ++band) {
            points.push(Qt.point(panel.xOfHz(hz[band]), panel.yOfLevel(levels[band])));
        }
        if (closed) {
            points.push(Qt.point(panel.xOfHz(hz[hz.length - 1]), plotArea.height));
        }
        return points;
    }

    // --- the audiogram, on the levels view ---

    readonly property var audiogramHz: [125, 250, 500, 750, 1000, 1500, 2000, 3000, 4000, 6000, 8000]
    readonly property bool audiogramInDbfs: (panel.params.audiogramUnit ?? "dbHl") === "dbFs"

    /// The quietest level heard at each measured point, in dBFS of a band.
    readonly property var thresholdPoints: {
        const points = [];
        if (plotArea.width <= 0) {
            return points;
        }
        for (const hz of panel.audiogramHz) {
            const mark = panel.params["audiogram" + hz] ?? "none";
            if (mark === "none") {
                continue;
            }
            const level = panel.params["audiogram" + hz + "Db"] ?? 0;
            const dbfs = panel.audiogramInDbfs ? level : level - bands.listeningOffsetDb;
            points.push(Qt.point(panel.xOfHz(hz), panel.yOfLevel(dbfs)));
        }
        return points;
    }

    readonly property var ceilingPoints: {
        const points = [];
        if (plotArea.width <= 0) {
            return points;
        }
        for (let band = 0; band < bands.count; ++band) {
            points.push(Qt.point(panel.xOfHz(bands.centresHz[band]),
                                 panel.yOfLevel(bands.ceilingDbfs(bands.value(band, "Ceiling")))));
        }
        return points;
    }

    // --- editing ---

    /// What the handles stand for: every band, or the points that are on.
    readonly property var handleIndices: {
        if (panel.throughPoints) {
            return bands.placed;
        }
        const all = [];
        for (let band = 0; band < bands.count; ++band) {
            all.push(band);
        }
        return all;
    }

    /// Where the handle of band (or point) \a index is.
    function handlePoint(index: int): point {
        if (panel.throughPoints) {
            return Qt.point(panel.xOfHz(bands.pointValue(index, "Hz")),
                            panel.yOfGain(panel.drawnPointGain(index, panel.editing)));
        }
        return Qt.point(panel.xOfHz(bands.centresHz[index]), panel.yOfGain(panel.drawnGain(index, panel.editing)));
    }

    /// The band or point whose handle is under (\a x, \a y), or -1.
    function bandAt(x: real, y: real): int {
        let nearest = -1;
        let distance = panel.grabRadius;
        for (const index of panel.handleIndices) {
            const point = panel.handlePoint(index);
            const away = Math.hypot(point.x - x, point.y - y);
            if (away <= distance) {
                distance = away;
                nearest = index;
            }
        }
        return nearest;
    }

    /// The id of the setting a drag of \a index moves up and down.
    function editedParamId(index: int): string {
        return panel.throughPoints ? bands.pointParamId(index, panel.editing) : bands.paramId(index, panel.editing);
    }

    /// A frequency a point may be at, from where it is dragged to.
    function pointHzAtX(x: real): real {
        return Math.round(Math.max(100, Math.min(16000, panel.hzAtX(x))));
    }

    function dragTo(index: int, x: real, y: real) {
        if (y !== panel.pressedY) {
            SessionModel.setParam(panel.blockId, panel.editedParamId(index),
                                  panel.settingAtY(panel.editing, y - panel.grabOffset), true);
        }
        // A point moves across too: its frequency.
        if (panel.throughPoints && x !== panel.pressedX) {
            SessionModel.setParam(panel.blockId, bands.pointParamId(index, "Hz"),
                                  panel.pointHzAtX(x - panel.grabOffsetX), true);
        }
    }

    function endDrag() {
        if (panel.dragging >= 0) {
            SessionModel.commitParam(panel.blockId, panel.editedParamId(panel.dragging));
            if (panel.throughPoints) {
                SessionModel.commitParam(panel.blockId, bands.pointParamId(panel.dragging, "Hz"));
            }
        }
        panel.dragging = -1;
    }

    /// **A new point where the curves already are**, at \a hz: the
    /// first point that is off, switched on with the four settings the
    /// curves have there, as one edit, so nothing changes until it is
    /// moved. Nothing when every point is on. The point, or -1.
    function addPoint(hz: real): int {
        const free = bands.points.find(point => !point.on);
        if (free === undefined) {
            return -1;
        }
        // The settings at hz, from the curves through the points that are on.
        const on = bands.points.filter(point => point.on).sort((a, b) => a.hz - b.hz);
        const changes = [[bands.pointParamId(free.index, "On"), true],
                         [bands.pointParamId(free.index, "Hz"), Math.round(hz)]];
        for (const which of bands.settings) {
            const key = which.toLowerCase();
            let value = key === "ceiling" ? bands.defaultCeilingDb : 0;
            if (on.length > 0) {
                value = bands.curveThrough(on.map(point => Math.log(point.hz)), on.map(point => point[key]))(Math.log(hz));
            }
            changes.push([bands.pointParamId(free.index, which), Math.round(value * 10) / 10]);
        }
        SessionModel.setParams(panel.blockId, changes);
        panel.selected = free.index;
        return free.index;
    }

    /// Takes the chosen point out: switched off, its settings kept.
    function removePoint(point: int) {
        if (point >= 0) {
            SessionModel.setParam(panel.blockId, bands.pointParamId(point, "On"), false);
            panel.selected = -1;
        }
    }

    /// Fit from audiogram: one edit, and the marks it leaves.
    function fit() {
        panel.lastFit = SessionModel.fitFromAudiogram(panel.blockId);
    }

    /// Where the last fit could not help, in hertz: a band's centre, or a
    /// point's place.
    readonly property var unhelped: panel.lastFit.filter(result => result.noResponse || result.beyondMaximum)
                                                   .map(result => result.hz ?? bands.centresHz[result.band])

    /// Every band setting of the other ear's block, as one edit.
    function copyFromOtherEar() {
        if (panel.otherBlock === "") {
            return;
        }
        const changes = [];
        if (panel.throughPoints) {
            // Every point, placed or not, as the other ear has it.
            for (const point of otherBands.points) {
                changes.push([bands.pointParamId(point.index, "On"), point.on]);
                changes.push([bands.pointParamId(point.index, "Hz"), point.hz]);
                for (const which of bands.settings) {
                    changes.push([bands.pointParamId(point.index, which), point[which.toLowerCase()]]);
                }
            }
        } else {
            for (let band = 0; band < bands.count; ++band) {
                for (const which of bands.settings) {
                    changes.push([bands.paramId(band, which), otherBands.value(band, which)]);
                }
            }
        }
        SessionModel.setParams(panel.blockId, changes);
    }

    /// Whether the block was on when a comparison began, so letting go puts
    /// back what was there.
    property bool wasEnabled: true
    property bool comparing: false

    function beginComparing() {
        panel.wasEnabled = SessionModel.block(panel.blockId).enabled ?? true;
        panel.comparing = true;
        SessionModel.setBlockEnabled(panel.blockId, false);
    }

    function endComparing() {
        if (panel.comparing) {
            panel.comparing = false;
            SessionModel.setBlockEnabled(panel.blockId, panel.wasEnabled);
        }
    }

    function gainText(db: real): string {
        return KI18n.i18nc("@item a gain in decibels", "%1 dB", (db > 0 ? "+" : "") + db.toFixed(1));
    }

    function hzText(hz: real): string {
        return hz >= 1000 ? KI18n.i18nc("@item a frequency in kilohertz", "%1 kHz", (hz / 1000).toFixed(hz >= 10000 ? 0 : 1))
                          : KI18n.i18nc("@item a frequency in hertz", "%1 Hz", Math.round(hz));
    }

    function viewName(which: string): string {
        return which === "levels" ? KI18n.i18nc("@option:radio a view of levels against frequency", "Levels")
                                  : KI18n.i18nc("@option:radio a view of gain against frequency", "Gain");
    }

    function settingName(which: string): string {
        switch (which) {
        case "Soft":
            return KI18n.i18nc("@option:radio the gain for soft sound", "Soft");
        case "Average":
            return KI18n.i18nc("@option:radio the gain for sound of average loudness", "Average");
        case "Loud":
            return KI18n.i18nc("@option:radio the gain for loud sound", "Loud");
        default:
            return KI18n.i18nc("@option:radio the highest level a band may put out", "Ceiling");
        }
    }

    /// A band or point setting's name in the inspector: a point's
    /// frequency, or one of the four settings.
    function fieldName(which: string): string {
        if (which === "Hz") {
            return KI18n.i18nc("@label where a control point is", "Frequency");
        }
        return panel.settingName(which);
    }

    function paramLabel(paramId: string): string {
        switch (paramId) {
        case "speed":
            return KI18n.i18nc("@label how quickly the gain follows the sound", "Speed");
        case "loudnessDb":
            return KI18n.i18nc("@label a gain over every band", "Loudness");
        case "noiseFloorDb":
            return KI18n.i18nc("@label below this level soft sound is not lifted", "Noise floor");
        case "maximumGainDb":
            return KI18n.i18nc("@label the most gain any band is given", "Most gain");
        case "limiterCeilingDb":
            return KI18n.i18nc("@label the level nothing goes above", "Output ceiling");
        default:
            return paramId;
        }
    }

    InfoLine {
        Layout.fillWidth: true
        text: KI18n.i18nc("@info what a Hearing Compensation block does",
                          "Lifts soft sound for one ear, band by band, and holds loud sound under a ceiling.")
    }

    // --- which view, which curve, before/after ---

    // **One row that gives way, never two**: in a narrow card the
    // choice and the button shrink, their words eliding, rather than the
    // row folding onto a second line — which would move the plot under
    // somebody's pointer as the card is resized.
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        SegmentedFrame {
            QQC2.ButtonGroup {
                id: viewGroup
            }

            Repeater {
                model: ["gain", "levels"]

                delegate: SegmentedChoice {
                    required property var modelData

                    text: panel.viewName(modelData)
                    checked: panel.view === modelData
                    QQC2.ButtonGroup.group: viewGroup
                    onClicked: panel.view = modelData
                }
            }
        }

        Item {
            Layout.fillWidth: true
        }

        QQC2.Button {
            id: beforeControl

            // Down to its icon in a narrow card; its words are its name.
            // Filling up to its own width, so the row can shrink it.
            Layout.fillWidth: true
            Layout.minimumWidth: implicitHeight
            Layout.maximumWidth: implicitWidth
            Layout.preferredWidth: implicitWidth
            text: KI18n.i18nc("@action:button hold to hear the sound without the block", "Hold for before")
            icon.name: "media-playback-pause"
            onPressed: panel.beginComparing()
            onReleased: panel.endComparing()
            onCanceled: panel.endComparing()
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip", "The sound without this block, for as long as it is held")
        }
    }

    SegmentedFrame {
        visible: panel.view === "gain"

        QQC2.ButtonGroup {
            id: editingGroup
        }

        Repeater {
            model: bands.settings

            delegate: SegmentedChoice {
                required property var modelData

                text: panel.settingName(modelData)
                checked: panel.editing === modelData
                QQC2.ButtonGroup.group: editingGroup
                onClicked: panel.editing = modelData
            }
        }
    }

    // --- the plot ---

    Item {
        id: plotArea

        Layout.fillWidth: true
        Layout.preferredHeight: Kirigami.Units.gridUnit * 11
        clip: true

        Rectangle {
            anchors.fill: parent
            color: AppColors.well
            radius: AppMetrics.troughRadius
        }

        Repeater {
            model: panel.view === "gain" ? [-20, -10, 0, 10, 20, 30, 40, 50, 60] : [-100, -80, -60, -40, -20, 0]

            delegate: Rectangle {
                required property var modelData

                x: 0
                y: Math.round(panel.view === "gain" ? panel.yOfGain(modelData) : panel.yOfLevel(modelData))
                width: plotArea.width
                height: 1
                color: panel.view === "gain" && modelData === 0 ? AppColors.muted : AppColors.border
            }
        }

        Repeater {
            model: [125, 250, 500, 1000, 2000, 4000, 8000, 16000]

            delegate: Rectangle {
                required property var modelData

                x: Math.round(panel.xOfHz(modelData))
                y: 0
                width: 1
                height: plotArea.height
                color: AppColors.border
            }
        }

        // The sound behind the gains, on a scale of its own.
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            visible: panel.view === "gain" && panel.spectrum.length > 0

            ShapePath {
                strokeColor: Qt.rgba(AppColors.muted.r, AppColors.muted.g, AppColors.muted.b, 0.5)
                strokeWidth: 1
                fillColor: Qt.rgba(AppColors.muted.r, AppColors.muted.g, AppColors.muted.b, 0.22)

                PathPolyline {
                    path: panel.view === "gain" ? panel.spectrumPoints(panel.spectrum, true) : []
                }
            }
        }

        // The gain view's curves.
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            visible: panel.view === "gain"

            ShapePath {
                strokeColor: Qt.rgba(AppColors.muted.r, AppColors.muted.g, AppColors.muted.b, 0.7)
                strokeWidth: 1
                strokeStyle: ShapePath.DashLine
                fillColor: "transparent"

                PathPolyline {
                    id: otherPolyline

                    path: panel.otherBlock !== "" ? panel.line(otherBands, "Average") : []
                }
            }

            ShapePath {
                strokeColor: Qt.rgba(panel.tint.r, panel.tint.g, panel.tint.b, 0.45)
                strokeWidth: 1.5
                fillColor: "transparent"

                PathPolyline {
                    path: panel.line(bands, "Soft")
                }
            }

            ShapePath {
                strokeColor: Qt.rgba(panel.tint.r, panel.tint.g, panel.tint.b, 0.7)
                strokeWidth: 1.5
                fillColor: "transparent"

                PathPolyline {
                    path: panel.line(bands, "Loud")
                }
            }

            ShapePath {
                strokeColor: Qt.rgba(panel.tint.r, panel.tint.g, panel.tint.b, 0.6)
                strokeWidth: 1
                strokeStyle: ShapePath.DashLine
                fillColor: "transparent"

                PathPolyline {
                    path: panel.line(bands, "Ceiling")
                }
            }

            ShapePath {
                strokeColor: panel.tint
                strokeWidth: 2
                fillColor: "transparent"
                joinStyle: ShapePath.RoundJoin
                capStyle: ShapePath.RoundCap

                PathPolyline {
                    id: averagePolyline

                    path: panel.line(bands, "Average")
                }
            }

            // Now: the gain each band is applying at this moment.
            ShapePath {
                strokeColor: AppColors.text
                strokeWidth: 2
                fillColor: "transparent"
                joinStyle: ShapePath.RoundJoin

                PathPolyline {
                    id: nowPolyline

                    path: panel.nowPoints
                }
            }
        }

        // The levels view: the ear, and the sound in and out.
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            visible: panel.view === "levels"

            ShapePath {
                strokeColor: Qt.rgba(AppColors.muted.r, AppColors.muted.g, AppColors.muted.b, 0.6)
                strokeWidth: 1
                fillColor: Qt.rgba(AppColors.muted.r, AppColors.muted.g, AppColors.muted.b, 0.15)

                PathPolyline {
                    path: panel.view === "levels" ? panel.spectrumPoints(panel.inputSpectrum, true) : []
                }
            }

            ShapePath {
                strokeColor: panel.tint
                strokeWidth: 1.5
                fillColor: "transparent"

                PathPolyline {
                    path: panel.view === "levels" ? panel.spectrumPoints(panel.spectrum, false) : []
                }
            }

            ShapePath {
                strokeColor: AppColors.text
                strokeWidth: 2
                fillColor: "transparent"

                PathPolyline {
                    path: panel.thresholdPoints
                }
            }

            ShapePath {
                strokeColor: Qt.rgba(panel.tint.r, panel.tint.g, panel.tint.b, 0.8)
                strokeWidth: 1
                strokeStyle: ShapePath.DashLine
                fillColor: "transparent"

                PathPolyline {
                    path: panel.ceilingPoints
                }
            }
        }

        // A band the fit could not help: more gain is unlikely to.
        Repeater {
            model: panel.view === "gain" ? panel.unhelped : []

            delegate: Kirigami.Icon {
                required property var modelData

                x: panel.xOfHz(modelData) - width / 2
                y: Kirigami.Units.smallSpacing
                width: Kirigami.Units.iconSizes.small
                height: width
                source: "data-warning"
            }
        }

        Repeater {
            id: handleRepeater

            model: panel.view === "gain" ? panel.handleIndices : []

            delegate: Rectangle {
                id: handle

                /// The band, or the point, this is the handle of.
                required property var modelData
                readonly property int index: handle.modelData

                readonly property bool chosen: handle.index === panel.selected
                readonly property point at: plotArea.width > 0 ? panel.handlePoint(handle.index) : Qt.point(0, 0)

                x: handle.at.x - width / 2
                y: handle.at.y - height / 2
                width: handle.chosen ? 12 : 8
                height: width
                radius: width / 2
                color: panel.tint
                border.width: handle.chosen ? 2 : 1
                border.color: handle.chosen ? AppColors.text : panel.tint

                Accessible.role: Accessible.Indicator
                Accessible.name: KI18n.i18nc("@info a band's handle, 'Average at 1.0 kHz: +12.0 dB'", "%1 at %2: %3",
                                             panel.settingName(panel.editing),
                                             panel.hzText(panel.throughPoints ? bands.pointValue(handle.index, "Hz")
                                                                              : bands.centresHz[handle.index]),
                                             panel.gainText(panel.throughPoints
                                                            ? panel.drawnPointGain(handle.index, panel.editing)
                                                            : panel.drawnGain(handle.index, panel.editing)))
            }
        }

        MouseArea {
            anchors.fill: parent
            enabled: panel.view === "gain"
            acceptedButtons: Qt.LeftButton
            preventStealing: true

            onPressed: mouse => {
                const band = panel.bandAt(mouse.x, mouse.y);
                if (band < 0) {
                    // Through points, a double click in the open adds one.
                    mouse.accepted = panel.throughPoints;
                    return;
                }
                panel.pressedX = mouse.x;
                panel.pressedY = mouse.y;
                panel.grabOffset = mouse.y - panel.handlePoint(band).y;
                panel.grabOffsetX = mouse.x - panel.handlePoint(band).x;
                panel.selected = band;
                panel.dragging = band;
            }
            onDoubleClicked: mouse => {
                if (panel.throughPoints && panel.bandAt(mouse.x, mouse.y) < 0) {
                    panel.addPoint(panel.pointHzAtX(mouse.x));
                }
            }
            onPositionChanged: mouse => {
                if (panel.dragging >= 0) {
                    panel.dragTo(panel.dragging, mouse.x, mouse.y);
                }
            }
            onReleased: panel.endDrag()
            onCanceled: panel.endDrag()
        }
    }

    QQC2.Label {
        id: fitWords

        Layout.fillWidth: true
        visible: panel.unhelped.length > 0
        wrapMode: Text.WordWrap
        font: AppFonts.small
        color: AppColors.muted
        text: KI18n.i18ncp("@info bands where the fit asked for more than the block may give",
                           "In the marked band more gain is unlikely to help. Frequency Lowering can move the sound to where it is heard.",
                           "In the %1 marked bands more gain is unlikely to help. Frequency Lowering can move the sound to where it is heard.",
                           panel.unhelped.length)
    }

    // --- the band inspector ---

    ColumnLayout {
        id: inspector

        /// The chosen band's settings, or the chosen point's.
        readonly property var band: panel.selected < 0 ? ({ soft: 0, average: 0, loud: 0, ceiling: 100 })
                                  : panel.throughPoints ? bands.points[panel.selected]
                                                        : bands.all[panel.selected]
        /// Where the chosen band or point is, and the band whose live
        /// values stand for it: a point's nearest.
        readonly property real hz: panel.selected < 0 ? 0
                                 : panel.throughPoints ? bands.pointValue(panel.selected, "Hz")
                                                       : bands.centresHz[panel.selected]
        readonly property int liveBand: {
            if (panel.selected < 0) {
                return -1;
            }
            if (!panel.throughPoints) {
                return panel.selected;
            }
            let nearest = 0;
            for (let band = 1; band < bands.count; ++band) {
                if (Math.abs(Math.log(bands.centresHz[band] / inspector.hz))
                        < Math.abs(Math.log(bands.centresHz[nearest] / inspector.hz))) {
                    nearest = band;
                }
            }
            return nearest;
        }

        visible: panel.selected >= 0
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            Layout.fillWidth: true
            font: AppFonts.heading
            color: AppColors.text
            text: panel.selected < 0 ? ""
                : panel.throughPoints
                ? KI18n.i18nc("@title a control point of a Hearing Compensation block, 'Point at 890 Hz'", "Point at %1",
                              panel.hzText(inspector.hz))
                : KI18n.i18nc("@title a band of a Hearing Compensation block, 'Band 5 at 890 Hz'", "Band %1 at %2",
                              panel.selected + 1, panel.hzText(inspector.hz))
        }

        QQC2.Label {
            Layout.fillWidth: true
            font: AppFonts.small
            color: AppColors.muted
            wrapMode: Text.WordWrap
            text: panel.selected >= 0
                ? KI18n.i18nc("@info how a band compresses: '2.0:1 above -40 dBFS'", "Compresses %1:1 above %2 dBFS",
                              isFinite(bands.ratio(inspector.band)) ? bands.ratio(inspector.band).toFixed(1) : "∞",
                              bands.averageInputDbfs)
                : ""
        }

        // The band's input/output curve, as the Dynamics Processor draws
        // its own: level in across, level out up, from the block's law,
        // with a dot where the band is now.
        Item {
            id: bandCurveArea

            readonly property real quietestDbfs: -90
            readonly property var curve: {
                const points = [];
                if (bandCurveArea.width <= 0 || panel.selected < 0) {
                    return points;
                }
                for (let level = bandCurveArea.quietestDbfs; level <= 0; level += 1) {
                    points.push(Qt.point(bandCurveArea.xOf(level),
                                         bandCurveArea.yOf(level + bands.steadyGain(inspector.band, level))));
                }
                return points;
            }
            /// The band's level and gain now, while the block publishes them.
            readonly property bool live: inspector.liveBand >= 0 && panel.nowLevels.length > inspector.liveBand
            readonly property point now: bandCurveArea.live
                ? Qt.point(bandCurveArea.xOf(panel.nowLevels[inspector.liveBand]),
                           bandCurveArea.yOf(panel.nowLevels[inspector.liveBand] + panel.nowGains[inspector.liveBand]))
                : Qt.point(0, 0)

            function xOf(dbfs: real): real {
                const clamped = Math.max(bandCurveArea.quietestDbfs, Math.min(0, dbfs));
                return bandCurveArea.width * (clamped - bandCurveArea.quietestDbfs) / -bandCurveArea.quietestDbfs;
            }

            function yOf(dbfs: real): real {
                const clamped = Math.max(bandCurveArea.quietestDbfs, Math.min(0, dbfs));
                return bandCurveArea.height * -clamped / -bandCurveArea.quietestDbfs;
            }

            Layout.preferredWidth: Kirigami.Units.gridUnit * 8
            Layout.preferredHeight: Kirigami.Units.gridUnit * 8
            clip: true

            Rectangle {
                anchors.fill: parent
                color: AppColors.well
                radius: AppMetrics.troughRadius
            }

            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer

                // Unchanged, for the curve to be read against.
                ShapePath {
                    strokeColor: AppColors.border
                    strokeWidth: 1
                    fillColor: "transparent"
                    startX: 0
                    startY: bandCurveArea.height

                    PathLine {
                        x: bandCurveArea.width
                        y: 0
                    }
                }

                ShapePath {
                    strokeColor: panel.tint
                    strokeWidth: 2
                    fillColor: "transparent"
                    joinStyle: ShapePath.RoundJoin

                    PathPolyline {
                        id: bandCurvePolyline

                        path: bandCurveArea.curve
                    }
                }
            }

            Rectangle {
                id: bandCurveDot

                visible: bandCurveArea.live
                x: bandCurveArea.now.x - width / 2
                y: bandCurveArea.now.y - height / 2
                width: 8
                height: 8
                radius: width / 2
                color: AppColors.text
            }
        }

        Repeater {
            model: panel.selected < 0 ? [] : panel.throughPoints ? ["Hz"].concat(bands.settings) : bands.settings

            delegate: ParamRow {
                required property var modelData

                readonly property string settingId: panel.throughPoints ? bands.pointParamId(panel.selected, modelData)
                                                                        : bands.paramId(panel.selected, modelData)

                blockId: panel.blockId
                descriptor: BlockCatalogue.describeParam(panel.typeId, settingId)
                label: panel.fieldName(modelData)
                currentValue: panel.params[settingId]
                Layout.fillWidth: true
            }
        }

        QQC2.Button {
            id: removeControl

            visible: panel.throughPoints && panel.selected >= 0
            text: KI18n.i18nc("@action:button take a control point out of the curves", "Remove Point")
            icon.name: "list-remove"
            onClicked: panel.removePoint(panel.selected)
        }
    }

    // --- the settings over every band ---

    Repeater {
        model: ["speed", "loudnessDb", "noiseFloorDb", "maximumGainDb", "limiterCeilingDb"]

        delegate: ParamRow {
            required property var modelData

            blockId: panel.blockId
            descriptor: BlockCatalogue.describeParam(panel.typeId, modelData)
            label: panel.paramLabel(modelData)
            currentValue: panel.params[modelData]
            Layout.fillWidth: true
        }
    }

    // --- the audiogram ---

    CollapsibleSection {
        id: audiogramSection

        Layout.fillWidth: true
        title: KI18n.i18nc("@title:group the quietest levels one ear hears, at each frequency", "Audiogram")

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            ParamRow {
                blockId: panel.blockId
                descriptor: BlockCatalogue.describeParam(panel.typeId, "audiogramUnit")
                label: KI18n.i18nc("@label what the audiogram's levels are measured in", "Measured in")
                currentValue: panel.params.audiogramUnit
                Layout.fillWidth: true
            }

            // **Two lines a frequency**: what was heard on the first,
            // the "too loud" level under it. Five controls on one line asked
            // for more than the card has, and the whole panel was pushed out
            // past its right edge. The choices shrink; the numbers keep the
            // room their digits need.
            GridLayout {
                Layout.fillWidth: true
                columns: 3
                columnSpacing: Kirigami.Units.smallSpacing
                rowSpacing: 0

                Repeater {
                    model: panel.audiogramHz

                    delegate: QQC2.Label {
                        required property var modelData
                        required property int index

                        Layout.row: index * 2
                        Layout.column: 0
                        text: panel.hzText(modelData)
                        color: AppColors.text
                    }
                }

                Repeater {
                    model: panel.audiogramHz

                    delegate: QQC2.ComboBox {
                        id: markBox

                        required property var modelData
                        required property int index

                        readonly property string paramId: "audiogram" + modelData

                        Layout.row: index * 2
                        Layout.column: 1
                        Layout.fillWidth: true
                        Layout.minimumWidth: Kirigami.Units.gridUnit * 4
                        model: [KI18n.i18nc("@item:inlistbox nothing measured at this frequency", "—"),
                                KI18n.i18nc("@item:inlistbox heard at the level given", "Heard"),
                                KI18n.i18nc("@item:inlistbox nothing heard even at the loudest level tried", "No response")]
                        currentIndex: ["none", "heard", "noResponse"].indexOf(panel.params[markBox.paramId] ?? "none")
                        onActivated: index => SessionModel.setParam(panel.blockId, markBox.paramId,
                                                                   ["none", "heard", "noResponse"][index])
                    }
                }

                Repeater {
                    model: panel.audiogramHz

                    delegate: QQC2.SpinBox {
                        id: levelBox

                        required property var modelData
                        required property int index

                        readonly property string paramId: "audiogram" + modelData + "Db"

                        Layout.row: index * 2
                        Layout.column: 2
                        Layout.preferredWidth: panel.audiogramNumberWidth
                        enabled: (panel.params["audiogram" + modelData] ?? "none") !== "none"
                        from: -140
                        to: 140
                        editable: true
                        value: panel.params[levelBox.paramId] ?? 0
                        onValueModified: SessionModel.setParam(panel.blockId, levelBox.paramId, levelBox.value)
                        Accessible.name: KI18n.i18nc("@label:spinbox the quietest level heard at a frequency",
                                                     "Heard at, %1", panel.hzText(modelData))
                    }
                }

                Repeater {
                    model: panel.audiogramHz

                    delegate: QQC2.CheckBox {
                        id: tooLoudBox

                        required property var modelData
                        required property int index

                        Layout.row: index * 2 + 1
                        Layout.column: 1
                        Layout.fillWidth: true
                        Layout.minimumWidth: Kirigami.Units.gridUnit * 4
                        text: KI18n.i18nc("@option:check a 'too loud' level was measured at this frequency", "Too loud given")
                        // Whole on hover when a narrow card cuts it.
                        QQC2.ToolTip.text: tooLoudBox.text
                        QQC2.ToolTip.visible: tooLoudBox.hovered && tooLoudBox.implicitContentWidth > tooLoudBox.availableWidth
                        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                        checked: panel.params["audiogram" + modelData + "TooLoud"] === true
                        onToggled: SessionModel.setParam(panel.blockId, "audiogram" + modelData + "TooLoud", checked)
                    }
                }

                Repeater {
                    model: panel.audiogramHz

                    delegate: QQC2.SpinBox {
                        id: tooLoudLevel

                        required property var modelData
                        required property int index

                        readonly property string paramId: "audiogram" + modelData + "TooLoudDb"

                        Layout.row: index * 2 + 1
                        Layout.column: 2
                        Layout.preferredWidth: panel.audiogramNumberWidth
                        enabled: panel.params["audiogram" + modelData + "TooLoud"] === true
                        from: -140
                        to: 140
                        editable: true
                        value: panel.params[tooLoudLevel.paramId] ?? 0
                        onValueModified: SessionModel.setParam(panel.blockId, tooLoudLevel.paramId, tooLoudLevel.value)
                        Accessible.name: KI18n.i18nc("@label:spinbox the level at which sound becomes too loud",
                                                     "Too loud at, %1", panel.hzText(modelData))
                    }
                }
            }

            QQC2.Button {
                id: fitControl

                text: KI18n.i18nc("@action:button", "Fit from Audiogram")
                icon.name: "draw-freehand"
                onClicked: panel.fit()
            }
        }
    }

    QQC2.Button {
        id: copyControl

        enabled: panel.otherBlock !== ""
        text: KI18n.i18nc("@action:button take the other ear's block's band settings", "Copy from the Other Ear")
        icon.name: "edit-copy"
        onClicked: panel.copyFromOtherEar()
    }
}
