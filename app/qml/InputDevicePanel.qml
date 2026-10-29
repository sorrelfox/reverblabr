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
    readonly property alias profileMessage: profileMessage

    /// **A wireless headset recording silence because of its profile**: the
    /// session runs, the headset is there, the meters see nothing, and the
    /// engine says its microphone is switched off. WirePlumber switches a
    /// headset to its headset profile by itself once something records from
    /// it, so this is first given the time that takes.
    readonly property bool silentHeadset: reading.playbackProfile && reading.metered && reading.present
        && !(reading.levels.active ?? false)
    /// How long the silence has to last before it is worth a word.
    property int profileGraceMs: 3000

    spacing: AppMetrics.panelRowGap

    // A silence that ends starts the wait again the next time.
    onSilentHeadsetChanged: {
        if (!panel.silentHeadset) {
            profileMessage.due = false;
        }
    }

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

    // Shown only once the silence has outlasted the switch WirePlumber
    // would have made, and gone the moment the microphone is there.
    Kirigami.InlineMessage {
        id: profileMessage

        property bool due: false

        Layout.fillWidth: true
        visible: panel.silentHeadset && profileMessage.due
        type: Kirigami.MessageType.Warning
        text: KI18n.i18nc("@info %1 is a wireless headset; A2DP and HSP/HFP are the names of its two kinds of profile",
                          "%1 is on its playback profile (A2DP), which has no microphone, so nothing is being recorded. Switch it to its headset profile (HSP/HFP) in the system's sound settings; the sound in the headset is of lower quality while it is.",
                          reading.deviceName)
    }

    Timer {
        interval: panel.profileGraceMs
        running: panel.silentHeadset && !profileMessage.due
        onTriggered: profileMessage.due = true
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
