// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Fixture for tools/lint-i18n.py: the lint must report every line that ends
// with a marker comment, once per marker, and nothing else.

import QtQuick
import QtQuick.Controls as Controls

Item {
    property string title: "Untranslated title" // expect-finding

    Controls.Label {
        text: "Hello" // expect-finding
    }

    Controls.Label { text: 'Single quotes' } // expect-finding

    Controls.TextField {
        placeholderText: "Search" // expect-finding
    }

    Controls.Button {
        text: checked ? "Stop" : "Start" // expect-finding expect-finding
        ToolTip.text: "Starts the session" // expect-finding
        Accessible.name: "Start recording" // expect-finding
    }

    Controls.Label {
        text: KI18n.i18nc("@label", "Level") + " dB" // expect-finding
    }

    Controls.Label {
        text: qsTr("Qt translation") // expect-finding expect-finding
    }

    Controls.Label {
        text: KI18n.i18nc("@info", "A message too long for one line, split "
                          + "with a plus that xgettext never sees.") // expect-finding
    }
}
