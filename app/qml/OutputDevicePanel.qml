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

// Everything the Output Device block can be asked: which device it plays
// to, how loud, and — for the few people who need it — which of that
// device's channels the two sides of the session land on.
//
// **The volume is an amplitude and is read as a percentage.** Half the
// signal is −6.02 dB, and neither of those is a number to put beside a
// slider; the session file holds what the audio uses and the panel shows
// what a person asked for, and 50 % writes exactly 0.5.
//
// **Its own panel rather than the generic one** because of the three things
// the generic panel cannot know: that a device reference is a live list, a
// volume is a percentage, and a channel is one of the ones *this* device
// has. A block whose settings are a list of settings needs none of that.
ColumnLayout {
    id: panel

    /// The block being shown. The settings panel fills it in; a panel that
    /// required it could not be loaded by something that does not know what
    /// it is loading.
    property string blockId: ""

    readonly property alias devicePicker: devicePicker
    readonly property alias volumeSlider: volumeSlider
    readonly property alias channelRows: channelRows
    readonly property alias advancedSection: advanced
    readonly property alias volumeLabel: volumeText
    readonly property alias deviceState: reading
    /// Whether the device it is pointed at is there at the moment.
    readonly property bool unavailable: !reading.present

    /// The volume as the session holds it: an amplitude, 0…1.
    readonly property real volume: SessionModel.revision >= 0
        ? Number(SessionModel.param(panel.blockId, "volume") ?? 1)
        : 1

    spacing: AppMetrics.panelRowGap

    /// The two halves of "an amplitude read as a percentage", written down
    /// once so that the slider, the number beside it and a test all agree.
    function percentOf(amplitude: real): real {
        return amplitude * 100;
    }

    function amplitudeOf(percent: real): real {
        return percent / 100;
    }

    DeviceState {
        id: reading

        blockId: panel.blockId
        output: true
    }

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        visible: panel.unavailable
        type: Kirigami.MessageType.Warning
        text: KI18n.i18nc("@info the chosen output is not plugged in",
                          "This output is not there at the moment. It comes back on its own when the device does.")
    }

    DevicePicker {
        id: devicePicker

        Layout.fillWidth: true
        blockId: panel.blockId
        output: true
        chosen: reading.device
    }

    // --- volume, as the design draws it ---
    //
    // The name and the number on one line, the number in the accent, and
    // the slider under them at the panel's full width.
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label:slider how loud the block plays to its device", "Output volume")
            color: AppColors.text
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        QQC2.Label {
            id: volumeText

            text: KI18n.i18nc("@info:status a volume, as a percentage", "%1 %", Math.round(volumeSlider.value))
            color: AppColors.accentText
            font.family: AppFonts.monoFamily
            horizontalAlignment: Text.AlignRight
        }
    }

    QQC2.Slider {
        id: volumeSlider

        Layout.fillWidth: true
        // Close under its name, as one row with it.
        Layout.topMargin: AppMetrics.pullIntoRow
        from: 0
        to: 100
        // **No step**: the style draws a tick for every step, a hundred of
        // them, and the design has a plain track. The number beside it is
        // whole per cent, which is as fine as anybody aims.
        Accessible.name: KI18n.i18nc("@label:slider", "Output volume")

        // Live while it moves — coalesced and sent sixty times a second,
        // which is what a smoothed parameter can take — and the last value
        // always arrives, whatever the timer was doing.
        onMoved: SessionModel.setParam(panel.blockId, "volume", panel.amplitudeOf(value), true)
        onPressedChanged: if (!pressed) {
            SessionModel.commitParam(panel.blockId, "volume");
        }

        SessionBound {
            control: volumeSlider
            property: "value"
            value: panel.percentOf(panel.volume)
            suspended: volumeSlider.pressed
        }
    }

    // --- advanced: where the two sides land on the device ---
    //
    // Folded away under a heading, because the natural mapping is right for
    // almost everybody and a panel that opens with six channels laid out is
    // a panel that looks like a job of work.
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
