// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// A line in a place of a fixed size — a tile's face, a panel's header —
// that may not fit, and is still read in full.
//
// **A translation is often a third longer than the English**, and a tile
// does not grow for it. The line elides as it always did, and a pointer
// resting on it shows the whole of it; a line that fits shows no tooltip.
QQC2.Label {
    id: label

    elide: Text.ElideRight

    HoverHandler {
        id: hover
    }

    QQC2.ToolTip.text: label.text
    QQC2.ToolTip.visible: label.truncated && hover.hovered
    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
}
