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
import org.kde.ki18n

// The session tabs, then what the session is doing, how long it has been
// doing it, and Run/Stop.
//
// **Two places for one row** (ADR-0012). With *Follow system* KWin draws the
// title bar and this is the row under it, so the window keeps the person's
// decoration theme and button layout. In the Reverblabr appearances the
// window has no frame and this row *is* its title bar, as the
// prototype draws it: after Run come the theme button, the menu button while
// the menu bar is hidden, the Virtual Devices button, and the
// window's own buttons. What makes it a title bar is WindowTitleBar's.
WindowTitleBar {
    id: toolBar

    /// What the menu button opens: every menu, while the menu bar is hidden.
    property QQC2.Menu mainMenu: null
    /// Whether the menu button shows: not while the menu bar is up, which
    /// would put every menu in the window twice.
    property bool menuButtonShown: false
    readonly property bool menuButtonWanted: toolBar.menuButtonShown && toolBar.mainMenu !== null

    /// The Run/Stop action, so that the button and the menu item are the
    /// same action and cannot disagree about whether it is enabled.
    required property QQC2.Action runAction
    /// What the Virtual Devices button does: the Settings menu's
    /// action, so the two open the one window.
    property QQC2.Action virtualDevicesAction: null

    /// Where the session tabs go; the window puts them there.
    property alias tabs: tabSlot.data

    readonly property string statusText: AppShell.failed
        ? KI18n.i18nc("@info:status the session could not run", "Error")
        : AppShell.running
            ? KI18n.i18nc("@info:status the session is running", "Running")
            : AppShell.busy
                ? KI18n.i18nc("@info:status the session is starting or stopping", "Please wait")
                : KI18n.i18nc("@info:status the session is not running", "Idle")

    // Hours only once there are any, and days only once there are any, so
    // the common case is the short one.
    //
    // **A session can run for days.** Something left recording over a
    // weekend would otherwise show 53:17:04, which is a number nobody reads
    // as two days; so past a day the days are counted separately and the
    // clock starts again, the way a person would say it.
    function formatElapsed(seconds: int): string {
        const days = Math.floor(seconds / 86400);
        const withinDay = seconds % 86400;
        const hours = Math.floor(withinDay / 3600);
        const minutes = Math.floor((withinDay % 3600) / 60);
        const remainder = withinDay % 60;
        const padded = (value) => (value < 10 ? "0" : "") + value;
        const clock = hours > 0 || days > 0 ? hours + ":" + padded(minutes) + ":" + padded(remainder)
                                            : minutes + ":" + padded(remainder);
        return days > 0 ? KI18n.i18ncp("@info:status running time past a day, for example '2d 3:04:05'",
                                       "%1d %2", "%1d %2", days, clock)
                        : clock;
    }

    /// The pieces of the cluster, for the tests.
    readonly property alias runButton: runButton
    readonly property alias clock: elapsed
    readonly property alias status: status
    /// The title bar's own buttons, for the tests; the window's
    /// three and the empty space are WindowTitleBar's.
    readonly property alias themeButton: themeButton
    readonly property alias menuButton: menuButton
    readonly property alias virtualDevicesButton: virtualDevicesButton

    // **The group always shows**: the Virtual Devices button is in it in
    // every appearance — under KWin's title bar too, where the theme
    // button is not, and the menu button only while the menu bar is hidden.
    buttonsShown: true

    // **What is happening, for how long, what to do about it**: the
    // row reads left to right in the order a person asks those questions,
    // and the button — the thing they came to the window to press — ends
    // the row where the eye ends up. Putting it last also keeps it still:
    // the status word is the part that changes width, and it changes on the
    // side the session tabs give way on.
    Item {
        id: tabSlot

        Layout.fillWidth: true
        Layout.fillHeight: true
    }

    RowLayout {
        id: status

        spacing: AppMetrics.followsSystem ? Kirigami.Units.smallSpacing : AppMetrics.largeSpacing

        Rectangle {
            implicitWidth: Kirigami.Units.smallSpacing * 2
            implicitHeight: implicitWidth
            radius: width / 2
            color: AppShell.failed
                ? AppColors.danger
                : AppShell.running
                    ? AppColors.ok
                    : AppShell.busy ? AppColors.warning : AppColors.muted
        }

        QQC2.Label {
            text: toolBar.statusText
            color: AppColors.muted
            font: AppFonts.headerStatus
        }
    }

    // The elapsed time in the fixed-width font: a clock whose digits are
    // different widths is a clock that twitches once a second.
    //
    // **Big enough to read from across a desk, and only bright while it
    // is counting**: a clock standing at zero is not news. And it is as
    // wide as the widest time short of a day from the start, set to the
    // right, so going from 9:59 to 10:00 or from 59:59 to 1:00:00 does
    // not shove the rest of the row about.
    QQC2.Label {
        id: elapsed

        readonly property real widest: widestTime.advanceWidth

        text: toolBar.formatElapsed(AppShell.elapsedSeconds)
        font: AppFonts.timer
        horizontalAlignment: Text.AlignRight
        color: AppShell.running ? AppColors.text : AppColors.muted
        Layout.minimumWidth: elapsed.widest
        Accessible.name: KI18n.i18nc("@info:status", "Running time")
        Accessible.description: elapsed.text

        TextMetrics {
            id: widestTime

            font: elapsed.font
            text: "00:00:00"
        }
    }

    // **The one solid block of colour in the row**, because it is the
    // one thing on it that does anything. Accent to start, the live
    // colour to stop — the same orange as a wire with sound in it — so
    // that "it is running" can be read without reading the label.
    QQC2.Button {
        id: runButton

        readonly property color fill: !runButton.enabled
            ? AppColors.well
            : AppShell.running ? AppColors.live : AppColors.accent
        readonly property color ink: !runButton.enabled
            ? AppColors.muted
            : AppShell.running ? AppColors.inkOnLive : AppColors.textOnAccent

        action: toolBar.runAction
        implicitHeight: AppMetrics.primaryButtonHeight
        leftPadding: AppMetrics.primaryButtonPadding
        rightPadding: AppMetrics.primaryButtonPadding
        // It says it cannot be pressed in its own colours, above; the
        // style's faded look on top of those would lose the label.
        opacity: 1

        background: Rectangle {
            radius: AppMetrics.controlRadius
            color: runButton.down ? Qt.darker(runButton.fill, 1.1)
                 : runButton.hovered ? Qt.lighter(runButton.fill, 1.1)
                 : runButton.fill
            // A focus ring in the text colour: the accent a ring would
            // normally be drawn in is the button's own colour.
            border.width: runButton.visualFocus ? 2 : 0
            border.color: AppColors.text
        }

        contentItem: RowLayout {
            spacing: AppMetrics.buttonSpacing

            Kirigami.Icon {
                // The icon theme's play and stop, or the prototype's own
                // triangle and square.
                source: Appearance.followsSystem ? runButton.icon.name
                                                 : AppShell.running ? AppGlyphs.stop : AppGlyphs.run
                color: runButton.ink
                isMask: true
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: implicitWidth
            }

            QQC2.Label {
                text: runButton.text
                color: runButton.ink
                font: AppFonts.strong
            }
        }
    }

    // --- the title bar's end: the prototype's, in its order ---

    buttons: [
        // **What pressing it would make the window**: the sun on a dark
        // window, the moon on a light one, as the prototype has it.
        WindowButton {
            id: themeButton

            visible: toolBar.titleBar
            glyph: Appearance.dark ? AppGlyphs.sun : AppGlyphs.moon
            muted: true
            text: Appearance.dark
                ? KI18n.i18nc("@action:button switch the window to the light appearance", "Light Appearance")
                : KI18n.i18nc("@action:button switch the window to the dark appearance", "Dark Appearance")
            onClicked: Appearance.mode = Appearance.dark ? Appearance.ReverblabrLight : Appearance.ReverblabrDark
        },

        // **Every menu, behind one button**: the prototype draws no
        // menu bar, and a KDE window whose menu bar is hidden keeps its
        // menus here — with *Follow system* too, once Ctrl+M has hidden
        // the bar. Every shortcut works whether it is open or not.
        WindowButton {
            id: menuButton

            visible: toolBar.menuButtonWanted
            glyph: Appearance.followsSystem ? "application-menu" : AppGlyphs.menu
            muted: true
            text: KI18n.i18nc("@action:button the button that holds every menu", "Menu")
            Accessible.role: Accessible.ButtonMenu
            onClicked: toolBar.mainMenu.popup(menuButton, 0, menuButton.height)
        },

        // **The virtual devices, one press from anywhere**: the
        // outputs apps play into and the microphones they record from, in a
        // window of their own. In every appearance, because routing a
        // player into a device is not a thing to hunt through menus for.
        WindowButton {
            id: virtualDevicesButton

            glyph: Appearance.followsSystem ? "audio-card" : AppGlyphs.virtualDevices
            muted: true
            enabled: toolBar.virtualDevicesAction !== null && toolBar.virtualDevicesAction.enabled
            text: KI18n.i18nc("@action:button open the window that manages virtual audio devices",
                              "Virtual Devices")
            onClicked: toolBar.virtualDevicesAction.trigger()
        }
    ]
}
