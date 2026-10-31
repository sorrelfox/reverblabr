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
import org.kde.ki18n
import org.kde.kirigami as Kirigami

// What is said once before the first Hearing block, and can be read again
// from the Help menu. The same for everyone, and it looks at nothing.
//
// **Keyboard focus starts on the words, not on a button**: a screen reader
// then reads them as the dialog opens, and Return there does nothing, so
// nobody goes past them by accident. Each paragraph takes focus in turn, and
// Tab goes on to the buttons.
AppPromptDialog {
    id: dialog

    /// Whether this asks before adding, rather than being read again.
    property bool asking: false
    property var whenAccepted: null
    /// Kirigami's scrolling view, whose own type it does not publish.
    readonly property var scroller: contentItem

    /// The four paragraphs, each with its lead in bold.
    readonly property list<string> paragraphs: [
        KI18n.i18nc("@info notice before the first Hearing block; keep the bold tags", "<b>Keep it comfortable.</b> Sound should never be uncomfortable or painful. If it is, turn it down or take your headphones off. Start with the volume low and raise it slowly."),
        KI18n.i18nc("@info notice before the first Hearing block; keep the bold tags", "<b>These limits do not protect your ears.</b> They work on numbers inside the computer. How loud that is at your ear depends on your volume, your sound card and your headphones, which Reverblabr can't know. Long hours of loud sound can harm hearing with no warning."),
        KI18n.i18nc("@info notice before the first Hearing block; keep the bold tags", "<b>Some changes need a doctor, not a setting.</b> See a doctor straight away if your hearing changes suddenly or gets much worse within days, in one ear or both. Time matters. If this comes with a weak or numb face or a spinning feeling, or after a blow to the head, call for emergency help. See a doctor soon if one ear is worse than the other, your hearing comes and goes, you hear ringing in one ear or a beat in time with your pulse, you feel dizzy, or an ear hurts, feels blocked, looks injured or leaks blood, pus or fluid. Ringing that keeps you awake or gets you down is worth a visit too. Don't turn one side up to make up for a change: a setting can hide it."),
        KI18n.i18nc("@info notice before the first Hearing block; keep the bold tags", "<b>Before you add gain.</b> Don't add these blocks on top of headphones or earpieces that already shape the sound for you: the gains add up. That holds on loudspeakers too, if you wear such earpieces while you listen. Don't set them up for a child. Don't rely on a session for alarms or calls you must not miss: if it stops, the sound can stop or change. Don't wear closed headphones where you need to hear traffic.")
    ]

    /// What a screen reader is given: the words without their tags.
    function plainWords(index: int): string {
        return dialog.paragraphs[index].replace(/<[^>]*>/g, "");
    }

    /// Opens before an add, which `accepted` then carries out. Going past
    /// the notice is what is remembered; anything else adds nothing.
    function ask(accepted: var) {
        dialog.whenAccepted = accepted;
        dialog.asking = true;
        dialog.open();
    }

    /// Opens the same words with nothing waiting on them.
    function read() {
        dialog.whenAccepted = null;
        dialog.asking = false;
        dialog.open();
    }

    function goOn() {
        const accepted = dialog.whenAccepted;
        dialog.whenAccepted = null;
        FirstUseNotice.hearingAccepted = true;
        dialog.close();
        if (accepted) {
            accepted();
        }
    }

    /// Scrolls the dialog's text, whose view is Kirigami's own.
    function scrollBy(distance: real) {
        const view = dialog.scroller.contentItem;
        view.contentY = Math.max(0, Math.min(view.contentHeight - view.height, view.contentY + distance));
    }

    function bringIntoView(item: Item) {
        const view = dialog.scroller.contentItem;
        const top = item.mapToItem(view.contentItem, 0, 0).y;
        view.contentY = Math.max(0, Math.min(view.contentHeight - view.height, top - Kirigami.Units.gridUnit));
    }

    parent: QQC2.Overlay.overlay
    // Room for Kirigami's opening slide: a dialog that reaches the window's
    // edge while it moves is pushed back, and moved again, without end.
    maximumHeight: (parent?.height ?? 0) - Kirigami.Units.gridUnit * 6
    title: KI18n.i18nc("@title:window", "Before You Add a Hearing Block")
    preferredWidth: Kirigami.Units.gridUnit * 34
    standardButtons: dialog.asking ? Kirigami.Dialog.Cancel : Kirigami.Dialog.Close
    customFooterActions: dialog.asking ? [goOnAction] : []

    onOpened: words.itemAt(0).forceActiveFocus()
    onClosed: dialog.whenAccepted = null

    Kirigami.Action {
        id: goOnAction

        text: KI18n.i18nc("@action:button add the block after reading the notice", "Add Block")
        icon.name: "list-add"
        onTriggered: dialog.goOn()
    }

    Repeater {
        id: words

        model: dialog.paragraphs.length

        QQC2.Label {
            id: paragraph

            required property int index

            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            text: dialog.paragraphs[index]
            textFormat: Text.StyledText
            wrapMode: Text.Wrap
            activeFocusOnTab: true

            Accessible.role: Accessible.StaticText
            Accessible.name: dialog.plainWords(index)
            Accessible.focusable: true

            onActiveFocusChanged: {
                if (activeFocus) {
                    dialog.bringIntoView(paragraph);
                }
            }
            Keys.onUpPressed: dialog.scrollBy(-Kirigami.Units.gridUnit * 2)
            Keys.onDownPressed: dialog.scrollBy(Kirigami.Units.gridUnit * 2)
            Keys.onPressed: event => {
                if (event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown) {
                    const page = dialog.scroller.contentItem.height;
                    dialog.scrollBy(event.key === Qt.Key_PageUp ? -page : page);
                    event.accepted = true;
                }
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: -2
                visible: parent.activeFocus
                color: "transparent"
                border.color: AppColors.accent
                border.width: 1
            }
        }
    }
}
