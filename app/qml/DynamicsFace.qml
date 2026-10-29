// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Shapes
import org.kde.kirigami as Kirigami
import org.kde.ki18n

// What a Dynamics block shows on the canvas: the curve it is set to, where
// the sound is on it, and how much is being held back.
//
// **The face is the curve, not an icon.** The block declares a curve face,
// and every tile already draws its category icon; a second icon here would
// say "effect", which the tint says already. The line says what this block
// is set to do and the figure says what it is doing about it, which is what
// somebody glancing across a canvas wants to know.
//
// **A tile is glanced at, so it shows the loud end.** The panel's plot runs
// the whole way down to −120 dB because a handle has to be reachable there;
// a tile has room for the part where a person's sound actually is.
Item {
    id: face

    /// The block whose face this is. **Not a required property**: a face is
    /// made from a Component in the register, and a Loader cannot hand a
    /// Component its properties, so it is set once the face exists.
    property string blockId: ""

    readonly property string typeId: SessionModel.revision >= 0 ? (SessionModel.block(face.blockId).type ?? "") : ""
    /// Re-read when the session changes, so a value set anywhere — the
    /// panel, undo, a second window — is the curve the tile shows.
    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(face.blockId) : ({})

    readonly property var levels: Meters.frame >= 0 ? Meters.levels(face.blockId) : ({})
    readonly property bool active: Meters.live && (face.levels.active ?? false)

    readonly property real inputLevelDb: face.published(curve.inputLevelIndex, curve.lowestDb)
    readonly property real reductionDb: face.published(curve.gainReductionIndex, 0)

    readonly property color tint: AppColors.categoryTint(BlockPresentation.category(face.typeId))

    /// The parts themselves, so that a test reads what a person reads.
    readonly property alias curve: curve
    readonly property alias readout: reductionLabel

    implicitWidth: Kirigami.Units.gridUnit * 7
    implicitHeight: Kirigami.Units.gridUnit * 4

    function published(index: int, fallback: real): real {
        const values = face.levels.values ?? [];
        return index >= 0 && index < values.length ? values[index] : fallback;
    }

    // **Reading `tessellations` is what makes a placing follow the curve.**
    // A binding that only calls xOfDb() or pointAt() has nothing to depend
    // on, so it would be worked out once and then stay where it was through
    // every resize and every drag; the count changes whenever the mapping
    // does, and is the dependency that brings the binding back.
    DynamicsPlot {
        id: curve

        params: face.params
        plotWidth: plotArea.width
        plotHeight: plotArea.height
        // The part of the range a tile can say something about: from a quiet
        // room to full scale.
        lowestDb: -60
        highestDb: 0
    }

    Item {
        id: plotArea

        anchors.fill: parent
        // The curve is drawn as it is and the tile clips it, so a shape bent
        // off the bottom is not quietly flattened into something else.
        clip: true

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            // What the block would do if it did nothing.
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
                fillColor: Qt.rgba(face.tint.r, face.tint.g, face.tint.b, 0.16)

                PathPolyline {
                    path: curve.filled
                }
            }

            ShapePath {
                strokeColor: face.tint
                strokeWidth: Math.max(1, Kirigami.Units.smallSpacing / 2)
                fillColor: "transparent"
                joinStyle: ShapePath.RoundJoin

                PathPolyline {
                    path: curve.curve
                }
            }
        }

        // Where the sound is on the curve. **Its own item**: it moves every
        // frame and the curve underneath it does not (ADR-0009).
        Rectangle {
            id: marker

            readonly property point at: curve.tessellations >= 0 ? curve.pointAt(face.inputLevelDb) : Qt.point(0, 0)

            visible: face.active
            x: marker.at.x - width / 2
            y: marker.at.y - height / 2
            width: Kirigami.Units.gridUnit * 0.4
            height: width
            radius: width / 2
            color: AppColors.liveMark
        }
    }

    QQC2.Label {
        id: reductionLabel

        anchors.right: parent.right
        anchors.bottom: parent.bottom
        font: AppFonts.small
        color: face.active ? face.tint : AppColors.muted
        text: face.active
            ? KI18n.i18nc("@info how much the block is holding the sound back, in decibels", "%1 dB",
                          Number(Math.min(0, face.reductionDb)).toLocaleString(Qt.locale(), 'f', 1))
            : KI18n.i18nc("@info nothing is running, so there is nothing to report", "—")
        Accessible.name: KI18n.i18nc("@label how much the block is holding the sound back", "Gain reduction")
        Accessible.description: text
    }
}
