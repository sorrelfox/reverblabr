// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtTest
import org.kde.kirigami as Kirigami
import io.github.reverblabr

// What pops up over the window, and what frames what is in it, under the
// Reverblabr control style ([ADR-0012]): menus, tooltips, popups,
// panes and dialogs drawn from the tokens, Kirigami in the design's colours
// and corner, the window's type reaching every label, and the prototype's
// own glyphs where it draws its own.
//
// **Most of this only means something under that style**, which the suite's
// second run (`qml.mainwindow.reverblabr-style`) uses; under the first,
// those tests say so and skip. That run is also a dark window on a light
// desktop, where the platform can reach it (tests/qml/CMakeLists.txt): the
// case in which a menu drawn with the desktop's palette opened light.
TestCase {
    id: testCase

    name: "PopupStyle"
    when: windowShown

    QQC2.Action {
        id: openAction

        text: "Open…"
        shortcut: "Ctrl+O"
    }

    QQC2.Action {
        id: zoomAction

        text: "Zoom In"
        shortcut: StandardKey.ZoomIn
    }

    QQC2.ApplicationWindow {
        id: window

        width: 640
        height: 480
        visible: true

        ColumnLayout {
            x: 20
            y: 40

            QQC2.Button {
                id: hinted

                text: "Hinted"
            }

            QQC2.Label {
                id: label

                text: "A label"
            }

            Kirigami.Heading {
                id: heading

                text: "A heading"
                level: 4
            }

            QQC2.Pane {
                id: pane

                QQC2.Label {
                    text: "100 %"
                }
            }

            Item {
                id: viewItem

                implicitWidth: 10
                implicitHeight: 10
                Kirigami.Theme.inherit: false
                Kirigami.Theme.colorSet: Kirigami.Theme.View
            }
        }

        QQC2.Menu {
            id: menu

            QQC2.MenuItem {
                id: plainRow

                text: "Plain"
            }

            QQC2.MenuItem {
                id: keyedRow

                action: openAction
            }

            QQC2.MenuItem {
                id: standardRow

                action: zoomAction
            }

            QQC2.MenuSeparator {
                id: separator
            }

            QQC2.MenuItem {
                id: chosenRow

                text: "Chosen"
                checkable: true
                checked: true
            }

            QQC2.MenuItem {
                id: disabledRow

                text: "Not now"
                enabled: false
            }

            DestructiveMenuItem {
                id: destructiveRow

                text: "Delete"
            }

            QQC2.Menu {
                id: subMenu

                title: "More"

                QQC2.MenuItem {
                    text: "Inside"
                }
            }
        }

        QQC2.Popup {
            id: popup

            width: 120
            height: 80
        }

        Kirigami.Dialog {
            id: dialog

            title: "A dialog"
            standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel

            QQC2.Label {
                id: dialogLabel

                text: "Its words"
            }
        }

        AppDialog {
            id: tokenDialog

            title: "Ours"
            standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel

            QQC2.Label {
                id: tokenLabel

                text: "Its words"
            }
        }

        AppDialog {
            id: listDialog

            title: "A list"
            listContent: true
            preferredHeight: 200
            standardButtons: Kirigami.Dialog.Cancel

            ListView {
                model: 3
                delegate: QQC2.ItemDelegate {
                    required property int index

                    text: "Row " + index
                }
            }
        }

        AppPromptDialog {
            id: tokenPrompt

            title: "Delete it?"
            subtitle: "This cannot be undone."
            standardButtons: Kirigami.Dialog.Cancel
        }
    }

    // **A plain window**, not the application's: a menu here is handed the
    // desktop's palette for menus with nothing of the window's to override
    // it, which is how a menu that drew with the palette opened light and
    // unreadable in a dark appearance.
    Window {
        id: plainWindow

        width: 200
        height: 160
        visible: true

        QQC2.Menu {
            id: plainWindowMenu

            QQC2.MenuItem {
                id: plainWindowRow

                text: "Row"
            }
        }
    }

    Component {
        id: glyphImage

        Image {}
    }

    Component {
        id: mainWindow

        Main {}
    }

    function init() {
        Appearance.mode = Appearance.ReverblabrDark;
    }

    function cleanup() {
        menu.close();
        plainWindowMenu.close();
        popup.close();
        dialog.close();
        tokenDialog.close();
        listDialog.close();
        tokenPrompt.close();
        hinted.QQC2.ToolTip.hide();
        Appearance.mode = SuiteAppearance;
    }

    function requireOwnStyle() {
        if (!Appearance.ownStyle) {
            skip("only under the Reverblabr control style");
        }
    }

    function openMenu() {
        menu.open();
        tryVerify(() => menu.opened, 1000, "the menu did not open");
    }

    // **The window's type is every label's**: a label names no font
    // of its own, and inherits the design's body text from the window
    // rather than the platform's default for labels — also after the
    // appearance has been changed while the window is open, which is when
    // the platform's default had stayed behind.
    function test_aLabelIsSetInTheDesignsBodyText() {
        requireOwnStyle();
        Appearance.mode = Appearance.FollowSystem;
        Appearance.mode = Appearance.ReverblabrDark;
        compare(window.color, AppColors.window);
        tryCompare(label.font, "pointSize", AppFonts.body.pointSize);
        compare(label.font.family, AppFonts.body.family);
        compare(label.color, AppColors.text);
        compare(heading.font.family, AppFonts.body.family);
    }

    // The settings panel's card: the surface in a border, its corners and
    // its resting shadow, rows inside a small margin.
    function test_aMenuIsThePrototypesPopover() {
        requireOwnStyle();
        openMenu();
        const background = menu.background;
        compare(background.color, AppColors.surface);
        compare(background.border.color, AppColors.border);
        compare(background.border.width, 1);
        compare(background.radius, AppMetrics.panelRadius);
        compare(background.shadow.color, AppColors.shadow);
        compare(background.shadow.size, menu.popupType === QQC2.Popup.Item ? AppMetrics.shadowSize : 0);
        compare(menu.topPadding, AppMetrics.menuPadding);
        verify(menu.width >= AppMetrics.menuMinimumWidth);
    }

    function test_aMenuRowIsTheBodyTextRaisedUnderThePointer() {
        requireOwnStyle();
        openMenu();
        compare(plainRow.implicitHeight, AppMetrics.mediumControlHeight);
        compare(plainRow.font.pointSize, AppFonts.body.pointSize);
        compare(plainRow.leftPadding, AppMetrics.rowSpacing);
        compare(plainRow.background.color.a, 0);
        compare(plainRow.background.radius, AppMetrics.smallRadius);

        menu.currentIndex = 0;
        tryVerify(() => plainRow.highlighted, 1000, "the first row is not the current one");
        compare(plainRow.background.color, AppColors.surface2);

        compare(disabledRow.opacity, AppMetrics.disabledOpacity);
    }

    // "Open… Ctrl+O": the key at the far end, muted and smaller, and a
    // standard key with several bindings written as its first.
    function test_aMenuRowSaysItsKeyAtTheFarEnd() {
        requireOwnStyle();
        openMenu();
        compare(plainRow.shortcutText, "");
        compare(keyedRow.shortcutText, Appearance.shortcutText("Ctrl+O"));
        verify(keyedRow.shortcutText !== "");
        verify(standardRow.shortcutText !== "", "a standard key has words too");

        const key = keyedRow.contentItem.children[1];
        verify(key.visible);
        compare(key.text, keyedRow.shortcutText);
        compare(key.color, AppColors.muted);
        compare(key.font.pointSize, AppFonts.small.pointSize);
        // At the far end: its right edge is the row's content edge.
        compare(key.x + key.width, keyedRow.contentItem.width);
        const words = keyedRow.contentItem.children[0];
        verify(words.x + words.implicitWidth <= key.x, "the words run into the key");
    }

    // The prototype's tick in front of a chosen row; its chevron on a row
    // that opens another menu; a rule in the border colour between groups.
    function test_aMenuMarksChoicesAndSubmenusWithTheDesignsGlyphs() {
        requireOwnStyle();
        openMenu();
        verify(chosenRow.indicator.visible);
        compare(chosenRow.indicator.source, AppGlyphs.check);
        compare(chosenRow.indicator.color, AppColors.text);
        compare(chosenRow.indicator.width, AppMetrics.menuGlyphSize);

        const subRow = menu.itemAt(menu.count - 1);
        verify(subRow.subMenu, "the last row opens the submenu");
        verify(subRow.arrow.visible);
        compare(subRow.arrow.source, AppGlyphs.submenu);
        compare(subRow.arrow.color, AppColors.muted);

        compare(separator.contentItem.color, AppColors.border);
        compare(separator.contentItem.height, 1);
    }

    // **The regression**: a menu in a window of its own kind is still the
    // design's, whatever palette the platform hands it.
    function test_aMenuInAPlainWindowIsStillTheDesigns() {
        requireOwnStyle();
        plainWindowMenu.open();
        tryVerify(() => plainWindowMenu.opened, 1000, "the menu did not open");
        compare(plainWindowMenu.background.color, AppColors.surface);
        compare(plainWindowRow.contentItem.children[0].color, AppColors.text);
    }

    // A small piece of the popover: the raised surface in a border, the
    // caption text, words that wrap rather than run across the window.
    function test_aToolTipIsASmallPopover() {
        requireOwnStyle();
        hinted.QQC2.ToolTip.show("A short hint");
        const tip = hinted.QQC2.ToolTip.toolTip;
        tryVerify(() => tip.opened, 1000, "the tooltip did not open");
        compare(tip.background.color, AppColors.surface2);
        compare(tip.background.border.color, AppColors.border);
        compare(tip.background.radius, AppMetrics.controlRadius);
        compare(tip.contentItem.color, AppColors.text);
        compare(tip.font.pointSize, AppFonts.small.pointSize);
        compare(tip.leftPadding, AppMetrics.largeSpacing);

        hinted.QQC2.ToolTip.show("A hint that has a great deal more to say than fits on one line of a tooltip, "
                                 + "because it is the help that used to sit on a panel and now sits here.");
        tryVerify(() => tip.width <= AppMetrics.toolTipMaximumWidth, 1000, "the tooltip ran on");
        verify(tip.contentItem.lineCount > 1, "the words did not wrap");
    }

    function test_aPopupIsThePanelsCard() {
        requireOwnStyle();
        popup.open();
        tryVerify(() => popup.opened, 1000, "the popup did not open");
        compare(popup.background.color, AppColors.surface);
        compare(popup.background.border.color, AppColors.border);
        compare(popup.background.radius, AppMetrics.panelRadius);
        compare(popup.padding, AppMetrics.panelPadding);
    }

    // The prototype's one pane: the zoom control's framed surface.
    function test_aPaneIsTheZoomControlsFrame() {
        requireOwnStyle();
        compare(pane.background.color, AppColors.surface);
        compare(pane.background.border.color, AppColors.border);
        compare(pane.background.radius, AppMetrics.controlRadius);
        compare(pane.padding, AppMetrics.hairSpacing);
    }

    // **Kirigami's dialogs are the design's popover too**: their card on the
    // surface with its corner, their words in the design's type, and the
    // style's buttons in a row at the end.
    function test_aKirigamiDialogIsTheDesigns() {
        requireOwnStyle();
        compare(Kirigami.Units.cornerRadius, AppMetrics.panelRadius);
        compare(viewItem.Kirigami.Theme.backgroundColor, AppColors.surface);

        dialog.open();
        tryVerify(() => dialog.opened, 2000, "the dialog did not open");
        compare(dialog.background.color, AppColors.surface);
        compare(dialog.background.radius, AppMetrics.panelRadius);
        compare(dialogLabel.color, AppColors.text);
        compare(dialogLabel.font.family, AppFonts.body.family);

        const ok = dialog.standardButton(Kirigami.Dialog.Ok);
        const cancel = dialog.standardButton(Kirigami.Dialog.Cancel);
        verify(ok && cancel, "the dialog has its two buttons");
        compare(cancel.background.color, AppColors.surface2);
        compare(cancel.background.border.color, AppColors.controlBorder);
        compare(cancel.font.pointSize, AppFonts.button.pointSize);
        verify(Math.abs(ok.x - cancel.x) >= ok.width, "the buttons sit side by side");
        compare(ok.y, cancel.y);
    }

    // **A dialog of ours takes its room from the tokens**: round its
    // title, its content and its buttons, the settings panel's side
    // padding, where Kirigami's own spacing had left the words against the
    // dialog's edge. A dialog that is one list keeps its sides bare, and a
    // question gets the same room round its words.
    function test_ourDialogsTakeTheirRoomFromTheTokens() {
        requireOwnStyle();
        tokenDialog.open();
        tryVerify(() => tokenDialog.opened, 2000, "the dialog did not open");
        compare(tokenDialog.header.padding, AppMetrics.dialogPadding);
        compare(tokenDialog.footer.padding, AppMetrics.dialogPadding);
        compare(tokenDialog.contentItem.leftPadding, AppMetrics.dialogPadding);
        compare(tokenDialog.contentItem.rightPadding, AppMetrics.dialogPadding);
        // Where the words are, not only what the numbers say.
        const words = tokenLabel.mapToItem(tokenDialog.background, 0, 0);
        verify(words.x >= AppMetrics.dialogPadding,
               "the words stand " + words.x + " px from the dialog's edge, not " + AppMetrics.dialogPadding);

        listDialog.open();
        tryVerify(() => listDialog.opened, 2000, "the list dialog did not open");
        compare(listDialog.header.padding, AppMetrics.dialogPadding);
        compare(listDialog.contentItem.leftPadding, dialog.contentItem.leftPadding,
                "a list runs to the sides as in Kirigami's own dialog");

        tokenPrompt.open();
        tryVerify(() => tokenPrompt.opened, 2000, "the question did not open");
        compare(tokenPrompt.contentPadding, AppMetrics.dialogPadding);
        compare(tokenPrompt.footer.padding, AppMetrics.dialogPadding);
    }

    // **Following the system, a dialog is Kirigami's own**: the same room a
    // plain Kirigami dialog has, in either control style.
    function test_ourDialogsAreKirigamisOwnWhileFollowingTheSystem() {
        Appearance.mode = Appearance.FollowSystem;
        dialog.open();
        tokenDialog.open();
        tryVerify(() => dialog.opened && tokenDialog.opened, 2000, "the dialogs did not open");
        tryCompare(tokenDialog.header, "padding", dialog.header.padding);
        compare(tokenDialog.header.padding, Kirigami.Units.largeSpacing);
        compare(tokenDialog.footer.padding, dialog.footer.padding);
        compare(tokenDialog.contentItem.leftPadding, dialog.contentItem.leftPadding);
        compare(tokenPrompt.contentPadding, Kirigami.Units.largeSpacing);
    }

    // **A row that destroys something is in the danger colour**, as
    // the prototype writes its one such row; the rows round it are not.
    // Under the desktop's style the row hands its colour over through the
    // theme, which is what that style writes a row in.
    function test_aRowThatDestroysSomethingIsInTheDangerColour() {
        // A row's theme is settled once its menu is open and the row is in
        // the window; before that it is Kirigami's default.
        for (const mode of [Appearance.ReverblabrDark, Appearance.ReverblabrLight, Appearance.FollowSystem]) {
            Appearance.mode = mode;
            openMenu();
            tryCompare(destructiveRow.Kirigami.Theme, "textColor", AppColors.danger);
            if (Appearance.ownStyle && mode !== Appearance.FollowSystem) {
                compare(destructiveRow.contentItem.children[0].color, AppColors.dangerText);
                compare(plainRow.contentItem.children[0].color, AppColors.text);
            }
            menu.close();
            tryVerify(() => !menu.visible, 1000, "the menu did not close");
        }
    }

    // **Deleting a session is that row**, in both of the window's menus:
    // the one behind its menu button and the classic menu bar.
    function test_deletingASessionIsARowThatDestroysSomething() {
        const window = createTemporaryObject(mainWindow, testCase);
        tryVerify(() => AppShell.hasSession, 2000, "the window came up with a session");
        for (const menus of [window.mainMenu, window.menus]) {
            const session = menus.menuAt(0);
            let found = 0;
            for (let i = 0; i < session.count; ++i) {
                const row = session.itemAt(i);
                if (row?.action === window.deleteSessionAction) {
                    verify(row instanceof DestructiveMenuItem, "Delete Session is an ordinary row");
                    ++found;
                } else if (row?.action) {
                    verify(!(row instanceof DestructiveMenuItem), row.text + " is marked as destroying something");
                }
            }
            compare(found, 1, "the Session menu has one Delete Session row");
        }
    }

    function test_kirigamisCornerFollowsBothPalettes() {
        requireOwnStyle();
        for (const mode of [Appearance.ReverblabrDark, Appearance.ReverblabrLight]) {
            Appearance.mode = mode;
            tryCompare(viewItem.Kirigami.Theme, "backgroundColor", AppColors.surface);
            compare(Kirigami.Units.cornerRadius, AppMetrics.panelRadius);
        }
    }

    // **Every glyph the prototype draws of its own is there**, in both
    // styles: the title bar will need the last ones.
    function test_everyGlyphLoads() {
        const names = ["add", "run", "stop", "check", "submenu", "sun", "moon", "minimize", "maximize", "restore",
                       "close"];
        for (const name of names) {
            const image = createTemporaryObject(glyphImage, testCase, {
                                                    "source": AppGlyphs[name]
                                                });
            tryCompare(image, "status", Image.Ready, 1000, name + " did not load");
            verify(image.implicitWidth > 0, name + " is empty");
        }
    }

    // **The header draws the prototype's own "+" and Run**, and the icon
    // theme's when following the system.
    function test_theHeaderUsesTheDesignsGlyphs() {
        const main = createTemporaryObject(mainWindow, testCase);
        tryVerify(() => main.sessionTabs.footerItem !== null, 2000, "the tabs have no + button");
        const add = main.sessionTabs.footerItem;
        const runGlyph = main.toolBar.runButton.contentItem.children[0];

        Appearance.mode = Appearance.ReverblabrDark;
        compare(add.icon.name, "");
        compare(add.icon.source, AppGlyphs.add);
        compare(runGlyph.source, AppGlyphs.run);

        Appearance.mode = Appearance.FollowSystem;
        compare(add.icon.name, "list-add");
        compare(String(runGlyph.source), main.toolBar.runButton.icon.name);
    }
}
