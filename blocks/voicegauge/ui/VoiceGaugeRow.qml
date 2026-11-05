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

// One Voice Gauge on the floating window: a dot, its name, the number, the
// word on the right and the track beneath. It has no buttons, takes no focus
// and nothing on it pulses.
ColumnLayout {
    id: row

    /// FloatingSurface's row.
    required property var modelData
    required property bool large

    readonly property VoiceGaugeWords words: VoiceGaugeWords {}
    readonly property real published: row.modelData.state
    readonly property real level: row.modelData.reading
    /// Only from a block that runs: one switched off keeps its last slots.
    readonly property var marks: row.modelData.watch === "watching" && row.modelData.enabled
        ? row.words.marksOf(row.modelData.marks) : ({ upper: row.words.notSet, lower: row.words.notSet })
    readonly property string kind: row.words.kindOf(row.modelData.watch, row.modelData.enabled, row.published, row.level)
    readonly property string name: row.modelData.nickname !== ""
        ? row.modelData.nickname : BlockPresentation.name("meter.voicegauge")
    readonly property string number: row.words.headline(row.kind, row.level)
    readonly property string shownWord: row.words.line(row.kind, row.published, row.modelData.watch)
    readonly property alias bar: track

    spacing: 0
    Accessible.ignored: true

    RowLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.leftMargin: Kirigami.Units.smallSpacing * 2
        Layout.rightMargin: Kirigami.Units.smallSpacing * 2
        spacing: Kirigami.Units.smallSpacing * 2

        VoiceGaugeDot {
            dot: row.words.dot(row.kind)
            size: row.large ? 16 : 10
            Layout.alignment: Qt.AlignVCenter
        }

        ElidedLabel {
            text: row.name
            color: AppColors.muted
            font: row.large ? AppFonts.body : AppFonts.small
            elide: Text.ElideRight
            Layout.maximumWidth: row.width / 4
            Accessible.ignored: true
        }

        ElidedLabel {
            text: row.number
            color: AppColors.text
            font: AppFonts.readout
            elide: Text.ElideRight
            Accessible.ignored: true
        }

        ElidedLabel {
            text: row.shownWord
            color: row.kind === "noSound" ? AppColors.warningText : AppColors.text
            font: row.large ? AppFonts.body : AppFonts.small
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignRight
            Layout.fillWidth: true
            Accessible.ignored: true
        }
    }

    VoiceGaugeBar {
        id: track

        level: row.level
        shown: row.words.showsNumber(row.kind)
        held: row.kind === "held"
        upper: row.marks.upper
        lower: row.marks.lower
        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.smallSpacing * 2
        Layout.rightMargin: Kirigami.Units.smallSpacing * 2
        Layout.bottomMargin: Kirigami.Units.smallSpacing
    }
}
