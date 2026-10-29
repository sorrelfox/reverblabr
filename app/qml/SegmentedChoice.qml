// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// One choice in a SegmentedFrame: the Volume block's overdrive, the
// Wave's sides, a virtual device's kind, channels and owner.
//
// **The chosen one filled**, in every look. The Reverblabr style fills a
// checked button with the accent by itself. The desktop style's own checked
// look is a shade of grey that reads as "pressed" rather than "chosen", and
// next to its neighbour it can hardly be told apart; so the chosen button is
// drawn from the selection's colours instead, and the style paints the
// selection's fill with the text colour that goes on it.
QQC2.Button {
    id: choice

    /// Whether it is a segment of its frame's row, as the frame draws one
    /// (SegmentedFrame.segmented): everywhere but under the desktop's look.
    readonly property bool segmented: !Appearance.followsSystem

    checkable: true
    highlighted: choice.checked
    Kirigami.Theme.inherit: !choice.checked
    Kirigami.Theme.colorSet: choice.checked ? Kirigami.Theme.Selection : Kirigami.Theme.Button
    flat: choice.segmented
    Layout.preferredHeight: choice.segmented ? AppMetrics.segmentHeight : -1
    // Sharing a frame that may be narrower than its choices; in any
    // other frame each is as wide as its words, as before.
    Layout.fillWidth: (choice.parent?.parent as SegmentedFrame)?.shrinkable === true
    Layout.minimumWidth: Layout.fillWidth ? Kirigami.Units.gridUnit : -1

    // **A segment is as wide as its share of the row**, and a
    // translation can be a third longer than the word it was made for: when
    // the words no longer fit, they elide and the whole is on hover.
    QQC2.ToolTip.text: choice.text
    QQC2.ToolTip.visible: choice.hovered && choice.implicitContentWidth > choice.availableWidth
    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
}
