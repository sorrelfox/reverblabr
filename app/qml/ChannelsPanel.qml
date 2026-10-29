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
import org.kde.ki18n

// The Channels block's settings: which action, and a line saying what
// it does, as the design draws it.
//
// **The generic panel could offer the list**, but only in the engine's
// spelling — "leftToBoth" — and with nothing to say what "Difference" is
// for; the line under the list is most of the panel's worth.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property string action: SessionModel.revision >= 0
        ? (SessionModel.param(panel.blockId, "action") ?? "none")
        : "none"

    /// The controls, so that a test drives what a person drives.
    readonly property alias actionBox: actionControl
    readonly property alias explanationLine: explanation
    readonly property alias words: words

    spacing: AppMetrics.panelRowGap

    ChannelsWords {
        id: words
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: AppMetrics.labelSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label:listbox what the Channels block does to the two sides", "Action")
            color: AppColors.muted
            horizontalAlignment: Text.AlignRight
            Layout.preferredWidth: Math.max(AppMetrics.panelNarrowLabelWidth, implicitWidth)
        }

        QQC2.ComboBox {
            id: actionControl

            Layout.fillWidth: true
            model: words.actions
            displayText: words.actionName(panel.action)
            Accessible.name: KI18n.i18nc("@label:listbox", "Action")

            delegate: QQC2.ItemDelegate {
                required property string modelData
                required property int index

                width: actionControl.width
                text: words.actionName(modelData)
                highlighted: actionControl.highlightedIndex === index
            }

            // An action travels as its stable id, never as its position.
            onActivated: SessionModel.setParam(panel.blockId, "action",
                                               String(words.actions[actionControl.currentIndex]))

            SessionBound {
                control: actionControl
                property: "currentIndex"
                value: Math.max(0, words.actions.indexOf(panel.action))
            }
        }
    }

    InfoLine {
        id: explanation

        text: words.explanation(panel.action)
    }
}
