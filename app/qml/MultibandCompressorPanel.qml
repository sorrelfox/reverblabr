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

// The Multiband Compressor's panel.
//
// - **The bands**, across the frequencies from 20 Hz to 20 kHz: where each
//   one is, with how much it is holding back hanging from the top of it, and
//   the two splits as handles to drag. A press on a band picks it.
// - **The picked band's curve**, drawn by the Dynamics Processor's own plot
//   from the band's threshold, ratio and makeup and the shared knee — the
//   curve its compressor follows — with where the band's sound is on it
//   while the session runs.
// - The picked band's controls, then the knee and the splits as rows a
//   keyboard can reach.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property string typeId: "effect.multibandcompressor"
    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(panel.blockId) : ({})

    /// What a drag of a split has sent that the session has not said yet;
    /// dropped the moment it does, as the Dynamics panel does.
    property var edits: ({})
    readonly property var settings: Object.assign({}, panel.params, panel.edits)
    onParamsChanged: panel.edits = ({})

    /// Which band the curve and the controls are for: 0 low, 1 middle, 2 high.
    property int band: 1
    readonly property string bandId: words.bandIds[panel.band]

    /// Which split is being dragged: "lowCrossover", "highCrossover", or "".
    property string dragging: ""

    readonly property var levels: Meters.frame >= 0 ? Meters.levels(panel.blockId) : ({})
    readonly property bool active: Meters.live && (panel.levels.active ?? false)
    readonly property var reductions: [0, 1, 2].map(which => {
        const values = panel.levels.values ?? [];
        return panel.active && which < values.length ? Math.min(0, values[which]) : 0;
    })
    /// While the panel asks: three values a band, low first — the band's
    /// level on its curve, how high it comes out, what is taken off
    /// (MultibandCompressor::kValuesPerBand).
    readonly property var bandValues: Meters.frame >= 0 ? Meters.bandValues(panel.blockId) : []
    readonly property bool liveCurve: panel.active && panel.bandValues.length >= 9
    readonly property real bandInputDb: panel.liveCurve ? panel.bandValues[panel.band * 3] : curve.lowestDb

    readonly property real lowSplit: panel.value("lowCrossover")
    readonly property real highSplit: panel.value("highCrossover")
    readonly property real lowestHz: 20
    readonly property real highestHz: 20000
    /// How close the splits may come: the block holds the upper at least
    /// this many times the lower (dsp::kMinCrossoverSpacing), and a drag
    /// stops there rather than being moved by the engine afterwards.
    readonly property real splitSpacing: 1.5

    /// The band's settings as the Dynamics plot reads them.
    readonly property var curveParams: ({
        threshold: panel.value(panel.bandId + "Threshold"),
        ratio: panel.value(panel.bandId + "Ratio"),
        makeup: panel.value(panel.bandId + "Makeup"),
        knee: panel.value("knee")
    })

    readonly property color tint: AppColors.categoryTint(BlockPresentation.category(panel.typeId))

    /// The parts a test reaches for.
    readonly property alias words: words
    readonly property alias strip: strip
    readonly property alias curve: curve
    readonly property alias bandRows: bandRows

    spacing: AppMetrics.panelRowGap

    onVisibleChanged: panel.askForBands()
    onBlockIdChanged: panel.askForBands()
    Component.onCompleted: panel.askForBands()
    Component.onDestruction: panel.letGoOfBands()

    MultibandWords {
        id: words
    }

    /// A parameter as the panel draws it: an edit being dragged, the
    /// session's value, or the block's default.
    function value(paramId: string): real {
        const held = panel.settings[paramId];
        return held === undefined ? (BlockCatalogue.describeParam(panel.typeId, paramId).default ?? 0) : held;
    }

    /// Asks for the bands' levels while the panel is shown, and lets go
    /// while it is not; nothing is kept for a panel nobody is looking at.
    function askForBands() {
        if (panel.visible && panel.blockId !== "") {
            Meters.analyse(panel.blockId, -1);
        } else {
            panel.letGoOfBands();
        }
    }

    function letGoOfBands() {
        if (panel.blockId !== "" && Meters.analysed() === panel.blockId) {
            Meters.analyse("");
        }
    }

    function paramLabel(paramId: string): string {
        const field = paramId.startsWith(panel.bandId) ? paramId.slice(panel.bandId.length) : paramId;
        switch (field) {
        case "Threshold":
            return KI18n.i18nc("@label where a band's compressor starts working", "Threshold");
        case "Ratio":
            return KI18n.i18nc("@label how much of what goes over the threshold comes out", "Ratio");
        case "Attack":
            return KI18n.i18nc("@label how fast a band's compressor reacts to the band getting louder", "Attack");
        case "Release":
            return KI18n.i18nc("@label how fast a band's compressor lets go once the band is quieter", "Release");
        case "Makeup":
            return KI18n.i18nc("@label gain added to a band after it has been held back", "Makeup");
        case "knee":
            return KI18n.i18nc("@label how wide the bend around every band's threshold is", "Knee");
        case "lowCrossover":
            return KI18n.i18nc("@label where the low band ends and the middle begins", "Low split");
        case "highCrossover":
            return KI18n.i18nc("@label where the middle band ends and the high begins", "High split");
        }
        return paramId;
    }

    // --- frequencies across the strip ---

    function xOfHz(hz: real): real {
        const at = Math.log(Math.max(hz, panel.lowestHz) / panel.lowestHz) / Math.log(panel.highestHz / panel.lowestHz);
        return strip.width * Math.max(0, Math.min(1, at));
    }

    function hzAtX(x: real): real {
        const at = Math.max(0, Math.min(1, x / Math.max(1, strip.width)));
        return panel.lowestHz * Math.pow(panel.highestHz / panel.lowestHz, at);
    }

    /// Where a split may go when dragged to \a hz: inside its own range, and
    /// no nearer the other split than the block allows.
    function splitAt(paramId: string, hz: real): real {
        const declared = BlockCatalogue.describeParam(panel.typeId, paramId);
        let wanted = Math.max(declared.minimum ?? hz, Math.min(declared.maximum ?? hz, hz));
        if (paramId === "lowCrossover") {
            wanted = Math.min(wanted, panel.highSplit / panel.splitSpacing);
        } else {
            wanted = Math.max(wanted, panel.lowSplit * panel.splitSpacing);
        }
        return Math.round(wanted);
    }

    function dragSplit(paramId: string, hz: real) {
        const at = panel.splitAt(paramId, hz);
        const next = Object.assign({}, panel.edits);
        next[paramId] = at;
        panel.edits = next;
        SessionModel.setParam(panel.blockId, paramId, at, true);
    }

    /// Which band a place on the strip is in.
    function bandAtX(x: real): int {
        const hz = panel.hzAtX(x);
        return hz < panel.lowSplit ? 0 : (hz < panel.highSplit ? 1 : 2);
    }

    // --- the bands ---

    Item {
        id: strip

        Layout.fillWidth: true
        Layout.preferredHeight: Kirigami.Units.gridUnit * 4

        Accessible.role: Accessible.Graphic
        Accessible.name: KI18n.i18nc("@info accessible name of a multiband compressor's bands", "Bands")
        Accessible.description: [0, 1, 2].map(which => KI18n.i18nc(
            "@info a band, where it is, and how much it holds back now", "%1, %2, %3",
            words.bandName(which), words.rangeText(which, panel.lowSplit, panel.highSplit),
            words.reductionText(panel.reductions[which]))).join("; ")

        Rectangle {
            anchors.fill: parent
            radius: AppMetrics.barRadius
            color: AppColors.well
        }

        Repeater {
            model: 3

            delegate: Item {
                id: region

                required property int index

                readonly property real startX: region.index === 0 ? 0 : panel.xOfHz(region.index === 1 ? panel.lowSplit : panel.highSplit)
                readonly property real endX: region.index === 2 ? strip.width
                                                                 : panel.xOfHz(region.index === 0 ? panel.lowSplit : panel.highSplit)

                objectName: "band" + region.index
                x: region.startX
                width: Math.max(0, region.endX - region.startX)
                height: strip.height

                // The picked band stands out from the other two.
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 1
                    radius: AppMetrics.barRadius
                    color: Qt.rgba(panel.tint.r, panel.tint.g, panel.tint.b, region.index === panel.band ? 0.16 : 0.05)
                }

                // How much the band holds back, hanging from the top.
                Rectangle {
                    objectName: "reductionBar"
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: 1
                    width: Math.max(4, Math.min(Kirigami.Units.gridUnit, region.width / 3))
                    height: Math.round((strip.height - 2) * words.reductionFraction(panel.reductions[region.index]))
                    radius: AppMetrics.barRadius
                    color: panel.tint
                }

                QQC2.Label {
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Kirigami.Units.smallSpacing
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width
                    text: panel.active ? words.reductionText(panel.reductions[region.index]) : words.bandName(region.index)
                    font: AppFonts.small
                    color: region.index === panel.band ? AppColors.text : AppColors.muted
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    Accessible.ignored: true
                }
            }
        }

        // The two splits, as handles.
        Repeater {
            model: ["lowCrossover", "highCrossover"]

            delegate: Rectangle {
                id: handle

                required property string modelData

                objectName: handle.modelData
                x: Math.round(panel.xOfHz(handle.modelData === "lowCrossover" ? panel.lowSplit : panel.highSplit)) - width / 2
                width: panel.dragging === handle.modelData ? 3 : 2
                height: strip.height
                color: panel.dragging === handle.modelData ? AppColors.accentMark : AppColors.text
                opacity: panel.dragging === handle.modelData ? 1 : 0.6
            }
        }

        MouseArea {
            id: stripArea

            /// How near a split a press has to be to take hold of it.
            readonly property real grab: Kirigami.Units.gridUnit * 0.6

            anchors.fill: parent
            preventStealing: true
            hoverEnabled: true
            cursorShape: panel.dragging !== "" || stripArea.nearSplit(stripArea.mouseX) !== ""
                ? Qt.SizeHorCursor : Qt.PointingHandCursor

            function nearSplit(x: real): string {
                const toLow = Math.abs(x - panel.xOfHz(panel.lowSplit));
                const toHigh = Math.abs(x - panel.xOfHz(panel.highSplit));
                if (Math.min(toLow, toHigh) > stripArea.grab) {
                    return "";
                }
                return toLow <= toHigh ? "lowCrossover" : "highCrossover";
            }

            onPressed: mouse => {
                panel.dragging = stripArea.nearSplit(mouse.x);
                if (panel.dragging === "") {
                    panel.band = panel.bandAtX(mouse.x);
                }
            }
            onPositionChanged: mouse => {
                if (panel.dragging !== "") {
                    panel.dragSplit(panel.dragging, panel.hzAtX(mouse.x));
                }
            }
            onReleased: {
                if (panel.dragging !== "") {
                    SessionModel.commitParam(panel.blockId, panel.dragging);
                }
                panel.dragging = "";
            }
            onCanceled: {
                if (panel.dragging !== "") {
                    SessionModel.commitParam(panel.blockId, panel.dragging);
                }
                panel.dragging = "";
            }
        }
    }

    // --- which band ---

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        SegmentedFrame {
            QQC2.ButtonGroup {
                id: bandGroup
            }

            Repeater {
                model: 3

                delegate: SegmentedChoice {
                    required property int index

                    text: words.bandName(index)
                    checked: panel.band === index
                    QQC2.ButtonGroup.group: bandGroup
                    Accessible.description: words.rangeText(index, panel.lowSplit, panel.highSplit)
                    onClicked: panel.band = index
                }
            }
        }

        ElidedLabel {
            text: words.rangeText(panel.band, panel.lowSplit, panel.highSplit)
            color: AppColors.muted
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignRight
            Layout.fillWidth: true
        }
    }

    // --- the band's curve ---

    // A binding that only calls xOfDb() or pointAt() has nothing to depend
    // on; the tessellation count changes whenever the mapping does, and
    // brings it back (DynamicsPanel).
    DynamicsPlot {
        id: curve

        params: panel.curveParams
        plotWidth: plotArea.width
        plotHeight: plotArea.height
    }

    Item {
        id: plotArea

        Layout.fillWidth: true
        Layout.preferredHeight: Math.round(plotArea.width * 0.5)

        Accessible.role: Accessible.Graphic
        Accessible.name: KI18n.i18nc("@info accessible name of a band's compressor curve; the band's name",
                                     "%1 band's curve", words.bandName(panel.band))

        Rectangle {
            anchors.fill: parent
            radius: AppMetrics.barRadius
            color: AppColors.well
        }

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

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
                strokeColor: panel.tint
                strokeWidth: Math.max(1, Kirigami.Units.smallSpacing / 2)
                fillColor: "transparent"
                joinStyle: ShapePath.RoundJoin

                PathPolyline {
                    path: curve.curve
                }
            }
        }

        // Where the band's sound is on its curve, this frame.
        Rectangle {
            id: marker

            readonly property point at: curve.tessellations >= 0 ? curve.pointAt(panel.bandInputDb) : Qt.point(0, 0)

            objectName: "liveMarker"
            visible: panel.liveCurve
            x: marker.at.x - width / 2
            y: marker.at.y - height / 2
            width: Kirigami.Units.gridUnit * 0.5
            height: width
            radius: width / 2
            color: AppColors.liveMark
        }
    }

    // --- the band's controls, then the shared ones ---

    ColumnLayout {
        id: bandRows

        Layout.fillWidth: true
        spacing: AppMetrics.panelRowGap

        Repeater {
            model: ["Threshold", "Ratio", "Attack", "Release", "Makeup"].map(field => panel.bandId + field)
                .concat(["knee", "lowCrossover", "highCrossover"])

            delegate: ParamRow {
                required property string modelData

                Layout.fillWidth: true
                blockId: panel.blockId
                descriptor: BlockCatalogue.describeParam(panel.typeId, modelData)
                label: panel.paramLabel(modelData)
                currentValue: panel.params[modelData]
            }
        }
    }

    InfoLine {
        text: KI18n.i18nc("@info what the Multiband Compressor block does",
                          "Holds the low, the middle and the high range back each by its own amount, so a loud bass does not pull the voice down with it and a sharp \"s\" can be tamed without dulling the rest. With every ratio at 1:1 the sound comes out as it went in.")
    }
}
