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
// Now that has it on. It has no buttons, and is turned off on the block's card
// or in the tray.
//
// **Hidden from a screen reader**, as it says nothing the Say Who Is Speaking
// shortcut does not, and a window over everything can take no focus.
Rectangle {
    id: label

    readonly property var placement: FloatingSurface.placementRevision >= 0 ? FloatingSurface.placement("label") : ({})
    readonly property bool large: label.placement.size === "large"
    readonly property int rowWidth: label.large ? 264 : 168
    readonly property int rowHeight: label.large ? 44 : 28
    readonly property alias rows: repeater

    implicitWidth: label.rowWidth
    implicitHeight: Math.max(1, FloatingSurface.rows.length) * label.rowHeight
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

            model: FloatingSurface.rows

            delegate: RowLayout {
                id: row

                required property var modelData
                readonly property string kind: words.kindOf(row.modelData.watch, row.modelData.enabled, row.modelData.state)
                readonly property string name: row.modelData.nickname !== ""
                    ? row.modelData.nickname : BlockPresentation.name("meter.speechnow")
                readonly property string shownWord: words.word(row.kind)

                width: parent.width
                height: label.rowHeight
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
        }
    }
}
