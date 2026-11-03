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

// One choice of a block's settings on a card: its name, and a row of segments
// that writes the session and follows it.
ColumnLayout {
    id: row

    /// The block, and which of its settings this is.
    required property string blockId
    required property string paramId
    required property string title
    /// The choices' ids, and the one chosen.
    required property var choices
    required property string current
    /// A choice's words, and what a screen reader or a tooltip says of it.
    required property var nameOf
    property var spokenOf: choice => row.nameOf(choice)
    property var tipOf: choice => ""

    /// The segments, so that a test drives what a person drives.
    readonly property alias buttons: repeater

    Layout.fillWidth: true
    spacing: AppMetrics.panelRowGap

    QQC2.Label {
        text: row.title
        color: AppColors.muted
    }

    SegmentedFrame {
        shrinkable: true
        Layout.fillWidth: true
        Accessible.name: row.title

        QQC2.ButtonGroup {
            id: group
        }

        Repeater {
            id: repeater

            model: row.choices

            delegate: SegmentedChoice {
                id: button

                required property string modelData

                text: row.nameOf(button.modelData)
                Accessible.name: row.spokenOf(button.modelData)
                QQC2.ToolTip.text: row.tipOf(button.modelData)
                QQC2.ToolTip.visible: hovered && QQC2.ToolTip.text !== ""
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                QQC2.ButtonGroup.group: group
                onClicked: SessionModel.setParam(row.blockId, row.paramId, button.modelData)

                SessionBound {
                    control: button
                    property: "checked"
                    value: row.current === button.modelData
                }
            }
        }
    }
}
