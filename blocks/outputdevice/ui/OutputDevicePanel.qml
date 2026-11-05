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
    readonly property alias limitsSection: limits
    readonly property alias limitReasonLine: limitReason
    readonly property alias limit: limitRow
    readonly property alias setLowerLine: setLower
    readonly property alias limitActingLine: limitActing
    /// Whether the device it is pointed at is there at the moment.
    readonly property bool unavailable: !reading.present

    /// The volume as the session holds it: an amplitude, 0…1.
    readonly property real volume: SessionModel.paramRevision >= 0
        ? Number(SessionModel.param(panel.blockId, "volume") ?? 1)
        : 1

    /// Whether the session needs a last limit here, why, and at what ceiling:
    /// the same answer the engine works out.
    readonly property var lastLimit: SessionModel.paramRevision >= 0 ? SessionModel.lastLimit(panel.blockId) : ({})
    /// The ceiling in use: what the block says, while it runs.
    readonly property real appliedLimitDb: {
        const published = (reading.levels.values ?? [])[3] ?? 0;
        return published < 0 ? published : (panel.lastLimit.appliedDb ?? 0);
    }
    /// Why the limit is there, in words.
    readonly property string limitReason: {
        switch (panel.lastLimit.cause) {
        case "block":
            return KI18n.i18nc("@info why this block has a limit; the name of a block",
                               "This limit is here because %1 can add gain after the last limit. It adds about 2 ms of delay.",
                               panel.lastLimit.blockName);
        case "twoWires":
            return KI18n.i18nc("@info why this block has a limit",
                               "This limit is here because two wires meet at one input, and their sum can go past the last limit. It adds about 2 ms of delay.");
        case "oneChannel":
            return KI18n.i18nc("@info why this block has a limit",
                               "This limit is here because both sides go to one channel, where together they can pass the last limit. It adds about 2 ms of delay.");
        default:
            return "";
        }
    }

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

    // --- the last limit, when the session needs one ---

    CollapsibleSection {
        id: limits

        Layout.fillWidth: true
        visible: panel.lastLimit.needed === true
        title: KI18n.i18nc("@title:group the digital limits of a block", "Limits")
        note: KI18n.i18nc("@info what the limits are not", "Digital, not at your ear")
        expanded: true

        InfoLine {
            id: limitReason

            Layout.fillWidth: true
            text: panel.limitReason
        }

        LimitRow {
            id: limitRow

            Layout.fillWidth: true
            blockId: panel.blockId
            paramId: "limitDb"
            typeId: "output.device"
            label: KI18n.i18nc("@label the highest level this block sends to the output", "Last limit")
            unit: KI18n.i18nc("@label unit of a level, 0 is full scale", "dBFS")
            valueText: value => KI18n.i18nc("@item a level, 0 is full scale", "%1 dBFS", Math.round(value))
            stopWords: KI18n.i18nc("@info said at the end of a slider", "Last limit stops here. To raise it, use Raise.")
            question: KI18n.i18nc("@info the question over a number", "Last limit is %1 dBFS. Raise it to:",
                                  Math.round(limitRow.value))
        }

        QQC2.Label {
            id: setLower

            Layout.fillWidth: true
            visible: panel.appliedLimitDb < limitRow.value
            wrapMode: Text.WordWrap
            font: AppFonts.small
            color: AppColors.muted
            text: KI18n.i18nc("@info a lower limit further up the chain is the one that holds",
                              "Set lower by Output ceiling: %1 dBFS", Math.round(panel.appliedLimitDb))
        }

        LimitActing {
            id: limitActing

            blockId: panel.blockId
            slot: 1
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
