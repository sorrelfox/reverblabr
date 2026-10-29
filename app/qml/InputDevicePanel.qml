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

// Everything the Input Device block can be asked: which microphone or
// interface it records from, and which of that device's channels the two
// sides of the session come from.
//
// **The mirror image of the output panel, with no volume.** What an input
// is recorded at is the device's own business — the system mixer sets it,
// and a second gain here would be a second place to go wrong — so the block
// has none and neither has this.
ColumnLayout {
    id: panel

    /// The block being shown. The settings panel fills it in.
    property string blockId: ""

    readonly property alias devicePicker: devicePicker
    readonly property alias channelRows: channelRows
    readonly property alias advancedSection: advanced
    readonly property alias deviceState: reading
    /// Whether the device it is pointed at is there at the moment.
    readonly property bool unavailable: !reading.present

    spacing: AppMetrics.panelRowGap

    DeviceState {
        id: reading

        blockId: panel.blockId
        output: false
    }

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        visible: panel.unavailable
        type: Kirigami.MessageType.Warning
        text: KI18n.i18nc("@info the chosen input is not plugged in",
                          "This input is not there at the moment. It comes back on its own when the device does.")
    }

    DevicePicker {
        id: devicePicker

        Layout.fillWidth: true
        blockId: panel.blockId
        output: false
        chosen: reading.device
    }

    // --- advanced: where the two sides come from ---
    //
    // Folded away under a heading, as the design draws it: the
    // natural mapping is right for almost everybody.
    CollapsibleSection {
        id: advanced

        title: KI18n.i18nc("@title:group the less usual settings", "Advanced")

        DeviceChannelRows {
            id: channelRows

            Layout.fillWidth: true
            blockId: panel.blockId
            channels: reading.channels
        }
    }
}
