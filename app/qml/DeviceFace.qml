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
import org.kde.ki18n

// The face of a device block: an icon, and the device.
//
// **A component, not a drawing on the canvas.** There is no canvas yet, and
// a face that can only be looked at once one exists is a face nobody can
// test; this is a piece of the window in its own right, and the canvas will
// put it on a tile.
//
// **One line, and it is the device**. A tile is 132 pixels wide,
// and two lines on it — "Default output" over the device it lands on —
// were two strings cut off halfway. Where the sound goes is the question
// somebody glancing at the tile actually has, so that is the line; that it
// got there by following the default is in the tooltip and the panel,
// where there is room to say it. A device that has gone says so instead,
// since that is the one thing about it worth reading first.
//
// The icons are stock theme names for now. Reverblabr's own block icons are
// their own task.
Item {
    id: face

    /// The block whose face this is. **Not a required property**: a face
    /// is made from a Component in the register, and a Loader cannot hand a
    /// Component its properties, so it is set once the face exists.
    property string blockId: ""
    /// Whether it plays or records. This one stays required, because the
    /// register binds it: which of the two device blocks this is follows
    /// from the type id, so it is settled there rather than by the canvas
    /// asking.
    required property bool output

    /// The reading behind it, for a test and for whatever puts the tile
    /// together.
    readonly property alias deviceState: reading
    readonly property bool unavailable: !reading.present

    readonly property string iconName: face.output ? "audio-volume-high" : "audio-input-microphone"

    /// The first line: what the block is pointed at.
    readonly property string title: reading.unset
        ? (face.output
            ? KI18n.i18nc("@label no output device has been chosen yet", "Choose an output")
            : KI18n.i18nc("@label no input device has been chosen yet", "Choose an input"))
        : (reading.followsDefault
            ? (face.output
                ? KI18n.i18nc("@label follows whatever the system output is", "Default output")
                : KI18n.i18nc("@label follows whatever the system input is", "Default input"))
            : (reading.deviceName !== "" ? reading.deviceName : reading.device))

    /// The second: where the default lands at the moment, or that the
    /// device is not there. **The words are the interface's**; the engine
    /// publishes a one or a zero and has no idea what language anybody
    /// speaks.
    readonly property string subtitle: face.unavailable
        ? KI18n.i18nc("@info the chosen device is not there at the moment", "Unavailable")
        : (reading.followsDefault && reading.deviceName !== "" ? reading.deviceName : "")

    /// The one line the tile shows.
    readonly property string line: face.unavailable
        ? face.subtitle
        : (reading.followsDefault && reading.deviceName !== "" ? reading.deviceName : face.title)

    /// Everything there is to say, for the tooltip and a screen reader.
    readonly property string detail: face.subtitle === ""
        ? face.title
        : KI18n.i18nc("@info:whatsthis a device and what there is to say about it", "%1 — %2",
                      face.title, face.subtitle)

    readonly property alias lineLabel: lineText

    implicitWidth: Kirigami.Units.gridUnit * 10
    implicitHeight: Kirigami.Units.gridUnit * 3

    Accessible.role: Accessible.StaticText
    Accessible.name: face.detail

    DeviceState {
        id: reading

        blockId: face.blockId
        output: face.output
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: AppMetrics.faceMargin
        spacing: AppMetrics.faceSpacing

        Item {
            Layout.fillHeight: true
        }

        Kirigami.Icon {
            source: face.iconName
            // Greyed out when there is nothing on the other end, so that a
            // glance at the canvas finds the block that has stopped working
            // without anybody reading a word.
            opacity: face.unavailable ? 0.5 : 1
            implicitWidth: AppMetrics.faceIconSize
            implicitHeight: AppMetrics.faceIconSize
            Layout.alignment: Qt.AlignHCenter
            Accessible.ignored: true
        }

        ElidedLabel {
            id: lineText

            text: face.line
            color: face.unavailable ? AppColors.warningText : AppColors.muted
            font: AppFonts.faceLine
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
