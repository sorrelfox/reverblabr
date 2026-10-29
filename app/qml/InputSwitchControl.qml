// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// What the Input Switch's face and panel both read and send, so a
// click on the tile and a click in the panel are the same switch.
QtObject {
    id: control

    /// The block being switched.
    property string blockId: ""

    /// The block's own times (blocks::InputSwitch::kFadeMs and
    /// kSlowFadeMs), written down again because the interface does not
    /// include the block; tst_inputswitch.qml holds them against what a
    /// click sends.
    readonly property real fadeMs: 500
    readonly property real slowFadeMs: 2000

    readonly property string input: SessionModel.revision >= 0
        ? (SessionModel.param(control.blockId, "input") ?? "a")
        : "a"
    /// Where the crossfade is, from nought (A) to one (B), while the block
    /// runs; where the setting says otherwise. **Settable**, so a test can
    /// say what a running block would have.
    property var published: Meters.frame >= 0 && Meters.live ? (Meters.levels(control.blockId).values ?? []) : []
    readonly property real position: control.published.length > 0 ? control.published[0]
                                                                    : (control.input === "b" ? 1 : 0)
    readonly property bool crossfading: control.position > 0 && control.position < 1

    /// Whether B is the one passing.
    readonly property bool onB: control.input === "b"

    readonly property string nameA: KI18n.i18nc("@label the first input of the Input Switch block", "Input A")
    readonly property string nameB: KI18n.i18nc("@label the second input of the Input Switch block", "Input B")
    readonly property string letterA: KI18n.i18nc("@item the Input Switch's first input, as a letter", "A")
    readonly property string letterB: KI18n.i18nc("@item the Input Switch's second input, as a letter", "B")

    /// What is heard, in a line.
    readonly property string passing: control.crossfading
        ? KI18n.i18nc("@info the Input Switch is fading from one input to the other", "Crossfading…")
        : (control.onB
            ? KI18n.i18nc("@info the Input Switch passes its second input", "Input B passing")
            : KI18n.i18nc("@info the Input Switch passes its first input", "Input A passing"))

    /// Switches to \a input over half a second or, \a slow, two: the time,
    /// the input and the time put back, as one edit, so the block takes the
    /// switch over the time it was sent with.
    function switchTo(input: string, slow: bool) {
        // Already there is nothing to send: an edit that changes nothing
        // would still be a step to undo.
        if (input === control.input) {
            return;
        }
        SessionModel.setParams(control.blockId, [["crossfade", slow ? control.slowFadeMs : control.fadeMs],
                                                 ["input", input], ["crossfade", control.fadeMs]]);
    }

    function toggle(slow: bool) {
        control.switchTo(control.onB ? "a" : "b", slow);
    }

    function slowHeld(): bool {
        return (Keyboard.modifiers() & Qt.ShiftModifier) !== 0;
    }
}
