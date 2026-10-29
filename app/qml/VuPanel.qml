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

// The VU block's own settings panel: the two dials large, with their
// marks, and which level reads 0 VU.
//
// **Almost all dial**, like the Peak/RMS panel: a meter's only settings are
// how it reads, so the card is the needles, a lamp a side that lights when
// that side has reached full scale — and goes out when clicked — then the
// reference and the tray, and no presets row.
//
// **The reference moves the needles, not the sound.** The block publishes
// where each needle is as an amplitude, and the reference chosen here is
// what that amplitude is divided by; a change shows at once, with no
// recompile and nothing sent to the audio thread but the stored choice.
ColumnLayout {
    id: panel

    /// The block whose settings these are. **Not a required property**: the
    /// panel is made from a Component in the register, which cannot see the
    /// loader's context, so the settings panel binds it once it exists.
    property string blockId: ""

    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(panel.blockId) : ({})
    readonly property string reference: panel.params.reference ?? VuScale.defaultReference
    readonly property bool inTray: panel.params.showInTray === true

    readonly property var levels: Meters.frame >= 0 ? Meters.levels(panel.blockId) : ({})
    readonly property bool active: Meters.live && (panel.levels.active ?? false)
    readonly property bool clipped: panel.levels.clipped ?? false
    readonly property var clippedChannels: panel.levels.clippedChannels ?? []
    readonly property real leftVu: VuScale.vuOf((panel.levels.values ?? [])[0] ?? 0, panel.reference)
    readonly property real rightVu: VuScale.vuOf((panel.levels.values ?? [])[1] ?? 0, panel.reference)

    /// The locale the numbers are written in. The user's, unless a test
    /// says otherwise.
    property var locale: Qt.locale()

    /// The controls themselves, so that a test can drive what a person
    /// drives rather than the function behind it.
    readonly property alias referenceBox: referenceControl
    readonly property alias referenceMenu: referenceMenu
    readonly property alias traySwitch: trayControl
    /// The two dials, once the row has made them.
    property VuGauge leftDial: null
    property VuGauge rightDial: null

    /// What this panel asks of the card it is in: room for two dials side
    /// by side, and no presets row.
    readonly property real panelWidth: Kirigami.Units.gridUnit * 20
    readonly property bool wantsPresets: false

    function formatVu(vu: real): string {
        if (vu <= VuScale.bottomVu) {
            return KI18n.i18nc("@info a VU needle resting on its pin, 'Below −20 VU'", "Below %1 VU",
                               Number(VuScale.bottomVu).toLocaleString(panel.locale, 'f', 0));
        }
        return KI18n.i18nc("@info a VU reading", "%1 VU", Number(vu).toLocaleString(panel.locale, 'f', 1));
    }

    /// A reference, as the level that reads 0 VU: "−18 dBFS".
    function referenceLabel(choiceId: string): string {
        return KI18n.i18nc("@item the level in decibels below full scale that reads 0 VU", "%1 dBFS",
                           Number(VuScale.referenceDbOf(choiceId)).toLocaleString(panel.locale, 'f', 0));
    }

    /// What a side reads, in words, for its tooltip and a screen reader.
    function sideText(channel: int): string {
        const reading = panel.formatVu(channel === 0 ? panel.leftVu : panel.rightVu);
        return (panel.clippedChannels[channel] === true)
            ? KI18n.i18nc("@info:tooltip a VU reading, and that the side has clipped", "%1, clipped — click to reset", reading)
            : reading;
    }

    spacing: 0

    // --- the dials ---
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.largeSpacing
        Layout.bottomMargin: Kirigami.Units.largeSpacing
        Layout.leftMargin: Kirigami.Units.smallSpacing * 2
        Layout.rightMargin: Kirigami.Units.smallSpacing * 2
        spacing: Kirigami.Units.largeSpacing

        Repeater {
            model: 2

            delegate: VuGauge {
                id: dial

                required property int index

                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                showMarks: true
                label: dial.index === 0 ? KI18n.i18nc("@label the left channel, under its meter", "L")
                                        : KI18n.i18nc("@label the right channel, under its meter", "R")
                position: VuScale.positionOf(dial.index === 0 ? panel.leftVu : panel.rightVu)
                active: panel.active
                clipped: panel.clippedChannels[dial.index] === true

                Accessible.role: Accessible.Indicator
                Accessible.name: dial.index === 0 ? KI18n.i18nc("@info the left channel's meter", "Left")
                                                  : KI18n.i18nc("@info the right channel's meter", "Right")
                Accessible.description: panel.sideText(dial.index)

                QQC2.ToolTip.text: panel.sideText(dial.index)
                QQC2.ToolTip.visible: dialHover.hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

                HoverHandler {
                    id: dialHover
                }

                // **Clicking puts the light out**, as on every meter.
                TapHandler {
                    enabled: panel.clipped
                    onTapped: Meters.resetClip(panel.blockId)
                }

                Component.onCompleted: {
                    if (dial.index === 0) {
                        panel.leftDial = dial;
                    } else {
                        panel.rightDial = dial;
                    }
                }
            }
        }
    }

    Kirigami.Separator {
        Layout.fillWidth: true
    }

    // --- the reference, and the tray ---
    ColumnLayout {
        Layout.fillWidth: true
        Layout.margins: Kirigami.Units.smallSpacing * 2
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                text: KI18n.i18nc("@label:listbox the level that reads 0 VU", "0 VU is")
                color: AppColors.muted
                Layout.fillWidth: true
                elide: Text.ElideRight
            }

            // The value at the end of the row, as the Peak/RMS panel writes
            // its hold, and still a choice.
            QQC2.ToolButton {
                id: referenceControl

                text: panel.referenceLabel(panel.reference)
                font.family: AppFonts.monoFamily
                font.weight: Font.DemiBold
                Accessible.name: KI18n.i18nc("@label:listbox the level that reads 0 VU", "Reference level")
                Accessible.description: panel.referenceLabel(panel.reference)
                onClicked: referenceMenu.popup(referenceControl, 0, referenceControl.height)

                QQC2.Menu {
                    id: referenceMenu

                    Instantiator {
                        model: VuScale.referenceChoices

                        delegate: QQC2.MenuItem {
                            required property string modelData

                            text: panel.referenceLabel(modelData)
                            checkable: true
                            checked: panel.reference === modelData
                            // An enum travels as its choice's stable id,
                            // never as its position.
                            onTriggered: SessionModel.setParam(panel.blockId, "reference", modelData)
                        }

                        onObjectAdded: (index, object) => referenceMenu.insertItem(index, object)
                        onObjectRemoved: (index, object) => referenceMenu.removeItem(object)
                    }
                }
            }
        }

        QQC2.CheckBox {
            id: trayControl

            text: KI18n.i18nc("@option:check", "Show in system tray")
            Layout.fillWidth: true
            // In the tray's popover, as on the Peak/RMS block.
            QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip",
                                           "Shows this meter when you click Reverblabr's icon in the system tray")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            onToggled: SessionModel.setParam(panel.blockId, "showInTray", trayControl.checked)

            SessionBound {
                control: trayControl
                property: "checked"
                value: panel.inTray
            }
        }
    }
}
