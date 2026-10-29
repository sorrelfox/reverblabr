// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.kirigami as Kirigami

// A question of ours: Kirigami's prompt, measured as AppDialog
// measures a dialog — the room round its words and its buttons from the
// tokens, and Kirigami's own while following the system. A prompt has no
// header: its title stands in the content, inside the same room.
Kirigami.PromptDialog {
    id: dialog

    // Kirigami's own is its large spacing, which the token is while
    // following the system.
    contentPadding: AppMetrics.dialogPadding

    property list<Binding> tokenPadding: [
        Binding {
            when: !AppMetrics.followsSystem && !dialog.isSliver(dialog.footer)
            target: dialog.footer
            property: "padding"
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
