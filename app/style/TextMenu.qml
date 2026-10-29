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
// and the rest in Qt's words, as the Basic style gives it. A field sets
// `ContextMenu.menu: textMenu.menu` and holds one of these.
//
// **Qt 6.11 and later only.** Qt 6.10 has no such menu, and
// qt6.10/TextMenu.qml stands in for this file there (CMakeLists.txt picks
// one), so that the same fields build on both.
QtObject {
    id: root

    /// The text item the menu cuts from and pastes into.
    property Item editor

    readonly property T.Menu menu: TextEditingContextMenu {
        editor: root.editor
    }
}
