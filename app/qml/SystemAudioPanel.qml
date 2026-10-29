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

// Everything the System Audio block can be asked: whether the machine's
// alerts count as system audio, and which applications do not.
//
// **Its own panel rather than the generic one** because the exclusion list
// is not a list of settings. It holds match rules — the strings the engine
// leaves applications out by — and a row of those is unreadable and
// unwritable by hand; what a person wants is a name, chosen from what is
// playing or typed for a program that is not.
//
// **The rule is what is stored, never a stream or an id.** A stream lasts
// as long as the sound does and a session file outlives it by months, so an
// exclusion that named one would come back meaning nothing. A rule means
// the same thing whether the program is running, closed or not yet
// installed, which is also what lets one be added before it ever starts.
ColumnLayout {
    id: panel

    /// The block being shown. The settings panel fills it in; a panel that
    /// required it could not be loaded by something that does not know what
    /// it is loading.
    property string blockId: ""

    /// The type whose words this panel borrows. It is the one type this
    /// panel is for, so it is written down rather than asked of the session.
    readonly property string typeId: "source.system"

    /// Whether the machine's notification sounds are taken as well.
    readonly property bool includeAlerts: SessionModel.revision >= 0
        && SessionModel.param(panel.blockId, "includeAlerts") === true
    /// The applications left out, by match rule, in the order they were
    /// added.
    readonly property var excluded: SessionModel.revision >= 0
        ? (SessionModel.param(panel.blockId, "excluded") ?? [])
        : []

    /// The parts, so that a test drives what a person drives.
    readonly property alias alertsCheck: alertsCheck
    readonly property alias advancedSection: advanced
    readonly property alias advancedButton: advanced.toggle
    readonly property alias excludedSummary: excludedSummary
    readonly property alias excludedRows: excludedRows
    readonly property alias excludeButton: excludeButton
    readonly property alias picker: applicationPicker

    spacing: AppMetrics.panelRowGap

    /// Leaves an application out. **The whole list travels**, because that
    /// is what the parameter is: a list the engine matches against, not a
    /// sequence of additions it has to replay.
    function exclude(rule: string) {
        if (rule === "" || panel.excluded.includes(rule)) {
            return;
        }
        SessionModel.setParam(panel.blockId, "excluded", panel.excluded.concat([rule]));
    }

    /// Takes one back in.
    function include(rule: string) {
        SessionModel.setParam(panel.blockId, "excluded", panel.excluded.filter(entry => entry !== rule));
    }

    // --- what the block takes, as the design says it ---
    InfoLine {
        text: KI18n.i18nc("@info what the System Audio block does",
                          "System Audio captures everything the system plays through PipeWire.")
    }

    // --- alert sounds ---
    //
    // **The setting in bold and why it is off under it**, indented to the
    // box's text, as the design draws a choice that needs a reason.
    QQC2.CheckBox {
        id: alertsCheck

        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.smallSpacing
        text: KI18n.i18nc("@option:check take the machine's notification sounds too", "Include alert sounds")
        font.bold: true
        // Not live: this is not a knob, and each press is one thing somebody
        // decided and one step to undo.
        onToggled: SessionModel.setParam(panel.blockId, "includeAlerts", alertsCheck.checked)

        SessionBound {
            control: alertsCheck
            property: "checked"
            value: panel.includeAlerts
        }
    }

    QQC2.Label {
        Layout.fillWidth: true
        Layout.leftMargin: alertsCheck.indicator.width + alertsCheck.spacing
        // Close under the choice it explains.
        Layout.topMargin: AppMetrics.pullIntoRow
        wrapMode: Text.WordWrap
        color: AppColors.muted
        font: AppFonts.small
        // **Why it is off.** The setting is one word and the reason is not,
        // and somebody reading "include alert sounds" has no way to guess
        // which way round the usual answer is.
        text: KI18n.i18nc("@info:whatsthis why alert sounds are left out by default",
                          "Notification sounds are normally excluded. Turn this on to capture them as well.")
    }

    // --- advanced: the applications left out ---
    //
    // Folded away, because most people want everything and the ones who do
    // not know exactly what they came for.
    CollapsibleSection {
        id: advanced

        title: KI18n.i18nc("@title:group the less usual settings", "Advanced")

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                text: KI18n.i18nc("@label the applications System Audio leaves out", "Excluded sources:")
                color: AppColors.muted
            }

            QQC2.Label {
                id: excludedSummary

                visible: panel.excluded.length === 0
                text: KI18n.i18nc("@info nothing is being left out", "None")
                color: AppColors.text
                Layout.fillWidth: true
            }
        }

        Repeater {
            id: excludedRows

            model: panel.excluded

            delegate: RowLayout {
                id: excludedRow

                required property string modelData

                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Icon {
                    source: Inventory.revision >= 0
                        ? (Inventory.application(excludedRow.modelData).icon ?? "")
                        : ""
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: Kirigami.Units.iconSizes.small
                    Accessible.ignored: true
                }

                QQC2.Label {
                    // What the program calls itself while it is playing, and
                    // what the rule says when it is not — which is the rule's
                    // own subject, and what somebody typed to begin with.
                    text: Inventory.revision >= 0
                        ? (RecentApplications.nameFor(excludedRow.modelData)
                            || Inventory.applicationName(excludedRow.modelData))
                        : excludedRow.modelData
                    color: AppColors.text
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                QQC2.ToolButton {
                    icon.name: "list-remove"
                    display: QQC2.AbstractButton.IconOnly
                    Accessible.name: KI18n.i18nc("@action:button stop leaving an application out",
                                                 "Take %1 back in",
                                                 Inventory.applicationName(excludedRow.modelData))
                    QQC2.ToolTip.text: Accessible.name
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    onClicked: panel.include(excludedRow.modelData)
                }
            }
        }

        // **As wide as its words**, as the design draws it: a button the
        // width of the panel reads as the panel's main action, and this is
        // an exception to the usual.
        QQC2.Button {
            id: excludeButton

            icon.name: "list-add"
            text: KI18n.i18nc("@action:button open the application picker", "Exclude source")
            onClicked: applicationPicker.open()

            // **Declared on the button rather than beside it**: a popup is
            // not laid out, but anything written inside a layout is treated
            // as though it were, and it opens where the eye already is.
            ApplicationPicker {
                id: applicationPicker

                y: excludeButton.height + Kirigami.Units.smallSpacing
                leavingOut: true
                alreadyChosen: panel.excluded
                onChosen: rule => panel.exclude(rule)
            }
        }

        InfoLine {
            text: KI18n.i18nc("@info what leaving an application out does",
                              "By default all system audio is captured. Add applications here to leave them out.")
        }
    }
}
