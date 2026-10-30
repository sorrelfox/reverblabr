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

// The Limiter's settings: the ceiling, how quickly it lets go, whether
// it watches between the samples, and how hard it is working now.
//
// **A panel of its own** because the ceiling is said in dBTP or dBFS
// depending on the switch under it, the release wants a logarithmic slider,
// and the reduction is a reading rather than a setting.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(panel.blockId) : ({})
    readonly property real ceiling: panel.params.ceiling ?? words.defaultCeiling
    readonly property real release: panel.params.release ?? words.defaultRelease
    readonly property bool truePeak: panel.params.truePeak ?? true

    readonly property var levels: Meters.frame >= 0 ? Meters.levels(panel.blockId) : ({})
    readonly property bool live: Meters.live && (panel.levels.values ?? []).length > words.reductionValue
    readonly property real reduction: panel.live ? (panel.levels.values[words.reductionValue] ?? 0) : 0

    /// The controls, so that a test drives what a person drives.
    readonly property alias ceilingSlider: ceilingControl
    readonly property alias ceilingLabel: ceilingText
    readonly property alias releaseSlider: releaseControl
    readonly property alias releaseLabel: releaseText
    readonly property alias truePeakSwitch: truePeakControl
    readonly property alias reductionLabel: reductionText
    readonly property alias words: words

    spacing: AppMetrics.panelRowGap

    LimiterWords {
        id: words
    }

    // --- the ceiling ---
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label:slider the level a Limiter never lets the sound past", "Ceiling")
            color: AppColors.text
            Layout.fillWidth: true
        }

        QQC2.Label {
            id: ceilingText

            text: words.ceilingText(ceilingControl.value, panel.truePeak)
            color: AppColors.accentText
            font.family: AppFonts.monoFamily
        }
    }

    QQC2.Slider {
        id: ceilingControl

        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.pullIntoRow
        from: words.lowestCeiling
        to: 0
        stepSize: 0.1
        snapMode: QQC2.Slider.SnapAlways
        Accessible.name: KI18n.i18nc("@label:slider", "Ceiling")
        Accessible.description: ceilingText.text

        onMoved: SessionModel.setParam(panel.blockId, "ceiling", Math.round(value * 10) / 10, true)
        onPressedChanged: if (!pressed) {
            SessionModel.commitParam(panel.blockId, "ceiling");
        }

        SessionBound {
            control: ceilingControl
            property: "value"
            value: panel.ceiling
            suspended: ceilingControl.pressed
        }
    }

    // --- how quickly it lets go ---
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.panelRowGap
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label:slider how quickly a Limiter lets go", "Release")
            color: AppColors.text
            Layout.fillWidth: true
        }

        QQC2.Label {
            id: releaseText

            text: words.releaseText(words.releaseAt(releaseControl.value))
            color: AppColors.accentText
            font.family: AppFonts.monoFamily
        }
    }

    QQC2.Slider {
        id: releaseControl

        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.pullIntoRow
        from: 0
        to: 1
        Accessible.name: KI18n.i18nc("@label:slider", "Release")
        Accessible.description: releaseText.text

        // Whole milliseconds: a fraction of one is nothing a release does.
        onMoved: SessionModel.setParam(panel.blockId, "release", Math.round(words.releaseAt(value)), true)
        onPressedChanged: if (!pressed) {
            SessionModel.commitParam(panel.blockId, "release");
        }

        SessionBound {
            control: releaseControl
            property: "value"
            value: words.releasePosition(panel.release)
            suspended: releaseControl.pressed
        }
    }

    QQC2.Switch {
        id: truePeakControl

        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.panelRowGap
        text: KI18n.i18nc("@option:check a Limiter detects true peaks", "Catch peaks between samples")
        // Its words elide in a narrow panel; they are whole on hover.
        QQC2.ToolTip.text: truePeakControl.text
        QQC2.ToolTip.visible: truePeakControl.hovered && truePeakControl.implicitContentWidth > truePeakControl.availableWidth
        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
        onToggled: SessionModel.setParam(panel.blockId, "truePeak", truePeakControl.checked)

        SessionBound {
            control: truePeakControl
            property: "checked"
            value: panel.truePeak
        }
    }

    InfoLine {
        text: KI18n.i18nc("@info what true-peak limiting is for",
                          "A signal can rise between its samples higher than any of them. Catching those peaks keeps a converter or an encoder further down from clipping.")
    }

    // --- how hard it is working ---
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.panelRowGap
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label how much gain a Limiter is taking off now", "Gain reduction")
            color: AppColors.muted
            Layout.fillWidth: true
        }

        QQC2.Label {
            id: reductionText

            text: panel.live ? words.reductionText(panel.reduction)
                             : KI18n.i18nc("@info no reading while the session is stopped", "—")
            color: AppColors.text
            font.family: AppFonts.monoFamily
        }
    }
}
