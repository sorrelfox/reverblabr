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

// Where the floating window sits: its size, its screen and its two margins.
//
// **Not a setting of any block.** It is this desktop's (FloatingSurface), and
// one window holds every block's row, so this reads and writes there and no
// edit or undo comes of it. Each block whose card has it shows the same place.
CollapsibleSection {
    id: place

    readonly property var placement: FloatingSurface.placementRevision >= 0 ? FloatingSurface.placement("label") : ({})
    /// The screen the numbers below are measured on.
    readonly property var placedOn: {
        return FloatingWindows.screenNamed(place.placement.screen ?? "") ?? FloatingWindows.screens[0] ?? null;
    }

    /// The controls, so that a test drives what a person drives.
    readonly property alias sizeChoices: sizeRepeater
    readonly property alias screenChoice: screenControl
    readonly property alias leftField: leftControl
    readonly property alias topField: topControl

    readonly property SpeechNowWords words: SpeechNowWords {}

    RowLayout {
        Layout.fillWidth: true
        spacing: AppMetrics.labelSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label how big the floating label is", "Size")
            color: AppColors.muted
        }

        SegmentedFrame {
            shrinkable: true
            Layout.fillWidth: true

            QQC2.ButtonGroup {
                id: sizeGroup
            }

            Repeater {
                id: sizeRepeater

                model: ["small", "large"]

                delegate: SegmentedChoice {
                    id: sizeButton

                    required property string modelData

                    text: place.words.sizeName(sizeButton.modelData)
                    Accessible.name: text
                    QQC2.ButtonGroup.group: sizeGroup
                    onClicked: FloatingSurface.setPlacement("label", { size: sizeButton.modelData })

                    SessionBound {
                        control: sizeButton
                        property: "checked"
                        value: place.placement.size === sizeButton.modelData
                    }
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: AppMetrics.labelSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label which screen the floating label is on", "Screen")
            color: AppColors.muted
        }

        QQC2.ComboBox {
            id: screenControl

            readonly property var names: FloatingWindows.screens.map(screen => screen.name).filter(name => name !== "")

            Layout.fillWidth: true
            model: [KI18n.i18nc("@item:inlistbox the screen the main window is on", "The window's")].concat(screenControl.names)
            currentIndex: screenControl.names.indexOf(place.placement.screen ?? "") + 1
            Accessible.name: KI18n.i18nc("@label which screen the floating label is on", "Screen")
            onActivated: index => FloatingSurface.setPlacement("label", { screen: index === 0 ? "" : screenControl.names[index - 1] })
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: AppMetrics.labelSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label how far the floating label sits from the left of the screen", "From the left")
            color: AppColors.muted
            Layout.fillWidth: true
        }

        ValueField {
            id: leftControl

            Layout.preferredWidth: Kirigami.Units.gridUnit * 4
            label: KI18n.i18nc("@label how far the floating label sits from the left of the screen", "From the left")
            value: place.placement.left ?? 0
            to: place.placedOn?.width ?? 16384
            decimals: 0
            stepSize: 10
            pageStepSize: 100
            onValueEdited: number => FloatingSurface.setPlacement("label", { left: number })
        }

        QQC2.Label {
            text: KI18n.i18nc("@label the unit of a place on the screen", "px")
            color: AppColors.muted
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: AppMetrics.labelSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label how far the floating label sits from the top of the screen", "From the top")
            color: AppColors.muted
            Layout.fillWidth: true
        }

        ValueField {
            id: topControl

            Layout.preferredWidth: Kirigami.Units.gridUnit * 4
            label: KI18n.i18nc("@label how far the floating label sits from the top of the screen", "From the top")
            value: place.placement.top ?? 0
            to: place.placedOn?.height ?? 16384
            decimals: 0
            stepSize: 10
            pageStepSize: 100
            onValueEdited: number => FloatingSurface.setPlacement("label", { top: number })
        }

        QQC2.Label {
            text: KI18n.i18nc("@label the unit of a place on the screen", "px")
            color: AppColors.muted
        }
    }
}
