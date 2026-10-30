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

// What a Hearing Compensation block shows on the canvas: its gain
// against frequency, small — the lift for soft sound faint, the lift for
// average sound strong. The ear is the tile's badge. The 64-band
// block's face draws the curves its points make, a band at a time.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""
    /// Which of the two blocks it is.
    property string typeId: "hearing.compensation16"

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(face.blockId) : ({})
    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(face.blockId).enabled ?? true)
    readonly property color tint: face.blockEnabled
        ? AppColors.categoryTint(BlockPresentation.category(face.typeId))
        : AppColors.muted

    /// The gains the face spans, top to bottom.
    readonly property real topDb: 40
    readonly property real bottomDb: -10

    /// The most lift any band gives soft sound, for the tooltip.
    readonly property real mostLift: bands.all.reduce((most, band) => Math.max(most, band.soft), 0)
    readonly property string detail: face.mostLift <= 0
        ? KI18n.i18nc("@info:tooltip a Hearing Compensation block that lifts nothing", "Flat: nothing lifted")
        : KI18n.i18nc("@info:tooltip the most a Hearing Compensation block lifts soft sound, '+24.0 dB'",
                      "Soft sound lifted by up to %1",
                      KI18n.i18nc("@item a gain in decibels", "+%1 dB", face.mostLift.toFixed(1)))

    readonly property alias bands: bands
    readonly property alias softLine: softPolyline
    readonly property alias averageLine: averagePolyline

    implicitWidth: Kirigami.Units.gridUnit * 12
    implicitHeight: Kirigami.Units.gridUnit * 6

    HearingBands {
        id: bands

        params: face.params
        typeId: face.typeId
    }

    function xOf(band: int): real {
        return plotArea.width * band / Math.max(1, bands.count - 1);
    }

    function yOf(db: real): real {
        const clamped = Math.max(face.bottomDb, Math.min(face.topDb, db));
        return plotArea.height * (face.topDb - clamped) / (face.topDb - face.bottomDb);
    }

    function line(which: string): var {
        const points = [];
        for (let band = 0; band < bands.count; ++band) {
            points.push(Qt.point(face.xOf(band), face.yOf(bands.all[band][which])));
        }
        return points;
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

            // 0 dB, for the lift to be read against.
            ShapePath {
                strokeColor: AppColors.border
                strokeWidth: 1
                fillColor: "transparent"
                startX: 0
                startY: face.yOf(0)

                PathLine {
                    x: plotArea.width
                    y: face.yOf(0)
                }
            }

            ShapePath {
                strokeColor: Qt.rgba(face.tint.r, face.tint.g, face.tint.b, 0.5)
                strokeWidth: 1
                fillColor: "transparent"
                joinStyle: ShapePath.RoundJoin

                PathPolyline {
                    id: softPolyline

                    path: plotArea.width > 0 ? face.line("soft") : []
                }
            }

            ShapePath {
                strokeColor: face.tint
                strokeWidth: 2
                fillColor: "transparent"
                joinStyle: ShapePath.RoundJoin
                capStyle: ShapePath.RoundCap

                PathPolyline {
                    id: averagePolyline

                    path: plotArea.width > 0 ? face.line("average") : []
                }
            }
        }
    }
}
