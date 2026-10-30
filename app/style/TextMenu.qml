// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic.impl
import QtQuick.Templates as T

// The menu a right click opens on text: Qt's own, with Cut, Copy, Paste
// and the rest in Qt's words, as the Basic style gives it. A field passes
// its `ContextMenu.onRequested` to popup() and holds one of these.
//
// **Made when it is asked for and gone when it closes.** Its entries are
// actions with the window's own keys on them: Undo is Ctrl+Z. A menu kept
// for every field held those keys for the whole window whenever its field
// had something to undo, and two holders of one key is a key that does
// nothing. So Ctrl+Z, pressed on the canvas after typing in the library's
// search, undid neither the session nor the search. The field takes its own
// Ctrl+Z while it is being typed in, menu or not.
//
// **Qt 6.11 and later only.** Qt 6.10 has no such menu, and
// qt6.10/TextMenu.qml stands in for this file there (CMakeLists.txt picks
// one), so that the same fields build on both.
QtObject {
    id: root

    /// The text item the menu cuts from and pastes into.
    property Item editor

    /// The menu while it is open, for a test; null otherwise.
    property T.Menu menu: null

    /// Opens the menu at \a position, in the editor's coordinates.
    function popup(position: point) {
        if (root.menu !== null) {
            return;
        }
        const opened = menuComponent.createObject(root.editor, { editor: root.editor });
        opened.closed.connect(() => {
            if (root.menu === opened) {
                root.menu = null;
            }
            opened.destroy();
        });
        root.menu = opened;
        opened.popup(position);
    }

    readonly property Component menuComponent: Component {
        TextEditingContextMenu {}
    }
}
