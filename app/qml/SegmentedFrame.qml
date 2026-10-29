// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// A row of choices of which one is chosen, framed as one control:
// the Volume block's overdrive, the Wave's sides, a virtual device's choices.
//
// **In the Reverblabr appearance it is the prototype's segmented row**: the
// choices sit two pixels apart inside one frame in the field's colour, and
// each is a flat button (`segmented` below), so the chosen one is a block of
// the accent in a well. **Following the system it is what it was**: the
// desktop style's own buttons side by side, and no frame of ours round
// them.
//
// **An item, not a control**, so that it adds nothing of its own in the
// desktop's look: the desktop style gives a control padding of its own,
// which pushed the buttons in.
Item {
    id: frame

    /// The choices: SegmentedChoice buttons, often from a Repeater.
    default property alias content: row.data
    /// The row the choices are in, for a test that walks them.
    readonly property alias row: row
    /// The frame drawn round them, for a test.
    readonly property alias frameRectangle: frameRectangle
    /// Whether the choices are drawn as segments in a frame: everywhere but
    /// under the desktop's own look.
    readonly property bool segmented: !Appearance.followsSystem
    /// The height a choice is given, where it is a segment; -1 leaves it to
    /// the button.
    readonly property real segmentHeight: frame.segmented ? AppMetrics.segmentHeight : -1
    /// From the frame's edge to the choices: its hairline and then the gap
    /// inside it.
    readonly property real inset: frame.segmented ? AppMetrics.segmentInset + 1 : 0

    /// Whether the frame may be narrower than its choices would like:
    /// it takes what its row gives it, up to its own width, and the choices
    /// share that, their words eliding and whole on hover. Where there
    /// is room it is exactly as wide as its choices, so only a narrow card
    /// sees a difference. Off for a frame that is meant to stretch across its
    /// row, which sets its own width.
    property bool shrinkable: true

    implicitWidth: row.implicitWidth + frame.inset * 2

    Binding {
        target: frame.Layout
        property: "fillWidth"
        value: true
        when: frame.shrinkable
    }
    Binding {
        target: frame.Layout
        property: "maximumWidth"
        value: frame.implicitWidth
        when: frame.shrinkable
    }
    Binding {
        target: frame.Layout
        property: "minimumWidth"
        value: Kirigami.Units.gridUnit * 4
        when: frame.shrinkable
    }
    implicitHeight: row.implicitHeight + frame.inset * 2

    Rectangle {
        id: frameRectangle

        anchors.fill: parent
        visible: frame.segmented
        color: AppColors.field
        border.width: 1
        border.color: AppColors.controlBorder
        radius: AppMetrics.controlRadius
    }

    RowLayout {
        id: row

        anchors.fill: parent
        anchors.margins: frame.inset
        spacing: frame.segmented ? AppMetrics.segmentInset : 0
    }
}
