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

// The Sync block's settings: the delay as a number to type, a slider
// to get near it, and what it comes to in samples while the session runs.
//
// **A box for the exact number.** Lining a sound up with a picture is done
// a millisecond or less at a time, by ear; the box takes a hundredth of a
// millisecond and its arrows step a whole one. The slider is square-law
// (SyncScale), so the short delays have room on it.
//
// **The samples line is the delay reported, not a setting.** It is what the
// block says it is holding the lane back by at the rate the session runs
// at — the number to compare with a video editor's — and it says that the
// delay is intended: a Sync block is not lined up against the other lanes
// the way a block's latency is.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(panel.blockId) : ({})
    readonly property real delayMs: panel.params.delay ?? 0

    readonly property var levels: Meters.frame >= 0 ? Meters.levels(panel.blockId) : ({})
    /// Whether the block is running and has said what it is doing.
    readonly property bool reporting: Meters.live && (panel.levels.values ?? []).length > 0
    /// What the block says it is holding the lane back by, in samples.
    readonly property int delaySamples: Math.round((panel.levels.values ?? [])[0] ?? 0)

    /// The locale numbers are written and read in. The user's, unless a
    /// test says otherwise.
    property var locale: Qt.locale()

    /// The controls themselves, so that a test drives what a person drives.
    readonly property alias delayBox: delayControl
    readonly property alias slider: delaySlider
    readonly property alias samplesLine: samplesText
    readonly property alias delayScale: delayScale

    spacing: AppMetrics.panelRowGap

    SyncScale {
        id: delayScale
    }

    /// The samples line: "5,760 samples at 48 kHz", while the session runs.
    function samplesText(samples: int, rate: int): string {
        return KI18n.i18ncp("@info a Sync block's delay in samples, at the rate the session runs at",
                            "%1 sample at %2 kHz", "%1 samples at %2 kHz", samples,
                            Number(rate / 1000).toLocaleString(panel.locale, 'f', rate % 1000 === 0 ? 0 : 1));
    }

    /// Reads a typed delay in milliseconds, in the reader's locale or with
    /// a point, or NaN.
    function parseMs(typed: string): real {
        const text = typed.trim().replace(/\s*ms$/i, "");
        let number = NaN;
        try {
            number = Number.fromLocaleString(panel.locale, text);
        } catch (error) {
            number = parseFloat(text.replace(",", "."));
        }
        return number;
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label:spinbox how long a Sync block holds the sound back", "Delay")
            color: AppColors.text
            Layout.fillWidth: true
        }

        // In hundredths of a millisecond, which a spin box counts in whole
        // numbers; the arrows step a whole millisecond.
        QQC2.SpinBox {
            id: delayControl

            from: 0
            to: Math.round(delayScale.maximumMs * 100)
            stepSize: 100
            editable: true
            font.family: AppFonts.monoFamily
            Layout.preferredWidth: Kirigami.Units.gridUnit * 7
            Accessible.name: KI18n.i18nc("@label:spinbox", "Delay in milliseconds")

            validator: DoubleValidator {
                bottom: 0
                top: delayScale.maximumMs
                decimals: 2
                notation: DoubleValidator.StandardNotation
                locale: panel.locale.name
            }
            textFromValue: (value, locale) => delayScale.number(value / 100, panel.locale)
            valueFromText: (text, locale) => {
                const ms = panel.parseMs(text);
                return isNaN(ms) ? delayControl.value : Math.round(Math.max(0, Math.min(delayScale.maximumMs, ms)) * 100);
            }
            onValueModified: SessionModel.setParam(panel.blockId, "delay", delayControl.value / 100)

            SessionBound {
                control: delayControl
                property: "value"
                value: Math.round(panel.delayMs * 100)
            }
        }

        QQC2.Label {
            text: KI18n.i18nc("@label the unit after a delay box", "ms")
            color: AppColors.muted
            font.family: AppFonts.monoFamily
        }
    }

    QQC2.Slider {
        id: delaySlider

        Layout.fillWidth: true
        from: 0
        to: 1
        Accessible.name: KI18n.i18nc("@label:slider how long a Sync block holds the sound back", "Delay")

        onMoved: SessionModel.setParam(panel.blockId, "delay", delayScale.msAt(value), true)
        onPressedChanged: if (!pressed) {
            SessionModel.commitParam(panel.blockId, "delay");
        }

        SessionBound {
            control: delaySlider
            property: "value"
            value: delayScale.positionOf(panel.delayMs)
            suspended: delaySlider.pressed
        }
    }

    QQC2.Label {
        id: samplesText

        visible: panel.reporting && AppShell.sampleRate > 0
        text: panel.samplesText(panel.delaySamples, AppShell.sampleRate)
        color: AppColors.muted
        font: AppFonts.small
        wrapMode: Text.WordWrap
        Layout.fillWidth: true

        QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip",
                                       "This delay is added on purpose, so the other lanes are not held back to match it.")
        QQC2.ToolTip.visible: samplesHover.hovered
        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

        HoverHandler {
            id: samplesHover
        }
    }
}
