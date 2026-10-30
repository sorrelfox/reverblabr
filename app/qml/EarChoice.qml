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

// **Which ear a hearing block works on**, Left or Right: a label and one
// segmented row, at the top of every hearing panel for one ear, where
// somebody setting up a second block for the other ear looks first.
//
// A choice is one edit, and so one step to undo. The block fades across to
// the other side by itself while the session runs (dsp::EarRouter), so
// nothing else has to happen here. The tile's L or R follows the same
// setting.
RowLayout {
    id: row

    /// The block whose ear this is.
    required property string blockId
    /// Its type, for the default while the session names no ear.
    required property string typeId

    /// "left" or "right": the session's setting, or the block's default.
    readonly property string ear: {
        const set = SessionModel.paramRevision >= 0 ? SessionModel.param(row.blockId, "ear") : undefined;
        if (set !== undefined && set !== null && String(set) !== "") {
            return String(set);
        }
        return BlockCatalogue.ready ? String(BlockCatalogue.describeParam(row.typeId, "ear").default ?? "left") : "left";
    }

    /// The two choices, for a test.
    readonly property alias choices: choiceRepeater

    /// Sets the ear, unless it is already that one: clicking the chosen
    /// side again is not an edit to undo.
    function choose(ear: string) {
        if (ear !== row.ear) {
            SessionModel.setParam(row.blockId, "ear", ear);
        }
    }

    /// What choosing \a ear does, said on hover.
    function tooltip(ear: string): string {
        if (ear === "right") {
            return KI18n.i18nc("@info:tooltip", "Works on the right ear; the left passes through");
        }
        return KI18n.i18nc("@info:tooltip", "Works on the left ear; the right passes through");
    }

    spacing: AppMetrics.labelSpacing

    Accessible.role: Accessible.Grouping
    Accessible.name: KI18n.i18nc("@label which ear a hearing block works on", "Ear")

    QQC2.Label {
        text: KI18n.i18nc("@label which ear a hearing block works on", "Ear:")
        color: AppColors.muted
    }

    SegmentedFrame {
        QQC2.ButtonGroup {
            id: earGroup
        }

        Repeater {
            id: choiceRepeater

            model: ["left", "right"]

            delegate: SegmentedChoice {
                id: earButton

                required property string modelData

                text: ChoiceWords.earName(earButton.modelData)
                Accessible.name: text
                QQC2.ButtonGroup.group: earGroup
                QQC2.ToolTip.text: row.tooltip(earButton.modelData)
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                onClicked: row.choose(earButton.modelData)

                SessionBound {
                    control: earButton
                    property: "checked"
                    value: row.ear === earButton.modelData
                }
            }
        }
    }

    Item {
        Layout.fillWidth: true
    }
}
