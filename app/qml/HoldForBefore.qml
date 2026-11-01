// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.ki18n

// **Hears the sound without a block, for a moment.** It goes round the session
// (SessionModel.holdBlockOff): the block's real off state, so the delay stays
// and the picture does not move, and a comparison is no edit, so undo and the
// unsaved mark never hear of it. The graph's own fade takes it there and back.
//
// Held with the pointer or the Space key. Enter keeps it on until Enter again.
// A card passes it the B key through keyDown() and keyUp(), so that B works
// wherever the focus is in the card. A key's release goes to whatever has the
// focus, so a hold by key also ends when the focus or the window moves on.
QQC2.Button {
    id: control

    /// The block this holds off.
    property string blockId: ""

    /// Whether the block is being held off now, by any route.
    readonly property bool holding: control.heldBlock !== ""
    /// Whether Enter has it held.
    property bool kept: false
    /// Whether the B key has it held.
    property bool byKey: false

    /// The block held, or nothing: the one to let go of, whatever happens to
    /// blockId meanwhile.
    property string heldBlock: ""

    readonly property var focusNow: control.Window.activeFocusItem
    readonly property bool windowActive: control.Window.active

    function begin() {
        if (control.heldBlock === "" && control.blockId !== "") {
            control.heldBlock = control.blockId;
            SessionModel.holdBlockOff(control.heldBlock, true);
        }
    }

    function end() {
        control.kept = false;
        control.byKey = false;
        if (control.heldBlock !== "") {
            SessionModel.holdBlockOff(control.heldBlock, false);
            control.heldBlock = "";
        }
    }

    /// Enter: held until the next one.
    function toggleKept() {
        if (control.kept) {
            control.end();
        } else {
            control.begin();
            control.kept = true;
        }
    }

    /// A card's key press. Returns whether it was the B key.
    function keyDown(event: var): bool {
        if (event.key !== Qt.Key_B || event.modifiers !== Qt.NoModifier) {
            return false;
        }
        if (!event.isAutoRepeat && !control.kept) {
            control.byKey = true;
            control.begin();
        }
        return true;
    }

    /// A card's key release. Returns whether it was the B key.
    function keyUp(event: var): bool {
        if (event.key !== Qt.Key_B) {
            return false;
        }
        if (!event.isAutoRepeat && control.byKey) {
            control.end();
        }
        return true;
    }

    // A card can stay and be handed another block of its type, and a hold kept
    // by Enter would stay on the first with its button gone.
    onBlockIdChanged: control.end()
    onFocusNowChanged: if (control.byKey) {
        control.end();
    }
    onWindowActiveChanged: if (control.byKey) {
        control.end();
    }

    text: KI18n.i18nc("@action:button hold to hear the sound without the block", "Hold for before")
    icon.name: "media-playback-pause"
    down: control.pressed || control.holding
    onPressed: control.begin()
    onReleased: control.end()
    onCanceled: control.end()
    Keys.onReturnPressed: control.toggleKept()
    Keys.onEnterPressed: control.toggleKept()
    Component.onDestruction: control.end()
    Accessible.name: control.text
    Accessible.description: KI18n.i18nc("@info:tooltip", "The sound without this block, for as long as it is held")
    QQC2.ToolTip.visible: hovered
    QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip", "The sound without this block, for as long as it is held")
}
