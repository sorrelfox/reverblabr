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

// **Reverblabr's own settings, in a window of their own**: the language it
// speaks, its appearance and when Auto Run runs, on one page that scrolls,
// a section each, with room for what comes next.
//
// **The same settings the menus change, through the same singletons**
// (LanguageSetting, Appearance, AutoRunSetting), so the menus and this
// window never disagree: each choice here is put back to what its setting
// says whenever the setting says anything, rather than bound to it, since a
// control that is clicked drops its binding.
//
// **What only a restart changes says so where it is chosen** (ADR-0036):
// a language, and the controls and the title bar of an appearance. The row
// that says so has Restart Now, and says too when a restart stops sessions:
// those of an engine running in the window.
//
// **A window, not a dialog**, framed as the Virtual Devices window is
// (ADR-0012): with no frame of KWin's in the Reverblabr appearances, its
// title in a WindowTitleBar and the edges a WindowChrome draws; KWin's frame
// and title while following the system. It stays above the main window,
// which it belongs to, and goes with it when the application quits.
QQC2.ApplicationWindow {
    id: root

    Kirigami.Theme.colorSet: Kirigami.Theme.Window
    Kirigami.Theme.inherit: false
    color: root.ownTitleBar ? "transparent" : AppColors.window

    /// Whether the window draws its own title bar: the main window's answer,
    /// fixed at start with the control style.
    readonly property bool ownTitleBar: Appearance.ownStyle
    /// Where the window's size is kept in the state file.
    readonly property string stateName: "Settings"

    /// Whether the controls chosen are another style than the one the window
    /// started with, which Qt cannot change in a window that is open
    /// (Appearance): the colours and fonts have changed already.
    readonly property bool styleRestartNeeded: (Appearance.mode !== Appearance.FollowSystem) !== Appearance.ownStyle
    /// Whether a restart stops sessions: those of an engine that runs in the
    /// window, asked for or fallen back to (ADR-0017).
    readonly property bool restartStopsSessions: !AppShell.engineInBackground

    /// The parts, for the tests.
    readonly property alias titleBar: titleBar
    readonly property alias chrome: chrome
    readonly property alias scroll: scroll
    readonly property alias languageSection: languageSection
    readonly property alias appearanceSection: appearanceSection
    readonly property alias autoRunSection: autoRunSection
    readonly property alias languageBox: languageBox
    readonly property alias languageHelp: languageHelp
    readonly property alias languageRestart: languageRestart
    readonly property alias darkChoice: darkChoice
    readonly property alias lightChoice: lightChoice
    readonly property alias systemChoice: systemChoice
    readonly property alias appearanceRestart: appearanceRestart
    readonly property alias onceALoginChoice: onceALoginChoice
    readonly property alias everyStartChoice: everyStartChoice
    readonly property alias autoRunUnavailable: autoRunUnavailable
    readonly property alias restartFailedMessage: restartFailedMessage

    /// Restart Now was pressed. The main window writes down what it keeps
    /// and starts Reverblabr again (Restart).
    signal restartRequested

    /// Writes the window's size down; the window's own close does, and the
    /// main window's, which quits the application with this one still open.
    function saveSize() {
        if (root.visibility !== Window.Maximized && root.visibility !== Window.FullScreen) {
            WindowState.saveWindowSize(root.stateName, root.width, root.height);
        }
    }

    /// **Every choice shows what its setting says**, written, not bound: the
    /// settings get the last word, whoever changed them.
    function showSettings() {
        languageBox.currentIndex = Math.max(0, languageBox.indexOfValue(LanguageSetting.chosen));
        darkChoice.checked = Appearance.mode === Appearance.ReverblabrDark;
        lightChoice.checked = Appearance.mode === Appearance.ReverblabrLight;
        systemChoice.checked = Appearance.mode === Appearance.FollowSystem;
        onceALoginChoice.checked = !AutoRunSetting.everyEngineStart;
        everyStartChoice.checked = AutoRunSetting.everyEngineStart;
    }

    /// Scrolls the page so that \a item is in view: what the keyboard has
    /// reached is never below the window's edge.
    function reveal(item: Item) {
        const flickable = scroll.contentItem as Flickable;
        if (item === null || flickable === null) {
            return;
        }
        let onThePage = false;
        for (let at = item.parent; at !== null && !onThePage; at = at.parent) {
            onThePage = at === flickable.contentItem;
        }
        if (!onThePage) {
            return;
        }
        const top = item.mapToItem(flickable.contentItem, 0, 0).y;
        const bottom = top + item.height;
        if (top < flickable.contentY) {
            flickable.contentY = top;
        } else if (bottom > flickable.contentY + flickable.height) {
            flickable.contentY = Math.min(bottom - flickable.height, flickable.contentHeight - flickable.height);
        }
    }

    title: KI18n.i18nc("@title:window", "Settings")
    flags: root.ownTitleBar ? (Qt.Window | Qt.FramelessWindowHint) : Qt.Window

    width: WindowState.windowSize(root.stateName).width > 0
        ? WindowState.windowSize(root.stateName).width : Kirigami.Units.gridUnit * 30
    height: WindowState.windowSize(root.stateName).height > 0
        ? WindowState.windowSize(root.stateName).height : Kirigami.Units.gridUnit * 32
    minimumWidth: Kirigami.Units.gridUnit * 20
    minimumHeight: Kirigami.Units.gridUnit * 14

    onClosing: root.saveSize()
    Component.onCompleted: root.showSettings()
    // **The keyboard starts in the window**, at its first choice, each time
    // it is shown.
    onVisibleChanged: {
        if (root.visible) {
            languageBox.forceActiveFocus();
        }
    }
    onActiveFocusItemChanged: root.reveal(root.activeFocusItem)

    Connections {
        target: LanguageSetting

        function onChosenChanged() {
            root.showSettings();
        }
    }

    Connections {
        target: Appearance

        function onModeChanged() {
            root.showSettings();
        }
    }

    Connections {
        target: AutoRunSetting

        function onEveryEngineStartChanged() {
            root.showSettings();
        }
    }

    // **Only the title**, in a title bar of our own: KWin's says it while
    // following the system, so there is no bar at all then.
    menuBar: WindowTitleBar {
        id: titleBar

        visible: root.ownTitleBar
        titleBar: root.ownTitleBar
        frameRadius: chrome.frameRadius
        maximized: chrome.edgeToEdge
        onWindowMenuRequested: chrome.showWindowMenu()

        QQC2.Label {
            text: root.title
            color: AppColors.text
            font: AppFonts.strong
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }

    /// **One part of the page**: a heading, and what is under it. A setting
    /// still to come is a section more.
    component Section: ColumnLayout {
        id: section

        required property string title
        /// What is under the heading.
        default property alias content: body.data

        Layout.fillWidth: true
        Layout.leftMargin: AppMetrics.panelPadding
        Layout.rightMargin: AppMetrics.panelPadding
        spacing: AppMetrics.mediumSpacing
        Accessible.role: Accessible.Grouping
        Accessible.name: section.title

        QQC2.Label {
            Layout.fillWidth: true
            text: section.title
            color: AppMetrics.followsSystem ? AppColors.text : AppColors.muted
            font: AppMetrics.followsSystem ? AppFonts.strong : AppFonts.libraryHeading
            wrapMode: Text.Wrap
            Accessible.role: Accessible.Heading
            Accessible.name: section.title
        }

        ColumnLayout {
            id: body

            Layout.fillWidth: true
            spacing: AppMetrics.mediumSpacing
        }
    }

    /// **A choice of several**, its words wrapping onto the next line rather
    /// than eliding: a translation can be half as long again.
    component Choice: QQC2.RadioButton {
        id: choice

        Layout.fillWidth: true

        contentItem: QQC2.Label {
            leftPadding: choice.indicator && !choice.mirrored ? choice.indicator.width + choice.spacing : 0
            rightPadding: choice.indicator && choice.mirrored ? choice.indicator.width + choice.spacing : 0
            text: choice.text
            font: choice.font
            color: AppColors.text
            wrapMode: Text.Wrap
            verticalAlignment: Text.AlignVCenter
        }
    }

    /// What a choice does, under it and in line with its words.
    component ChoiceDescription: QQC2.Label {
        required property QQC2.RadioButton choice

        Layout.fillWidth: true
        Layout.leftMargin: choice.indicator && !choice.mirrored ? choice.indicator.width + choice.spacing : 0
        Layout.rightMargin: choice.indicator && choice.mirrored ? choice.indicator.width + choice.spacing : 0
        color: AppColors.muted
        font: AppFonts.small
        wrapMode: Text.Wrap
    }

    /// Words a section says under its choices.
    component Help: QQC2.Label {
        Layout.fillWidth: true
        color: AppColors.muted
        font: AppFonts.small
        wrapMode: Text.Wrap
    }

    /// **What a choice waits for, and the button that ends the wait**:
    /// shown while the window runs with something else than was chosen.
    component RestartRow: RowLayout {
        id: row

        required property string text
        /// The button, for the tests.
        readonly property alias button: restartButton

        Layout.fillWidth: true
        spacing: AppMetrics.largeSpacing

        InfoLine {
            text: row.text
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
        }

        QQC2.Button {
            id: restartButton

            text: KI18n.i18nc("@action:button start Reverblabr again", "Restart Now")
            icon.name: Appearance.followsSystem ? "view-refresh" : ""
            Accessible.description: row.text
            Layout.alignment: Qt.AlignVCenter
            onClicked: root.restartRequested()
        }
    }

    QQC2.ScrollView {
        id: scroll

        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: scroll.availableWidth
            spacing: AppMetrics.wideSpacing

            // **A restart that could not start the copy** is said at the top:
            // this window goes on, and the person has to do it by hand.
            Kirigami.InlineMessage {
                id: restartFailedMessage

                Layout.fillWidth: true
                Layout.topMargin: AppMetrics.panelPadding
                Layout.leftMargin: AppMetrics.panelPadding
                Layout.rightMargin: AppMetrics.panelPadding
                type: Kirigami.MessageType.Error
                visible: Restart.failed
                text: KI18n.i18nc("@info",
                                  "Reverblabr could not be started again. Quit it and start it yourself.")
            }

            // --- the language ---
            Section {
                id: languageSection

                Layout.topMargin: restartFailedMessage.visible ? 0 : AppMetrics.panelPadding
                title: KI18n.i18nc("@title:group the language Reverblabr speaks", "Language")

                // **The desktop's language first**, said by name, so that
                // somebody can see what following it gives; then English,
                // then every language there is a catalog for, each in its
                // own words.
                QQC2.ComboBox {
                    id: languageBox

                    model: [{
                        code: "",
                        name: KI18n.i18nc("@item:inlistbox the language Reverblabr speaks; %1 is the desktop's language, named in that language",
                                          "Follow System (%1)", LanguageSetting.systemLanguageName)
                    }].concat(LanguageSetting.languages)
                    textRole: "name"
                    valueRole: "code"
                    implicitContentWidthPolicy: QQC2.ComboBox.WidestText
                    Layout.maximumWidth: languageSection.width
                    Accessible.name: languageSection.title
                    onActivated: index => LanguageSetting.chosen = languageBox.valueAt(index)
                    // A new list keeps showing the choice.
                    onModelChanged: Qt.callLater(root.showSettings)
                }

                // **What it changes, and what it does not**: the parts the
                // desktop shows, or that a terminal runs, are in the
                // desktop's language whatever is chosen here.
                Help {
                    id: languageHelp

                    text: KI18n.i18nc("@info",
                                      "The language of this window, of its notifications and of what KRunner shows. The panel widget, reverblabr-ctl, and what the launcher, System Settings and the software centre say about Reverblabr follow your desktop's language.")
                }

                RestartRow {
                    id: languageRestart

                    visible: LanguageSetting.restartNeeded
                    text: root.restartStopsSessions
                        ? KI18n.i18nc("@info",
                                      "Reverblabr changes to this language when it starts again. Restarting stops the sessions running in this window.")
                        : KI18n.i18nc("@info", "Reverblabr changes to this language when it starts again.")
                }
            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            // --- the appearance: the Settings menu's three ---
            Section {
                id: appearanceSection

                title: KI18n.i18nc("@title:group", "Appearance")

                QQC2.ButtonGroup {
                    id: appearanceGroup
                }

                Choice {
                    id: darkChoice

                    text: KI18n.i18nc("@option:radio an appearance", "Reverblabr Dark")
                    QQC2.ButtonGroup.group: appearanceGroup
                    onToggled: if (darkChoice.checked) {
                        Appearance.mode = Appearance.ReverblabrDark;
                    }
                }

                Choice {
                    id: lightChoice

                    text: KI18n.i18nc("@option:radio an appearance", "Reverblabr Light")
                    QQC2.ButtonGroup.group: appearanceGroup
                    onToggled: if (lightChoice.checked) {
                        Appearance.mode = Appearance.ReverblabrLight;
                    }
                }

                Choice {
                    id: systemChoice

                    text: KI18n.i18nc("@option:radio an appearance: the desktop's colours and fonts", "Follow System")
                    QQC2.ButtonGroup.group: appearanceGroup
                    onToggled: if (systemChoice.checked) {
                        Appearance.mode = Appearance.FollowSystem;
                    }
                }

                Help {
                    text: KI18n.i18nc("@info",
                                      "Follow System takes the colours, fonts and title bar of your desktop. Colours and fonts change at once, the controls and the title bar when Reverblabr starts again.")
                }

                RestartRow {
                    id: appearanceRestart

                    visible: root.styleRestartNeeded
                    text: root.restartStopsSessions
                        ? KI18n.i18nc("@info",
                                      "The controls and the title bar change to this appearance when Reverblabr starts again. Restarting stops the sessions running in this window.")
                        : KI18n.i18nc("@info",
                                      "The controls and the title bar change to this appearance when Reverblabr starts again.")
                }
            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            // --- when Auto Run runs (ADR-0023): the Settings menu's two ---
            Section {
                id: autoRunSection

                Layout.bottomMargin: AppMetrics.panelPadding
                title: KI18n.i18nc("@title:group when the sessions set to Auto Run start", "Auto Run")

                Help {
                    text: KI18n.i18nc("@info",
                                      "When the background engine starts the sessions that are set to Auto Run in their Info tab. The same for every session.")
                }

                QQC2.ButtonGroup {
                    id: autoRunGroup
                }

                Choice {
                    id: onceALoginChoice

                    text: KI18n.i18nc("@option:radio when Auto Run sessions start; the default",
                                      "Once per Login: Stopped Sessions Stay Stopped")
                    QQC2.ButtonGroup.group: autoRunGroup
                    Accessible.description: onceALoginDescription.text
                    onToggled: if (onceALoginChoice.checked) {
                        AutoRunSetting.everyEngineStart = false;
                    }
                }

                ChoiceDescription {
                    id: onceALoginDescription

                    choice: onceALoginChoice
                    text: KI18n.i18nc("@info what the Auto Run choice above does",
                                      "The first time the engine starts after you log in, and again after a crash. A session you stop stays stopped until you log in again.")
                }

                Choice {
                    id: everyStartChoice

                    text: KI18n.i18nc("@option:radio when Auto Run sessions start",
                                      "Every Time the Background Engine Starts")
                    QQC2.ButtonGroup.group: autoRunGroup
                    Accessible.description: everyStartDescription.text
                    onToggled: if (everyStartChoice.checked) {
                        AutoRunSetting.everyEngineStart = true;
                    }
                }

                ChoiceDescription {
                    id: everyStartDescription

                    choice: everyStartChoice
                    text: KI18n.i18nc("@info what the Auto Run choice above does",
                                      "Whatever starts the engine, such as opening this window or typing “rb” in KRunner, starts them, even after you stopped them. The engine stops a minute after nothing uses it.")
                }

                // **Where nothing would run them**: the choice is kept for
                // later, as each session keeps its own setting.
                Help {
                    id: autoRunUnavailable

                    visible: AppShell.sandboxed || AppShell.appImage || !AppShell.engineInBackground
                    text: AppShell.sandboxed
                        ? KI18n.i18nc("@info", "Reverblabr from a Flatpak can't run sessions when you log in.")
                        : AppShell.appImage
                        ? KI18n.i18nc("@info", "Reverblabr from an AppImage can't run sessions when you log in.")
                        : KI18n.i18nc("@info", "Needs Reverblabr's background engine, which isn't installed here.")
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
