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

// A part of a block's settings under a heading that folds it away:
// a rule, a chevron and the heading in bold, as every panel in the design
// draws "Advanced" and the Application block its "Audio device selection".
//
// **The heading is the control**, rather than a button beside it: the
// whole line is what a person reaches for, and a chevron that is the only
// thing to press is a small target for something pressed as often as this.
ColumnLayout {
    id: section

    /// What the part is called.
    required property string title
    /// Whether what is under the heading is showing. Starts folded, because
    /// what goes in a section is what most people never need.
    property bool expanded: false

    /// What is under the heading.
    default property alias content: body.data

    /// The heading, for a test that presses what a person presses.
    readonly property alias toggle: heading

    Layout.fillWidth: true
    // The heading's own padding is the gap in the prototype.
    spacing: AppMetrics.followsSystem ? Kirigami.Units.smallSpacing : 0

    onExpandedChanged: heading.checked = section.expanded

    // **Edge to edge of the card**, as the prototype rules a section off,
    // rather than inset with the rows; while following the system it stays
    // with them.
    Kirigami.Separator {
        Layout.fillWidth: true
        Layout.leftMargin: AppMetrics.followsSystem ? 0 : -AppMetrics.panelSidePadding
        Layout.rightMargin: AppMetrics.followsSystem ? 0 : -AppMetrics.panelSidePadding
    }

    QQC2.AbstractButton {
        id: heading

        Layout.fillWidth: true
        checkable: true
        checked: section.expanded
        padding: Kirigami.Units.smallSpacing
        // The rows' own edge in the prototype, where the chevron lines up
        // with the labels under it.
        leftPadding: AppMetrics.followsSystem ? Kirigami.Units.smallSpacing : 0
        rightPadding: AppMetrics.followsSystem ? Kirigami.Units.smallSpacing : 0
        topPadding: AppMetrics.sectionTopPadding
        bottomPadding: AppMetrics.sectionBottomPadding
        // **Both ways**: pressing the heading sets the section, and so does
        // anything that toggles the button without pressing it. A binding
        // alone would break at the first press and stop following.
        onCheckedChanged: section.expanded = heading.checked
        // A button that stays pressed while the part is showing: which way it
        // is folded is its checked state, with no words of its own to say it.
        Accessible.role: Accessible.Button
        Accessible.name: section.title
        Accessible.checkable: true
        Accessible.checked: section.expanded

        /// The heading's colour: in the Reverblabr appearance it takes the
        /// accent under the pointer, as the prototype's does; the
        /// desktop's look has no such thing and keeps the text colour.
        readonly property color ink: heading.hovered && !Appearance.followsSystem ? AppColors.accent
                                                                                 : AppColors.text

        contentItem: RowLayout {
            spacing: AppMetrics.followsSystem ? Kirigami.Units.smallSpacing : AppMetrics.mediumSpacing

            Kirigami.Icon {
                source: section.expanded ? "arrow-down" : "arrow-right"
                implicitWidth: AppMetrics.sectionChevronSize
                implicitHeight: AppMetrics.sectionChevronSize
                color: heading.ink
                Accessible.ignored: true
            }

            QQC2.Label {
                text: section.title
                // The prototype's headings are semibold; the desktop's look
                // keeps them bold.
                font.weight: Appearance.followsSystem ? Font.Bold : AppFonts.demiBold
                color: heading.ink
                Layout.fillWidth: true
            }
        }
    }

    ColumnLayout {
        id: body

        Layout.fillWidth: true
        // The first row's own top in the prototype, under the heading.
        Layout.topMargin: AppMetrics.followsSystem ? 0 : AppMetrics.panelEndPadding
        visible: section.expanded
        spacing: AppMetrics.panelRowGap
    }
}
