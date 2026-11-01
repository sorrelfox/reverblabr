// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// The floating label's stack of rows: a dot, a name and a word for each Speech
// Now that has it on, and a number, a word and a track for each Voice Gauge. It
// has no buttons, and is turned off on the block's card or in the tray.
//
// **Hidden from a screen reader**, as it says nothing the Say Who Is Speaking
// and Say Voice Level shortcuts do not, and a window over everything can take
// no focus.
Rectangle {
    id: label

    readonly property var placement: FloatingSurface.placementRevision >= 0 ? FloatingSurface.placement("label") : ({})
    readonly property bool large: label.placement.size === "large"
    readonly property int rowWidth: label.large ? 264 : 168
    readonly property int rowHeight: label.large ? 44 : 28
    /// A gauge's row is wider and taller, since its track needs room.
    readonly property int gaugeWidth: label.large ? 396 : 264
    readonly property int gaugeHeight: label.large ? 66 : 44
    readonly property var gaugeRows: FloatingSurface.rows.filter(row => row.type === label.gaugeType)
    readonly property string gaugeType: "meter.voicegauge"
    readonly property alias rows: repeater
    readonly property var noRow: ({ type: "", nickname: "", watch: "stopped", enabled: true, state: 0, reading: -120, marks: 0 })

    implicitWidth: label.gaugeRows.length > 0 ? label.gaugeWidth : label.rowWidth
    implicitHeight: Math.max(label.rowHeight, (FloatingSurface.rows.length - label.gaugeRows.length) * label.rowHeight
                             + label.gaugeRows.length * label.gaugeHeight)
    color: AppColors.window
    border.color: AppColors.border
    border.width: 1
    radius: AppMetrics.barRadius
    Accessible.ignored: true

    SpeechNowWords {
        id: words
    }

    Column {
        anchors.fill: parent

        Repeater {
            id: repeater

            // By count, with each row reading its place: a level moves ten
            // times a second, and a list as the model would remake every row.
            model: FloatingSurface.rows.length

            delegate: Item {
                id: row

                required property int index
                /// Empty for the moment a row goes, before it is destroyed.
                readonly property var modelData: FloatingSurface.rows[row.index] ?? label.noRow
                readonly property bool isGauge: row.modelData.type === label.gaugeType
                readonly property string kind: words.kindOf(row.modelData.watch, row.modelData.enabled, row.modelData.state)
                readonly property string name: row.modelData.nickname !== ""
                    ? row.modelData.nickname : BlockPresentation.name("meter.speechnow")
                readonly property string shownWord: words.word(row.kind)
                readonly property alias gauge: gaugeLoader.item

                width: parent.width
                height: row.isGauge ? label.gaugeHeight : label.rowHeight

                RowLayout {
                    anchors.fill: parent
                    visible: !row.isGauge
                    spacing: Kirigami.Units.smallSpacing * 2

                    SpeechNowDot {
                        kind: row.kind
                        size: label.large ? 16 : 10
                        Layout.leftMargin: Kirigami.Units.smallSpacing * 2
                        Layout.alignment: Qt.AlignVCenter
                    }

                    ElidedLabel {
                        text: row.name
                        color: AppColors.muted
                        font: label.large ? AppFonts.body : AppFonts.small
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        Accessible.ignored: true
                    }

                    ElidedLabel {
                        text: row.shownWord
                        color: AppColors.text
                        font: label.large ? AppFonts.heading : AppFonts.strong
                        Layout.rightMargin: Kirigami.Units.smallSpacing * 2
                        Accessible.ignored: true
                    }
                }

                Loader {
                    id: gaugeLoader

                    anchors.fill: parent
                    active: row.isGauge
                    sourceComponent: VoiceGaugeRow {
                        modelData: row.modelData
                        large: label.large
                    }
                }
            }
        }
    }
}
