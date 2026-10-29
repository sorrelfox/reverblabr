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

// A line of explanation with the information sign before it, as the design
// writes what a block does and what a list is for. Shared by the panels
// that have one (System Audio, Simple Compressor).
RowLayout {
    id: line

    property alias text: words.text
    /// The words, for a test.
    readonly property alias label: words

    Layout.fillWidth: true
    spacing: AppMetrics.followsSystem ? Kirigami.Units.smallSpacing : AppMetrics.largeSpacing

    Kirigami.Icon {
        // The prototype's own circled "i" in the Reverblabr appearance,
        // at its fourteen pixels.
        source: AppMetrics.followsSystem ? "dialog-information-symbolic" : AppGlyphs.info
        // Recoloured, not the theme's blue: an explanation, muted like its
        // words, as the design draws it.
        isMask: true
        color: AppColors.muted
        implicitWidth: AppMetrics.panelIconSize
        implicitHeight: AppMetrics.panelIconSize
        Layout.alignment: Qt.AlignTop
        Accessible.ignored: true
    }

    QQC2.Label {
        id: words

        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: AppColors.muted
        font: AppFonts.small
    }
}
