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

// What a Multiband Compressor shows on the canvas: a well for each
// band with how much it is holding back hanging from the top, low to high,
// the band's name under it, and where the bands meet under those.
//
// **Gain reduction hangs down**, as a compressor's meter does on a mixing
// desk: a bar that grows from the top reads as "taken off", where one that
// grows from the bottom would read as a level.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property string typeId: "effect.multibandcompressor"
    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(face.blockId) : ({})
    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(face.blockId).enabled ?? true)
    readonly property color tint: face.blockEnabled ? AppColors.categoryTint(BlockPresentation.category(face.typeId))
                                                    : AppColors.muted

    readonly property var levels: Meters.frame >= 0 ? Meters.levels(face.blockId) : ({})
    readonly property bool active: Meters.live && (face.levels.active ?? false)
    /// Each band's gain reduction, low first: what the block publishes as
    /// its three values, or nothing while it is not running.
    readonly property var reductions: [0, 1, 2].map(band => {
        const values = face.levels.values ?? [];
        return face.active && band < values.length ? Math.min(0, values[band]) : 0;
    })

    readonly property real lowSplit: face.value("lowCrossover")
    readonly property real highSplit: face.value("highCrossover")

    /// Everything there is to say, for the tooltip: what each band does.
    readonly property string detail: [0, 1, 2].map(band => words.bandSummary(
        band, face.lowSplit, face.highSplit,
        face.value(words.bandIds[band] + "Threshold"), face.value(words.bandIds[band] + "Ratio"))).join("\n")

    readonly property alias words: words
    readonly property alias bars: barRow
    readonly property alias captionLabel: captionText

    implicitWidth: Kirigami.Units.gridUnit * 8
    implicitHeight: Kirigami.Units.gridUnit * 5

    Accessible.role: Accessible.StaticText
    Accessible.name: KI18n.i18nc("@info accessible name of a multiband compressor's tile", "Multiband Compressor, %1",
                                 captionText.text)
    Accessible.description: face.detail

    MultibandWords {
        id: words
    }

    /// A parameter as it stands, or the block's default.
    function value(paramId: string): real {
        const held = face.params[paramId];
        return held === undefined ? (BlockCatalogue.describeParam(face.typeId, paramId).default ?? 0) : held;
    }

    ColumnLayout {
        anchors {
            fill: parent
            leftMargin: AppMetrics.faceMargin
            rightMargin: AppMetrics.faceMargin
            topMargin: AppMetrics.faceSpacing
            bottomMargin: AppMetrics.faceSpacing
        }
        spacing: AppMetrics.faceSpacing

        Row {
            id: barRow

            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Kirigami.Units.smallSpacing

            Repeater {
                model: 3

                delegate: ColumnLayout {
                    id: column

                    required property int index

                    width: (barRow.width - 2 * barRow.spacing) / 3
                    height: barRow.height
                    spacing: 2

                    // The well the bar hangs in.
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: AppMetrics.barRadius
                        color: AppColors.well

                        Rectangle {
                            objectName: "reductionBar"
                            anchors {
                                top: parent.top
                                left: parent.left
                                right: parent.right
                            }
                            height: Math.round(parent.height * words.reductionFraction(face.reductions[column.index]))
                            radius: AppMetrics.barRadius
                            color: face.tint
                        }
                    }

                    ElidedLabel {
                        text: words.bandName(column.index)
                        font: AppFonts.tileCaption
                        color: AppColors.muted
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        Accessible.ignored: true
                    }
                }
            }
        }

        ElidedLabel {
            id: captionText

            text: words.splitsText(face.lowSplit, face.highSplit)
            font: AppFonts.tileCaption
            color: AppColors.muted
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            Layout.fillWidth: true
            Accessible.ignored: true
        }
    }
}
