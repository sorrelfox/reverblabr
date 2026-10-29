// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.ki18n

// The frame the session canvas hangs in: the area it fills and the colour
// it sits on, with a word in place of it for a window that has been built
// without one.
//
// **The colour lives here rather than in the canvas.** A canvas that has
// not been put in yet still has to look like a canvas and not like a hole,
// which is what lets the rest of the window be laid out and looked at on
// its own.
Rectangle {
    id: canvas

    /// The canvas itself (SessionCanvas), put in by the window.
    property alias content: contentSlot.data

    color: AppColors.canvas

    Item {
        id: contentSlot

        anchors.fill: parent
    }

    PlaceholderMessage {
        anchors.centerIn: parent
        width: Math.min(parent.width - Kirigami.Units.gridUnit * 2, Kirigami.Units.gridUnit * 24)
        visible: contentSlot.children.length === 0
        text: KI18n.i18nc("@info:placeholder", "The session canvas goes here")
    }
}
