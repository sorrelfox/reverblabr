// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick

// Keeps one control showing what the session says, for as long as it lives.
//
// **A binding is not enough.** A control assigns its own property when
// somebody uses it — a CheckBox writes `checked`, a ComboBox writes
// `currentIndex`, a Slider writes `value`, a TextField writes `text` — and
// that assignment drops the binding that was drawing it from the session.
// From then on the control shows the last thing it was clicked to, so it
// stops following an undo, a second window, a session opened while the
// panel is showing, or the engine refusing the value outright. Nothing
// looks wrong until somebody undoes something, which is why the same trap
// was walked into in panel after panel before it was named.
//
// So the session's answer is not bound to the property: it is written to
// it, again, every time the session says anything at all. **The session
// gets the last word**, and a panel author writes the expression once
// instead of once for the binding and once for putting it back.
//
// **Every change, not every different value.** A click the engine refused,
// or clamped to the value that was already there, leaves that value where
// it was and the control showing something else; only the fact that
// something happened says the control needs redrawing.
// **Not a `Connections`, though it listens to one.** A `Connections`
// reads every `onSomething` in it as a handler for a signal of its
// target, so an object written that way cannot notice its own properties
// changing — it is told it is assigning to a property that does not
// exist, and the control silently stops being redrawn.
QtObject {
    id: bound

    /// The control being kept honest.
    required property var control
    /// The one property of it the session owns — "checked", "currentIndex",
    /// "value", "text" — and which this object, rather than a binding,
    /// writes.
    required property string property
    /// What the session says it should be, written as an ordinary binding.
    required property var value

    /// While this is true the control is left alone, because somebody is
    /// working it: a text field would rewrite itself under the cursor, and
    /// a slider would fight the pointer. What the session says is put back
    /// the moment it goes false again, so nothing is lost by waiting.
    property bool suspended: false

    readonly property Connections session: Connections {
        target: SessionModel

        function onChanged() {
            bound.settle();
        }
    }

    /// Writes the session's answer into the control.
    function settle() {
        if (!bound.suspended) {
            bound.control[bound.property] = bound.value;
        }
    }

    onValueChanged: bound.settle()

    // **Letting go settles on the next turn of the loop, not on this one.**
    // Letting go of a control is also the moment a panel sends what was
    // done with it — a text field sends on losing focus, a slider on the
    // button coming up — and a handler and a binding on the same change
    // run in whatever order they were built in. Waiting one turn means the
    // session has been told what was typed before it is asked what the
    // field should say, so an edit can never be overwritten by the value
    // it was about to replace.
    onSuspendedChanged: if (!bound.suspended) {
        Qt.callLater(bound.settle);
    }
    // The control is drawn before anybody has touched anything, and it is
    // drawn from the session like every state after it.
    Component.onCompleted: bound.settle()
}
