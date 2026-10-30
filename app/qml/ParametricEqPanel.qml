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

// The Parametric EQ's settings: the response, with a handle on each
// band to drag; a chip for each band to pick it by; and the picked band's
// type, switch, frequency, gain and Q.
//
// **The curve is the block's**, from EqualizerPlot, and the handles are
// placed by the same plot: a handle sits at its band's frequency and, for a
// band with a gain, at that gain — for one without (a pass filter, a notch,
// a band-pass) on the 0 dB line.
//
// **The sound behind the curve**: the spectrum of what the block puts
// out, drawn in the design's muted colour under the line while the session
// runs. The panel asks for it while it is shown and lets go when it closes
// or hides, and the engine copies the block's output out for nobody else —
// so a spectrum costs nothing while no curve is open.
//
// **One grab over the whole plot**, as the Dynamics panel has: the
// band nearest the pointer when the button goes down is the one that moves
// until it comes up, read in the plot's own coordinates, and the gesture is
// kept from the scroll view the panel sits in. Across is frequency, up and
// down is gain; the wheel over a band is its width.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""
    /// The band being edited below the curve: a slot, or -1.
    property int selected: -1
    /// The band a drag is moving, or -1.
    property int dragging: -1
    /// Where the press was, and how far from the handle's centre: a drag
    /// moves the handle by how far the pointer has moved, rather than
    /// snapping its centre to the pointer.
    property point pressedAt: Qt.point(0, 0)
    property point grabOffset: Qt.point(0, 0)

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(panel.blockId) : ({})
    readonly property color tint: AppColors.categoryTint(BlockPresentation.category("advanced.parametriceq"))
    /// A curve wants a wider card than a list of settings.
    readonly property real panelWidth: AppMetrics.followsSystem ? Kirigami.Units.gridUnit * 24 : 420
    /// How near a band's handle a press has to be to take it.
    readonly property real grabRadius: Kirigami.Units.gridUnit

    readonly property string selectedType: panel.selected >= 0 ? bands.value(panel.selected, "Type") : "peak"

    /// The parts a test drives, as a person would.
    readonly property alias plot: plot
    readonly property alias bands: bands
    readonly property alias words: words
    readonly property alias plotArea: plotArea
    readonly property alias chips: chipRepeater
    readonly property alias addButton: addControl
    readonly property alias deleteButton: deleteControl
    readonly property alias typeBox: typeControl
    readonly property alias onSwitch: onControl
    readonly property alias frequencySlider: frequencyControl
    readonly property alias gainSlider: gainControl
    readonly property alias qSlider: qControl
    readonly property alias spectrumShape: spectrumShape

    spacing: Kirigami.Units.largeSpacing

    // The lowest band in use is the one shown first, and a band that goes
    // away hands the selection to the nearest one left.
    //
    // **Picked once the block's settings have arrived**, not in the same
    // breath as the block id: the bands are read from the session through
    // two bindings, and asked at once they are still the defaults — which
    // picked the band that used to be lowest.
    //
    // **On a timer the panel owns**, not Qt.callLater: a panel made and
    // closed in the same turn — every block's panel opened in turn, as the
    // canvas tests do — left a call behind that ran after it was gone and
    // threw a TypeError reaching for its bands. A timer goes with
    // the panel.
    onBlockIdChanged: {
        panel.selected = -1;
        selectOnceSettled.restart();
        panel.askForSpectrum();
    }

    Timer {
        id: selectOnceSettled

        interval: 0
        onTriggered: panel.selectSomething()
    }
    onVisibleChanged: panel.askForSpectrum()
    Component.onCompleted: panel.askForSpectrum()
    Component.onDestruction: panel.letGoOfSpectrum()

    /// Asks for this block's spectrum while the panel is shown, and lets go
    /// of it while it is not.
    function askForSpectrum() {
        if (panel.visible && panel.blockId !== "") {
            Meters.analyse(panel.blockId);
        } else {
            panel.letGoOfSpectrum();
        }
    }

    /// Lets go only of this block's: another panel may have asked for its
    /// own since.
    function letGoOfSpectrum() {
        if (panel.blockId !== "" && Meters.analysed() === panel.blockId) {
            Meters.analyse("");
        }
    }
    Connections {
        target: bands

        function onUsedChanged() {
            if (bands.used.indexOf(panel.selected) < 0) {
                panel.selectSomething();
            }
        }
    }

    FilterWords {
        id: words
    }

    EqualizerPlot {
        id: plot

        params: panel.params
        plotWidth: plotArea.width
        plotHeight: plotArea.height
        rangeDb: 24
        selectedBand: panel.selected
        // Read again on every frame the meters tick, and empty — so nothing
        // is drawn — when the session is not publishing this block's.
        spectrum: Meters.frame >= 0 ? Meters.spectrum(panel.blockId) : []
    }

    ParametricEqBands {
        id: bands

        params: panel.params
        plot: plot
    }

    function selectSomething() {
        const used = bands.used;
        if (used.length === 0) {
            panel.selected = -1;
            return;
        }
        // Nearest in frequency to what was selected, or the lowest.
        if (panel.selected >= 0) {
            const was = bands.value(panel.selected, "Frequency");
            panel.selected = used.reduce((best, band) => Math.abs(Math.log(bands.value(band, "Frequency") / was))
                                                         < Math.abs(Math.log(bands.value(best, "Frequency") / was))
                                                         ? band : best, used[0]);
            return;
        }
        panel.selected = used[0];
    }

    /// Where a band's handle is on the plot.
    function handlePoint(band: int): point {
        const type = bands.value(band, "Type");
        const db = words.hasGain(type) ? bands.value(band, "Gain") : 0;
        return Qt.point(plot.xOfHz(bands.value(band, "Frequency")), plot.yOfDb(db));
    }

    /// The band whose handle is nearest (x, y), within reach, or -1.
    function bandAt(x: real, y: real): int {
        let nearest = -1;
        let distance = panel.grabRadius;
        for (const band of bands.used) {
            const point = panel.handlePoint(band);
            const away = Math.hypot(point.x - x, point.y - y);
            if (away <= distance) {
                distance = away;
                nearest = band;
            }
        }
        return nearest;
    }

    function set(band: int, which: string, value: var, live: bool) {
        SessionModel.setParam(panel.blockId, plot.paramId(band, which), value, live);
    }

    function commit(band: int, which: string) {
        SessionModel.commitParam(panel.blockId, plot.paramId(band, which));
    }

    /// Moves a band with the pointer at (x, y): across is frequency, and up
    /// and down is gain for a band that has one.
    ///
    /// **Each axis only once the pointer has moved along it**, and by the
    /// grab's offset: a drag straight up used to write the frequency back
    /// from where the pointer was rather than where the handle was, and a
    /// band at 1.00 kHz came out of it at 998 Hz.
    function dragTo(band: int, x: real, y: real) {
        if (x !== panel.pressedAt.x) {
            panel.set(band, "Frequency", plot.hzAtX(x - panel.grabOffset.x), true);
        }
        if (y !== panel.pressedAt.y && words.hasGain(bands.value(band, "Type"))) {
            const db = Math.max(-18, Math.min(18, plot.dbAtY(y - panel.grabOffset.y)));
            panel.set(band, "Gain", Math.round(db * 10) / 10, true);
        }
    }

    function endDrag() {
        if (panel.dragging >= 0) {
            panel.commit(panel.dragging, "Frequency");
            panel.commit(panel.dragging, "Gain");
        }
        panel.dragging = -1;
    }

    /// A band in the free slot, a bell at 0 dB in the widest gap, as one
    /// step to undo; and it is the one selected.
    function addBand() {
        const band = bands.free;
        if (band < 0) {
            return;
        }
        SessionModel.setParams(panel.blockId, [
            [plot.paramId(band, "Frequency"), Math.round(bands.roomFor())],
            [plot.paramId(band, "Type"), "peak"],
            [plot.paramId(band, "Gain"), 0],
            [plot.paramId(band, "Q"), 1],
            [plot.paramId(band, "On"), true],
            [plot.paramId(band, "Used"), true]
        ]);
        panel.selected = band;
    }

    function deleteBand() {
        if (panel.selected >= 0) {
            SessionModel.setParam(panel.blockId, plot.paramId(panel.selected, "Used"), false);
        }
    }

    // --- the curve ---

    Item {
        id: plotArea

        Layout.fillWidth: true
        Layout.preferredHeight: Kirigami.Units.gridUnit * 10
        clip: true

        Rectangle {
            anchors.fill: parent
            color: AppColors.well
            radius: AppMetrics.troughRadius
        }

        // The grid: a line every 6 dB, 0 dB a shade stronger, and one at
        // each power of ten in hertz.
        Repeater {
            model: [-18, -12, -6, 0, 6, 12, 18]

            delegate: Rectangle {
                required property var modelData

                x: 0
                y: plot.tessellations >= 0 ? Math.round(plot.yOfDb(modelData)) : 0
                width: plotArea.width
                height: 1
                color: modelData === 0 ? AppColors.muted : AppColors.border
            }
        }

        Repeater {
            model: [100, 1000, 10000]

            delegate: Rectangle {
                required property var modelData

                x: plot.tessellations >= 0 ? Math.round(plot.xOfHz(modelData)) : 0
                y: 0
                width: 1
                height: plotArea.height
                color: AppColors.border
            }
        }

        // The sound, behind everything that is drawn on top of it.
        Shape {
            id: spectrumShape

            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            visible: plot.spectrumArea.length > 0

            ShapePath {
                strokeColor: Qt.rgba(AppColors.muted.r, AppColors.muted.g, AppColors.muted.b, 0.5)
                strokeWidth: 1
                fillColor: Qt.rgba(AppColors.muted.r, AppColors.muted.g, AppColors.muted.b, 0.22)
                joinStyle: ShapePath.RoundJoin

                PathPolyline {
                    path: plot.spectrumArea
                }
            }
        }

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            // The selected band's own part, faint, under the whole.
            ShapePath {
                strokeColor: Qt.rgba(panel.tint.r, panel.tint.g, panel.tint.b, 0.55)
                strokeWidth: 1
                fillColor: "transparent"

                PathPolyline {
                    path: plot.bandCurve
                }
            }

            ShapePath {
                strokeColor: AppColors.text
                strokeWidth: 2
                fillColor: "transparent"
                joinStyle: ShapePath.RoundJoin
                capStyle: ShapePath.RoundCap

                PathPolyline {
                    path: plot.curve
                }
            }
        }

        Repeater {
            id: handleRepeater

            model: bands.used

            delegate: Rectangle {
                id: handle

                required property var modelData

                readonly property int band: modelData
                readonly property bool on: bands.value(handle.band, "On") !== false
                readonly property bool chosen: handle.band === panel.selected
                readonly property point at: plot.tessellations >= 0 ? panel.handlePoint(handle.band) : Qt.point(0, 0)

                x: handle.at.x - width / 2
                y: handle.at.y - height / 2
                width: handle.chosen ? 14 : 10
                height: width
                radius: width / 2
                color: handle.on ? panel.tint : AppColors.well
                border.width: handle.chosen ? 2 : 1
                border.color: handle.chosen ? AppColors.text : panel.tint

                Accessible.role: Accessible.Indicator
                Accessible.name: KI18n.i18nc("@info a Parametric EQ band's handle, 'Bell at 250 Hz'", "%1 at %2",
                                             words.bandTypeName(bands.value(handle.band, "Type")),
                                             words.frequencyText(bands.value(handle.band, "Frequency")))
            }
        }

        MouseArea {
            id: plotGrab

            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            // **The gesture is ours once it has begun**, or the scroll view
            // the panel sits in takes the drag away after a few pixels.
            preventStealing: true

            onPressed: mouse => {
                const band = panel.bandAt(mouse.x, mouse.y);
                if (band < 0) {
                    mouse.accepted = false;
                    return;
                }
                const centre = panel.handlePoint(band);
                panel.pressedAt = Qt.point(mouse.x, mouse.y);
                panel.grabOffset = Qt.point(mouse.x - centre.x, mouse.y - centre.y);
                panel.selected = band;
                panel.dragging = band;
            }

            onPositionChanged: mouse => {
                if (panel.dragging >= 0) {
                    panel.dragTo(panel.dragging, mouse.x, mouse.y);
                }
            }

            onReleased: panel.endDrag()
            onCanceled: panel.endDrag()

            // The wheel over a band is its width: up is narrower, a sixth
            // of an octave's worth of Q a notch.
            onWheel: wheel => {
                const band = panel.bandAt(wheel.x, wheel.y);
                if (band < 0) {
                    wheel.accepted = false;
                    return;
                }
                panel.selected = band;
                const q = bands.value(band, "Q") * Math.pow(2, wheel.angleDelta.y / 120 / 6);
                panel.set(band, "Q", Math.max(0.1, Math.min(18, q)), false);
            }
        }
    }

    // --- the bands, to pick one by ---

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Flow {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Repeater {
                id: chipRepeater

                model: bands.used

                delegate: QQC2.Button {
                    required property var modelData

                    readonly property int band: modelData

                    text: words.frequencyText(bands.frequency(band))
                    checkable: true
                    checked: band === panel.selected
                    highlighted: checked
                    flat: !checked
                    opacity: bands.value(band, "On") !== false ? 1 : AppMetrics.disabledOpacity
                    font: AppFonts.small
                    Accessible.name: KI18n.i18nc("@action:button pick a Parametric EQ band, 'Bell at 250 Hz'", "%1 at %2",
                                                 words.bandTypeName(bands.value(band, "Type")), text)
                    onClicked: panel.selected = band
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Button {
            id: addControl

            text: KI18n.i18nc("@action:button add a band to a Parametric EQ", "Add Band")
            icon.name: "list-add-symbolic"
            enabled: bands.free >= 0
            onClicked: panel.addBand()
        }

        Item {
            Layout.fillWidth: true
        }

        QQC2.Button {
            id: deleteControl

            text: KI18n.i18nc("@action:button remove the selected band from a Parametric EQ", "Delete Band")
            icon.name: "list-remove-symbolic"
            enabled: panel.selected >= 0
            onClicked: panel.deleteBand()
        }
    }

    // --- the selected band ---

    Kirigami.FormLayout {
        Layout.fillWidth: true
        enabled: panel.selected >= 0

        RowLayout {
            Kirigami.FormData.label: KI18n.i18nc("@label:listbox the type of a Parametric EQ band", "Type:")
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            QQC2.ComboBox {
                id: typeControl

                Layout.fillWidth: true
                model: words.bandTypes
                displayText: words.bandTypeName(panel.selectedType)

                delegate: QQC2.ItemDelegate {
                    required property string modelData
                    required property int index

                    width: typeControl.width
                    text: words.bandTypeName(modelData)
                    highlighted: typeControl.highlightedIndex === index
                }

                onActivated: panel.set(panel.selected, "Type", String(words.bandTypes[typeControl.currentIndex]), false)

                SessionBound {
                    control: typeControl
                    property: "currentIndex"
                    value: Math.max(0, words.bandTypes.indexOf(panel.selectedType))
                }
            }

            QQC2.Switch {
                id: onControl

                checked: panel.selected >= 0 && bands.value(panel.selected, "On") !== false
                Accessible.name: KI18n.i18nc("@option:check a Parametric EQ band is switched on", "Band on")
                onToggled: panel.set(panel.selected, "On", checked, false)
            }
        }

        RowLayout {
            Kirigami.FormData.label: KI18n.i18nc("@label:slider a Parametric EQ band's frequency", "Frequency:")
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Slider {
                id: frequencyControl

                Layout.fillWidth: true
                from: 0
                to: 1
                Accessible.name: KI18n.i18nc("@label:slider a Parametric EQ band's frequency", "Frequency")

                onMoved: panel.set(panel.selected, "Frequency", words.frequencyAt(value, plot.lowestHz, plot.highestHz), true)
                onPressedChanged: if (!pressed) {
                    panel.commit(panel.selected, "Frequency");
                }

                SessionBound {
                    control: frequencyControl
                    property: "value"
                    value: panel.selected >= 0
                        ? words.positionOf(bands.value(panel.selected, "Frequency"), plot.lowestHz, plot.highestHz) : 0.5
                    suspended: frequencyControl.pressed
                }
            }

            QQC2.Label {
                text: words.frequencyText(words.frequencyAt(frequencyControl.value, plot.lowestHz, plot.highestHz))
                font.family: AppFonts.monoFamily
                horizontalAlignment: Text.AlignRight
                Layout.preferredWidth: Kirigami.Units.gridUnit * 4
            }
        }

        RowLayout {
            Kirigami.FormData.label: KI18n.i18nc("@label:slider a Parametric EQ band's gain", "Gain:")
            Layout.fillWidth: true
            enabled: words.hasGain(panel.selectedType)
            spacing: Kirigami.Units.smallSpacing

            QQC2.Slider {
                id: gainControl

                Layout.fillWidth: true
                from: -18
                to: 18
                stepSize: 0.1
                Accessible.name: KI18n.i18nc("@label:slider a Parametric EQ band's gain", "Gain")

                onMoved: panel.set(panel.selected, "Gain", Math.round(value * 10) / 10, true)
                onPressedChanged: if (!pressed) {
                    panel.commit(panel.selected, "Gain");
                }

                SessionBound {
                    control: gainControl
                    property: "value"
                    value: panel.selected >= 0 ? bands.value(panel.selected, "Gain") : 0
                    suspended: gainControl.pressed
                }
            }

            QQC2.Label {
                text: words.gainText(gainControl.value)
                font.family: AppFonts.monoFamily
                horizontalAlignment: Text.AlignRight
                Layout.preferredWidth: Kirigami.Units.gridUnit * 4
            }
        }

        RowLayout {
            Kirigami.FormData.label: KI18n.i18nc("@label:slider how narrow a Parametric EQ band is", "Q:")
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Slider {
                id: qControl

                Layout.fillWidth: true
                // Logarithmic, as a width is heard: 0.1 to 18.
                from: 0
                to: 1
                Accessible.name: KI18n.i18nc("@label:slider how narrow a Parametric EQ band is", "Q")

                onMoved: panel.set(panel.selected, "Q", words.frequencyAt(value, 0.1, 18), true)
                onPressedChanged: if (!pressed) {
                    panel.commit(panel.selected, "Q");
                }

                SessionBound {
                    control: qControl
                    property: "value"
                    value: panel.selected >= 0 ? words.positionOf(bands.value(panel.selected, "Q"), 0.1, 18) : 0.5
                    suspended: qControl.pressed
                }
            }

            QQC2.Label {
                text: words.qText(words.frequencyAt(qControl.value, 0.1, 18))
                font.family: AppFonts.monoFamily
                horizontalAlignment: Text.AlignRight
                Layout.preferredWidth: Kirigami.Units.gridUnit * 4
            }
        }
    }
}
