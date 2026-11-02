// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.kirigami as Kirigami

// A dialog of ours: Kirigami's, with the room round its title, its
// content and its buttons taken from the tokens.
//
// **Why a type of its own.** Kirigami measures a dialog from its spacing
// units, which the whole window shares: raising them for the dialogs would
// move the title bar and every panel with them. So the dialogs, and only
// they, are measured here. While following the system nothing is set and
// the dialog is Kirigami's own.
//
// **A list goes to the edges**, as Kirigami draws one: `listContent` keeps
// the content's sides bare for a dialog that is one scrolling list.
Kirigami.Dialog {
    id: dialog

    // **The window it opens over takes the keyboard.** A panel outside the
    // main window opens its dialogs on that window (ADR-0032), with the
    // keyboard still in the panel's own.
    onAboutToShow: dialog.parent?.Window.window?.requestActivate()

    /// Whether the content is one list that runs to the dialog's sides.
    property bool listContent: false

    // **Kept inside the window, which also settles it.** Kirigami slides a
    // dialog in by moving `y` with its opacity, and a popup with no margins to
    // keep to places itself again at every step: a binding loop on `y`, each
    // time a dialog opens over the window. With margins the slide stays.
    margins: 0

    property list<Binding> tokenPadding: [
        Binding {
            when: !AppMetrics.followsSystem
            target: dialog.header
            property: "padding"
            value: AppMetrics.dialogPadding
            restoreMode: Binding.RestoreBindingOrValue
        },
        Binding {
            // Kirigami keeps a sliver of footer with nothing in it, for the
            // rounded corner; that stays as it is.
            when: !AppMetrics.followsSystem && !dialog.isSliver(dialog.footer)
            target: dialog.footer
            property: "padding"
            value: AppMetrics.dialogPadding
            restoreMode: Binding.RestoreBindingOrValue
        },
        Binding {
            when: !AppMetrics.followsSystem && !dialog.listContent
            target: dialog.contentItem
            property: "horizontalPadding"
            value: AppMetrics.dialogPadding
            restoreMode: Binding.RestoreBindingOrValue
        }
    ]

    /// Whether Kirigami's footer is only the sliver it keeps when there
    /// are no buttons. Its type is Kirigami's own, so it is asked by name.
    function isSliver(footer: var): bool {
        return footer?.bufferMode === true;
    }
}
