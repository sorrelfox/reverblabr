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

// What the application is, under what terms, what it is built on, and where
// to go from here, in the application's own fonts, colours and buttons.
//
// **Not Kirigami's AboutItem.** That brings form cards and headings nothing
// else here has, and lists its libraries as plain lines of its own, which
// leaves out PipeWire and any link. Every link here is a control, so a keyboard
// and a screen reader reach it like any other.
//
// **What it says comes from the application's record** (KAboutData), so
// `--version`, `--license` and this cannot disagree. The addresses are
// AppInfo's.
AppDialog {
    id: dialog

    /// The record to show: anything of KAboutData's shape.
    property var about: AppInfo.aboutData
    /// Where a bug goes. Empty leaves the button out.
    property url bugReportUrl: AppInfo.bugReportUrl

    /// The parts, for a test.
    readonly property alias nameLabel: nameText
    readonly property alias versionLabel: versionText
    readonly property alias descriptionLabel: descriptionText
    readonly property alias websiteButton: websiteAction
    readonly property alias bugButton: bugAction
    readonly property alias contributeButton: contributeAction
    readonly property alias licenceButton: licenceAction
    readonly property alias copyrightLabel: copyrightText
    readonly property alias libraries: libraryRows

    /// The licence the program is under. Empty leaves the link out.
    property string licenceName: AppInfo.licenceName

    component LinkButton: QQC2.Button {
        /// Where a press goes.
        property url target

        Accessible.description: KI18n.i18nc("@info:tooltip what pressing a link in the About dialog does",
                                            "Opens in your web browser")
        onClicked: Qt.openUrlExternally(target)
    }

    /// A link in a line of words: quieter than a button, and just as reachable.
    component TextLink: QQC2.AbstractButton {
        id: link

        /// Where a press goes.
        property url target

        font: AppFonts.body
        implicitWidth: implicitContentWidth
        implicitHeight: implicitContentHeight
        Accessible.role: Accessible.Link
        Accessible.name: link.text
        Accessible.description: KI18n.i18nc("@info:tooltip what pressing a link in the About dialog does",
                                            "Opens in your web browser")
        onClicked: Qt.openUrlExternally(link.target)

        contentItem: QQC2.Label {
            text: link.text
            font.underline: link.hovered || link.visualFocus
            color: AppColors.accentText
        }

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }
    }

    component SectionHeading: QQC2.Label {
        font: AppFonts.heading
        color: AppColors.muted
        Accessible.role: Accessible.Heading
        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.groupSpacing
    }

    title: KI18n.i18nc("@title:window", "About Reverblabr")
    preferredWidth: Kirigami.Units.gridUnit * 27
    standardButtons: Kirigami.Dialog.Close

    ColumnLayout {
        spacing: AppMetrics.panelRowGap

        RowLayout {
            spacing: AppMetrics.wideSpacing
            Layout.fillWidth: true

            Image {
                source: "qrc:/qt/qml/io/github/reverblabr/appicon/sc-apps-io.github.reverblabr.Reverblabr.svg"
                sourceSize: Qt.size(Kirigami.Units.iconSizes.huge, Kirigami.Units.iconSizes.huge)
                fillMode: Image.PreserveAspectFit
                Accessible.ignored: true
                Layout.alignment: Qt.AlignTop
                Layout.preferredWidth: Kirigami.Units.iconSizes.huge
                Layout.preferredHeight: Kirigami.Units.iconSizes.huge
            }

            ColumnLayout {
                spacing: AppMetrics.smallSpacing
                Layout.fillWidth: true

                QQC2.Label {
                    id: nameText

                    text: dialog.about.displayName
                    font: AppFonts.helpTitle
                    color: AppColors.text
                    wrapMode: Text.Wrap
                    Accessible.role: Accessible.Heading
                    Layout.fillWidth: true
                }

                QQC2.Label {
                    id: versionText

                    text: KI18n.i18nc("@info the version of the program, 'Version 1.2.3'", "Version %1",
                                      dialog.about.version)
                    font: AppFonts.small
                    color: AppColors.muted
                    wrapMode: Text.WrapAnywhere
                    Layout.fillWidth: true
                }

                QQC2.Label {
                    id: descriptionText

                    text: dialog.about.shortDescription
                    font: AppFonts.body
                    color: AppColors.text
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    Layout.topMargin: AppMetrics.smallSpacing
                }
            }
        }

        RowLayout {
            spacing: AppMetrics.buttonSpacing
            Layout.fillWidth: true
            Layout.topMargin: AppMetrics.smallSpacing

            LinkButton {
                id: websiteAction

                text: KI18n.i18nc("@action:button the project's page on the web", "Website")
                target: AppInfo.homepageUrl
            }

            LinkButton {
                id: bugAction

                visible: dialog.bugReportUrl.toString() !== ""
                text: KI18n.i18nc("@action:button", "Report Bug")
                target: dialog.bugReportUrl
            }

            LinkButton {
                id: contributeAction

                text: KI18n.i18nc("@action:button how to take part in the project", "Contribute")
                target: AppInfo.contributeUrl
            }
        }

        SectionHeading {
            text: KI18n.i18nc("@title:group the terms the program is under", "Licence")
        }

        RowLayout {
            spacing: AppMetrics.largeSpacing
            Layout.fillWidth: true

            TextLink {
                id: licenceAction

                visible: dialog.licenceName !== ""
                text: dialog.licenceName
                target: AppInfo.licenceUrl
            }

            QQC2.Label {
                id: copyrightText

                text: dialog.about.copyrightStatement
                font: AppFonts.small
                color: AppColors.muted
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }

        SectionHeading {
            text: KI18n.i18nc("@title:group what the program is built on", "Built with")
        }

        Repeater {
            id: libraryRows

            model: dialog.about.components

            RowLayout {
                id: library

                required property var modelData

                /// Its link, for a test.
                readonly property alias button: libraryLink

                spacing: AppMetrics.largeSpacing
                Layout.fillWidth: true

                TextLink {
                    id: libraryLink

                    text: library.modelData.name
                    target: library.modelData.webAddress
                }

                QQC2.Label {
                    text: library.modelData.description
                    font: AppFonts.small
                    color: AppColors.muted
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                QQC2.Label {
                    text: library.modelData.version
                    font: AppFonts.small
                    color: AppColors.muted
                    horizontalAlignment: Text.AlignRight
                    Layout.minimumWidth: Kirigami.Units.gridUnit * 3
                }
            }
        }
    }
}
