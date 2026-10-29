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

// The 10-Band EQ's settings: ten upright sliders, a preamp, keeping
// the level, and a way to put everything back.
//
// **As the design draws it**: a dB scale down the left (+12 to −12), and
// for each band its frequency over an upright slider with a line at 0 dB
// and its setting under it in the effects colour. Under the bands, the
// preamp, *Keep level* and **Flatten**, which puts all ten bands and the
// preamp back at 0 dB as one step to undo (SessionModel.setParams).
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(panel.blockId) : ({})
    readonly property real preampDb: panel.params.preamp ?? 0
    readonly property bool keepLevel: panel.params.keepLevel === true
    readonly property bool flat: bands.ids.every(id => Math.abs(panel.params[id] ?? 0) < 0.05) && Math.abs(panel.preampDb) < 0.05
    /// Ten columns want a wider card than a list of settings (SettingsPanel
    /// asks for this).
    readonly property real panelWidth: AppMetrics.followsSystem ? Kirigami.Units.gridUnit * 24 : 400
    /// The effects colour, for the settings under the bands.
    readonly property color tint: AppColors.categoryTint(BlockPresentation.category("effect.equalizer10"))

    /// The controls, so that a test drives what a person drives.
    readonly property alias bandSliders: bandRepeater
    readonly property alias scaleLabels: scaleRepeater
    readonly property alias preampSlider: preampControl
    readonly property alias keepLevelBox: keepLevelControl
    readonly property alias flattenButton: flattenControl
    readonly property alias bands: bands
    readonly property alias words: words
    /// The lowest band's slider, which the scale is laid against. Set by
    /// the slider itself, so that it is typed.
    property QQC2.Slider firstBandSlider: null

    spacing: Kirigami.Units.largeSpacing

    EqualizerBands {
        id: bands
    }

    FilterWords {
        id: words
    }

    /// Every band and the preamp back at 0 dB, as one edit.
    function flatten() {
        const changes = bands.ids.map(id => [id, 0]);
        changes.push(["preamp", 0]);
        SessionModel.setParams(panel.blockId, changes);
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: Kirigami.Units.gridUnit * 12
        spacing: Kirigami.Units.smallSpacing

        // The scale, level with the sliders' travel: +12 at the top of it,
        // −12 at the bottom.
        Item {
            id: scale

            Layout.fillHeight: true
            Layout.preferredWidth: Kirigami.Units.gridUnit * 1.8

            Repeater {
                id: scaleRepeater

                model: [12, 6, 0, -6, -12]

                delegate: QQC2.Label {
                    required property var modelData

                    readonly property QQC2.Slider travel: panel.firstBandSlider
                    // **Where the handle's centre runs, from reactive
                    // parts**: a mapToItem() is worked out once and not
                    // again when the layout settles, and put the scale
                    // above the handles. The band columns stand at the top
                    // of an area level with this one, so the slider's own
                    // y is the distance down; the handle's centre travels
                    // from half a handle below its top to half above its
                    // bottom.
                    readonly property real handleSize: travel === null || travel.handle === null ? 0 : travel.handle.height
                    readonly property real travelTop: travel === null ? 0 : travel.y + travel.topPadding + handleSize / 2
                    readonly property real travelLength: travel === null ? scale.height : travel.availableHeight - handleSize

                    anchors.right: parent.right
                    y: travelTop + travelLength * (bands.rangeDb - modelData) / (2 * bands.rangeDb) - height / 2
                    text: modelData > 0 ? "+" + modelData : String(modelData)
                    // The numbers down a meter's side are the same kind of
                    // thing: a scale to read a handle against.
                    font: AppFonts.meterScale
                    color: AppColors.muted
                    Accessible.ignored: true
                }
            }
        }

        Item {
            id: bandArea

            Layout.fillWidth: true
            Layout.fillHeight: true

            Repeater {
                id: bandRepeater

                model: bands.ids.length

                delegate: ColumnLayout {
                    id: column

                    required property int index

                    readonly property string paramId: bands.ids[column.index]
                    readonly property real gainDb: panel.params[column.paramId] ?? 0
                    readonly property alias slider: bandSlider
                    readonly property alias readout: readoutText

                    // **An equal share of the row, placed rather than laid
                    // out**, whatever a style's upright slider asks for: the
                    // desktop style's asks for more than a tenth of the card,
                    // and ten of them in a row layout pushed it wider — and a
                    // row layout shared what was left by what each column's
                    // numbers asked, so the columns came out uneven.
                    x: column.index * bandArea.width / bands.ids.length
                    width: bandArea.width / bands.ids.length
                    height: bandArea.height
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Label {
                        Layout.alignment: Qt.AlignHCenter
                        // **Never wider than its band**: in a narrow
                        // window's card a column is narrower than "16k", and
                        // the last one hung past the card's edge. The words
                        // get smaller instead.
                        Layout.maximumWidth: column.width
                        fontSizeMode: Text.HorizontalFit
                        minimumPixelSize: 6
                        horizontalAlignment: Text.AlignHCenter
                        text: words.bandLabel(bands.frequencies[column.index])
                        font: AppFonts.meterScale
                        color: AppColors.muted
                        Accessible.ignored: true
                    }

                    QQC2.Slider {
                        id: bandSlider

                        Layout.alignment: Qt.AlignHCenter
                        Layout.fillHeight: true
                        Layout.preferredWidth: Math.min(implicitWidth, column.width)
                        orientation: Qt.Vertical
                        Component.onCompleted: if (column.index === 0) {
                            panel.firstBandSlider = bandSlider;
                        }
                        from: -bands.rangeDb
                        to: bands.rangeDb
                        stepSize: 0.5
                        snapMode: QQC2.Slider.SnapAlways
                        Accessible.name: KI18n.i18nc("@label:slider an equaliser band, '1 kHz'", "%1 band",
                                                     words.bandText(bands.frequencies[column.index]))

                        onMoved: SessionModel.setParam(panel.blockId, column.paramId, value, true)
                        onPressedChanged: if (!pressed) {
                            SessionModel.commitParam(panel.blockId, column.paramId);
                        }

                        // The line at 0 dB, across the groove.
                        Rectangle {
                            x: (bandSlider.width - width) / 2
                            y: bandSlider.topPadding + bandSlider.availableHeight / 2
                            z: -1
                            width: Kirigami.Units.gridUnit
                            height: 1
                            color: AppColors.border
                        }

                        SessionBound {
                            control: bandSlider
                            property: "value"
                            value: column.gainDb
                            suspended: bandSlider.pressed
                        }
                    }

                    QQC2.Label {
                        id: readoutText

                        Layout.alignment: Qt.AlignHCenter
                        // The same for the number under it.
                        Layout.maximumWidth: column.width
                        fontSizeMode: Text.HorizontalFit
                        minimumPixelSize: 6
                        horizontalAlignment: Text.AlignHCenter
                        // Signed, and 0 for untouched: "+2.5", "-6", "0".
                        text: Math.abs(bandSlider.value) < 0.05 ? "0"
                            : (bandSlider.value > 0 ? "+" : "") + Number(bandSlider.value).toLocaleString(Qt.locale(), 'f',
                                                                                                         bandSlider.value % 1 === 0 ? 0 : 1)
                        font: AppFonts.mono
                        color: panel.tint
                        Accessible.ignored: true
                    }
                }
            }
        }
    }

    Kirigami.FormLayout {
        Layout.fillWidth: true

        RowLayout {
            Kirigami.FormData.label: KI18n.i18nc("@label:slider the gain before an equaliser's bands", "Preamp:")
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Slider {
                id: preampControl

                Layout.fillWidth: true
                from: -bands.rangeDb
                to: bands.rangeDb
                stepSize: 0.5
                snapMode: QQC2.Slider.SnapAlways
                Accessible.name: KI18n.i18nc("@label:slider the gain before an equaliser's bands", "Preamp")

                onMoved: SessionModel.setParam(panel.blockId, "preamp", value, true)
                onPressedChanged: if (!pressed) {
                    SessionModel.commitParam(panel.blockId, "preamp");
                }

                SessionBound {
                    control: preampControl
                    property: "value"
                    value: panel.preampDb
                    suspended: preampControl.pressed
                }
            }

            QQC2.Label {
                text: words.gainText(preampControl.value)
                font.family: AppFonts.monoFamily
                horizontalAlignment: Text.AlignRight
                Layout.preferredWidth: Kirigami.Units.gridUnit * 4
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            QQC2.CheckBox {
                id: keepLevelControl

                text: KI18n.i18nc("@option:check an equaliser lowers its output by its highest lift", "Keep level")
                checked: panel.keepLevel
                QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip",
                                               "Lowers the output by the highest point of the curve, so lifting bands changes the tone without making the sound any louder")
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                onToggled: SessionModel.setParam(panel.blockId, "keepLevel", checked)
            }

            Item {
                Layout.fillWidth: true
            }

            QQC2.Button {
                id: flattenControl

                text: KI18n.i18nc("@action:button put every band of an equaliser back at 0 dB", "Flatten")
                enabled: !panel.flat
                onClicked: panel.flatten()
            }
        }
    }
}
