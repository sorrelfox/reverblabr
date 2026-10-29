// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import org.kde.kirigami as Kirigami
import org.kde.ki18n

// What a Parametric EQ shows on the canvas: its response, small.
//
// **The curve, not a count of bands.** A tile is glanced at, and the shape
// of an equaliser is what it is doing: a lift in the low end, a notch, a
// roll-off. The line is the panel's line, from the same EqualizerPlot, so
// the tile and the editor cannot disagree; nothing on it is dragged — ten
// bands need the panel.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(face.blockId) : ({})
    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(face.blockId).enabled ?? true)
    /// The advanced blocks' colour, or grey for a block that is off.
    readonly property color tint: face.blockEnabled
        ? AppColors.categoryTint(BlockPresentation.category("advanced.parametriceq"))
        : AppColors.muted

    /// Everything there is to say, for the tooltip.
    readonly property string detail: {
        const lines = [];
        for (const band of bands.active) {
            const type = bands.value(band, "Type");
            const where = words.frequencyText(bands.value(band, "Frequency"));
            lines.push(words.hasGain(type)
                ? KI18n.i18nc("@info:tooltip one band of an equaliser, 'Bell 250 Hz +9.0 dB'", "%1 %2 %3",
                              words.bandTypeName(type), where, words.gainText(bands.value(band, "Gain")))
                : KI18n.i18nc("@info:tooltip one band of an equaliser with no gain, 'Notch 60 Hz'", "%1 %2",
                              words.bandTypeName(type), where));
        }
        return lines.length === 0 ? KI18n.i18nc("@info:tooltip an equaliser with no band switched on", "No bands")
                                  : lines.join(KI18n.i18nc("@info:tooltip between two bands of an equaliser", ", "));
    }

    readonly property alias plot: plot
    readonly property alias bands: bands

    implicitWidth: Kirigami.Units.gridUnit * 12
    implicitHeight: Kirigami.Units.gridUnit * 6

    FilterWords {
        id: words
    }

    ParametricEqBands {
        id: bands

        params: face.params
        plot: plot
    }

    EqualizerPlot {
        id: plot

        params: face.params
        plotWidth: plotArea.width
        plotHeight: plotArea.height
        rangeDb: 18
    }

    Item {
        id: plotArea

        anchors.fill: parent
        anchors.leftMargin: Kirigami.Units.smallSpacing * 3
        anchors.rightMargin: Kirigami.Units.smallSpacing * 3
        anchors.topMargin: Kirigami.Units.smallSpacing * 2
        anchors.bottomMargin: Kirigami.Units.smallSpacing * 2
        clip: true

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            // 0 dB, for the curve to be read against.
            ShapePath {
                strokeColor: AppColors.border
                strokeWidth: 1
                fillColor: "transparent"
                startX: 0
                startY: plotArea.height / 2

                PathLine {
                    x: plotArea.width
                    y: plotArea.height / 2
                }
            }

            ShapePath {
                strokeColor: face.tint
                strokeWidth: 2
                fillColor: "transparent"
                joinStyle: ShapePath.RoundJoin
                capStyle: ShapePath.RoundCap

                PathPolyline {
                    path: plot.curve
                }
            }
        }
    }
}
