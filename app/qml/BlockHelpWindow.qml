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

// **A block's help, in a window of its own**: what the block is for, how
// it works, what each of its settings does, when to use it and a few tips
// (docs/adr/0029-block-help.md).
//
// **A window, not a dialog over the canvas**, and kept above the others
// where the compositor allows, because it is read beside the settings it
// explains: a dialog would cover the very panel it is about. It belongs to
// no other window, so it stays where it was put while the main window is
// moved, minimised or closed to the tray.
//
// **Set as a page to be read.** A column of at most AppMetrics.helpMeasure
// characters, whatever the window's width, with the text larger and the
// lines further apart than a panel's, and a heading for each part and each
// setting. Everything is drawn with the tokens, so both appearances and
// the desktop's own scheme come out right.
QQC2.ApplicationWindow {
    id: root

    /// The kind of block whose help is showing.
    property string typeId: ""

    Kirigami.Theme.colorSet: Kirigami.Theme.Window
    Kirigami.Theme.inherit: false
    color: root.ownTitleBar ? "transparent" : AppColors.window

    readonly property bool ownTitleBar: Appearance.ownStyle
    readonly property string stateName: "BlockHelp"

    /// The page, or null for a block whose help is still to write.
    readonly property HelpPage page: pageLoader.item as HelpPage
    readonly property string blockName: BlockPresentation.name(root.typeId)

    /// The parts, for the tests.
    readonly property alias titleBar: titleBar
    readonly property alias chrome: chrome
    readonly property alias view: view
    readonly property alias column: column
    readonly property alias heading: heading
    readonly property alias summaryLabel: summaryLabel
    readonly property alias sectionList: sectionList

    /// The widest a line of text may be: the measure, in the body's own
    /// average character.
    readonly property real measureWidth: Math.ceil(bodyMetrics.averageCharacterWidth * AppMetrics.helpMeasure)

    /// What the page says when there is no page: what the library says of
    /// the block, and where to read more.
    readonly property var missingSections: [{
        title: BlockHelp.aboutTitle,
        setting: false,
        paragraphs: [KI18n.i18nc("@info a block's help page that has not been written yet", "This block's help page is still to be written. Until it is, its settings are described in the handbook.")],
        points: [],
        closing: []
    }]

    /// Writes the window's size down, as the other windows do.
    function saveSize() {
        if (root.visibility !== Window.Maximized && root.visibility !== Window.FullScreen) {
            WindowState.saveWindowSize(root.stateName, root.width, root.height);
        }
    }

    /// Puts the window beside \a near, at the right of its screen, the
    /// first time it is shown. **Only where the compositor lets an
    /// application say**: a Wayland compositor places a new window itself.
    function placeBeside(near: var) {
        const screen = near?.screen ?? root.screen;
        if (!screen) {
            return;
        }
        const room = Kirigami.Units.gridUnit * 2;
        root.x = screen.virtualX + Math.max(0, screen.width - root.width - room);
        root.y = screen.virtualY + Math.max(room, Math.round((screen.height - root.height) / 2));
    }

    title: KI18n.i18nc("@title:window %1 is a block's name", "%1 Help", root.blockName)
    // **Above the other windows**, so it is not lost behind the main
    // window the moment the panel it explains is clicked.
    flags: (root.ownTitleBar ? (Qt.Window | Qt.FramelessWindowHint) : Qt.Window) | Qt.WindowStaysOnTopHint

    // **A page's width and most of a screen's height**: the measure and its
    // margins, unless the person has made it otherwise.
    width: WindowState.windowSize(root.stateName).width > 0
        ? WindowState.windowSize(root.stateName).width
        : Math.round(root.measureWidth + AppMetrics.helpPagePadding * 2 + Kirigami.Units.gridUnit)
    height: WindowState.windowSize(root.stateName).height > 0
        ? WindowState.windowSize(root.stateName).height : Kirigami.Units.gridUnit * 36
    minimumWidth: Kirigami.Units.gridUnit * 18
    minimumHeight: Kirigami.Units.gridUnit * 14

    onClosing: root.saveSize()
    // A new page starts at its top.
    onTypeIdChanged: view.contentItem.contentY = 0

    FontMetrics {
        id: bodyMetrics

        font: AppFonts.helpBody
    }

    Loader {
        id: pageLoader

        sourceComponent: BlockHelp.pageFor(root.typeId)
    }

    Shortcut {
        sequences: [StandardKey.Close, "Escape"]
        onActivated: root.close()
    }

    menuBar: WindowTitleBar {
        id: titleBar

        visible: root.ownTitleBar
        titleBar: root.ownTitleBar
        frameRadius: chrome.frameRadius
        maximized: chrome.edgeToEdge
        onWindowMenuRequested: chrome.showWindowMenu()

        Kirigami.Icon {
            source: AppGlyphs.help
            isMask: true
            color: AppColors.muted
            implicitWidth: AppMetrics.panelIconSize
            implicitHeight: AppMetrics.panelIconSize
            Accessible.ignored: true
        }

        QQC2.Label {
            text: root.title
            color: AppColors.text
            font: AppFonts.strong
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }

    // The text of the page: a heading, a paragraph, a point.
    component PageText: QQC2.Label {
        Layout.fillWidth: true
        color: AppColors.text
        wrapMode: Text.Wrap
        lineHeight: AppMetrics.helpLineHeight
        textFormat: Text.PlainText
    }

    QQC2.ScrollView {
        id: view

        anchors.fill: parent
        contentWidth: availableWidth
        QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

        Item {
            width: view.availableWidth
            implicitHeight: column.implicitHeight + AppMetrics.helpPagePadding * 2

            ColumnLayout {
                id: column

                // **The measure, centred**: a wide window adds margin, not
                // longer lines.
                width: Math.min(root.measureWidth, parent.width - AppMetrics.helpPagePadding * 2)
                x: Math.round((parent.width - width) / 2)
                y: AppMetrics.helpPagePadding
                spacing: 0

                // --- the block, and what it is for ---
                RowLayout {
                    Layout.fillWidth: true
                    spacing: AppMetrics.largeSpacing

                    // **The tile's own icon and tint**, so the page and
                    // the block it explains are obviously the same thing.
                    Kirigami.Icon {
                        source: BlockPresentation.icon(root.typeId)
                        color: AppColors.categoryTint(BlockPresentation.category(root.typeId))
                        isMask: true
                        implicitWidth: heading.implicitHeight
                        implicitHeight: heading.implicitHeight
                        Layout.alignment: Qt.AlignTop
                        Accessible.ignored: true
                    }

                    PageText {
                        id: heading

                        text: root.blockName
                        font: AppFonts.helpTitle
                        lineHeight: 1
                        Accessible.role: Accessible.Heading
                    }
                }

                PageText {
                    id: summaryLabel

                    text: root.page?.summary ?? BlockPresentation.description(root.typeId)
                    font: AppFonts.helpLead
                    color: AppColors.muted
                    Layout.topMargin: AppMetrics.helpHeadingGap
                }

                // --- its parts and settings ---
                Repeater {
                    id: sectionList

                    model: root.page?.sections ?? root.missingSections

                    delegate: ColumnLayout {
                        id: section

                        required property var modelData
                        required property int index

                        Layout.fillWidth: true
                        // A part stands well clear of what came before it; a
                        // setting a little less, under its part.
                        Layout.topMargin: section.modelData.setting ? AppMetrics.helpSettingGap : AppMetrics.helpSectionGap
                        spacing: AppMetrics.helpParagraphGap

                        PageText {
                            text: section.modelData.title
                            font: section.modelData.setting ? AppFonts.helpSubheading : AppFonts.helpHeading
                            color: section.modelData.setting ? AppColors.accentText : AppColors.text
                            lineHeight: 1.15
                            Layout.bottomMargin: AppMetrics.helpHeadingGap - AppMetrics.helpParagraphGap
                            Accessible.role: Accessible.Heading
                        }

                        Repeater {
                            model: section.modelData.paragraphs

                            delegate: PageText {
                                required property string modelData

                                text: modelData
                                font: AppFonts.helpBody
                            }
                        }

                        Repeater {
                            model: section.modelData.points

                            delegate: RowLayout {
                                id: point

                                required property string modelData

                                Layout.fillWidth: true
                                spacing: 0

                                QQC2.Label {
                                    // no-i18n: a bullet, not a word
                                    text: "•"
                                    font: AppFonts.helpBody
                                    color: AppColors.muted
                                    Layout.preferredWidth: AppMetrics.helpPointIndent
                                    Layout.alignment: Qt.AlignTop
                                    Accessible.ignored: true
                                }

                                PageText {
                                    text: point.modelData
                                    font: AppFonts.helpBody
                                }
                            }
                        }

                        Repeater {
                            model: section.modelData.closing

                            delegate: PageText {
                                required property string modelData

                                text: modelData
                                font: AppFonts.helpBody
                            }
                        }
                    }
                }

                // --- the handbook, for a block with no page yet ---
                QQC2.Button {
                    visible: root.page === null
                    text: KI18n.i18nc("@action:button", "Open the Handbook")
                    icon.name: "help-contents"
                    Layout.topMargin: AppMetrics.helpParagraphGap
                    onClicked: Qt.openUrlExternally(AppInfo.handbookUrl)
                }
            }
        }
    }

    WindowChrome {
        id: chrome

        window: root
        active: root.ownTitleBar
    }
}
