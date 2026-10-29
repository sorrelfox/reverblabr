// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Fixture for tools/lint-i18n.py: nothing in this file may be flagged.

import QtQuick
import QtQuick.Controls as Controls
import org.kde.ki18n

Item {
    id: root

    property int count: 3
    property string sessionName: "internal-id"

    Controls.Label { text: KI18n.i18nc("@label", "Hello") }

    Controls.Label {
        text: KI18n.i18ncp("@info", "%1 file", "%1 files", root.count)
    }

    Controls.Label {
        text: KI18n.i18nc("@info",
                          "A message that continues on the next line")
    }

    Controls.Label { text: "" }
    Controls.Label { text: "•" }
    Controls.Label { text: "→ " + root.sessionName }
    Controls.Label { text: root.sessionName }
    Controls.Label { objectName: "statusLabel" }

    Controls.Label {
        text: "Frame time" // no-i18n: developer overlay, never shown to users
    }

    Controls.Button {
        text: checked ? KI18n.i18nc("@action:button", "Stop") : KI18n.i18nc("@action:button", "Start")
        icon.name: "media-playback-start"
    }
}
