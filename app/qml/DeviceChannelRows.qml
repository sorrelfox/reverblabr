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

// Which of a device's channels the left and the right side of the session
// use. The advanced half of both device panels, which are mirror images of
// each other here.
//
// **The channels are the device's own, by the names its ports carry.** A
// list of "1, 2, 3…" is a list somebody has to count along; `FL`, `FR` and
// `AUX3` are what the device calls them and what every other tool on the
// machine shows.
//
// **"Default" is not a channel.** It is the block's channel policy left
// alone — a mono device on both sides, a stereo pair straight through — and
// it is the right answer almost every time, which is why it comes first.
ColumnLayout {
    id: rows

    /// The block these channels belong to.
    required property string blockId
    /// The chosen device's channels. Empty for a device that is not there,
    /// which leaves the natural mapping as the only thing to pick — which
    /// is honest: there are no channels to offer.
    required property var channels

    readonly property alias leftCombo: leftCombo
    readonly property alias rightCombo: rightCombo
    readonly property alias leftLabel: leftLabel
    readonly property alias rightLabel: rightLabel
    /// How wide the labels' column is, to line up with the device's.
    property real labelWidth: AppMetrics.panelLabelWidth

    /// A channel picker left alone. The blocks' own `kNaturalChannel`,
    /// written here too because the interface may not reach into the block
    /// layer for it.
    readonly property string natural: "default"

    /// What the pickers offer: the natural mapping, then the device's own
    /// channels. **The value is a stable id and the label is a word**, so
    /// "Default" is translated and `FL` is not — `FL` is what the port is
    /// called, in every language.
    readonly property var entries: [{
        value: rows.natural,
        label: KI18n.i18nc("@item:inlistbox the block's natural channel mapping", "Default")
    }].concat((rows.channels ?? []).map(channel => ({
        value: channel,
        label: channel
    })))

    spacing: AppMetrics.panelRowGap

    /// Which channel a side is set to, falling back to the natural mapping
    /// the session does not mention.
    function chosen(paramId: string): string {
        const value = SessionModel.revision >= 0 ? SessionModel.param(rows.blockId, paramId) : undefined;
        return value === undefined ? rows.natural : String(value);
    }

    /// Where that sits in the picker. A channel the device no longer has
    /// falls back to the natural mapping rather than to nothing.
    function indexOf(value: string): int {
        const found = rows.entries.findIndex(entry => entry.value === value);
        return found >= 0 ? found : 0;
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: AppMetrics.labelSpacing

        QQC2.Label {
            id: leftLabel

            text: KI18n.i18nc("@label:listbox the left side of the session", "Left channel")
            color: AppColors.muted
            elide: Text.ElideRight
            // A label column is fixed; a longer word goes onto a second line
            // rather than out of sight.
            wrapMode: Text.Wrap
            maximumLineCount: 2
            horizontalAlignment: Text.AlignRight
            Layout.preferredWidth: rows.labelWidth
        }

        QQC2.ComboBox {
            id: leftCombo

            Layout.fillWidth: true
            model: rows.entries
            textRole: "label"
            valueRole: "value"
            Accessible.name: KI18n.i18nc("@label:listbox", "Left channel")
            onActivated: SessionModel.setParam(rows.blockId, "leftChannel", leftCombo.currentValue)

            SessionBound {
                control: leftCombo
                property: "currentIndex"
                value: rows.indexOf(rows.chosen("leftChannel"))
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: AppMetrics.labelSpacing

        QQC2.Label {
            id: rightLabel

            text: KI18n.i18nc("@label:listbox the right side of the session", "Right channel")
            color: AppColors.muted
            elide: Text.ElideRight
            // A label column is fixed; a longer word goes onto a second line
            // rather than out of sight.
            wrapMode: Text.Wrap
            maximumLineCount: 2
            horizontalAlignment: Text.AlignRight
            Layout.preferredWidth: rows.labelWidth
        }

        QQC2.ComboBox {
            id: rightCombo

            Layout.fillWidth: true
            model: rows.entries
            textRole: "label"
            valueRole: "value"
            Accessible.name: KI18n.i18nc("@label:listbox", "Right channel")
            onActivated: SessionModel.setParam(rows.blockId, "rightChannel", rightCombo.currentValue)

            SessionBound {
                control: rightCombo
                property: "currentIndex"
                value: rows.indexOf(rows.chosen("rightChannel"))
            }
        }
    }
}
