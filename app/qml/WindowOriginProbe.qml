// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

// **Where a window is on its screen** (ADR-0032), which Wayland does not tell
// a program and a panel leaving the window has to know.
//
// **A popup is told where it was put, relative to its parent.** So a tiny
// transparent one is asked for far off the top left corner, with the sliding
// Qt's popups ask for by default, and the compositor slides it back to the
// corner of the work area and says how far that is. It takes no pointer and
// no keyboard, is gone once it has answered, and leaves a press under way
// alone.
QtObject {
    id: probe

    /// The window to measure.
    property Window window: null
    /// Its contents' top left corner, from the top left of its screen's work
    /// area, once `known`.
    property point origin: Qt.point(0, 0)
    /// Whether `origin` holds what the last measurement found.
    property bool known: false
    /// A measurement ended, with `known` saying how it went.
    signal measured()

    /// How far off the screen to ask for. Further than any desktop reaches, so
    /// a position that comes back this far out is one nobody moved.
    readonly property int farAway: -100000
    /// Only Wayland needs asking. Elsewhere a window knows where it is.
    readonly property bool asksTheDesktop: Qt.platform.pluginName === "wayland"
    /// A desktop that does not answer has this long.
    readonly property int patienceMs: 250

    property Window askingWith: null

    function measure() {
        if (probe.window === null) {
            probe.known = false;
            probe.measured();
        } else if (!probe.asksTheDesktop) {
            const on = probe.window.screen;
            // Qt 6.10 types a window's screen as a plain QObject, so the
            // linter of that version does not know its virtualX and virtualY.
            // qmllint disable missing-property
            probe.origin = Qt.point(probe.window.x - (on?.virtualX ?? 0), probe.window.y - (on?.virtualY ?? 0));
            // qmllint enable missing-property
            probe.known = true;
            probe.measured();
        } else if (probe.askingWith === null) {
            probe.askingWith = popup.createObject(null, { transientParent: probe.window }) as Window;
            patience.restart();
        }
    }

    /// The popup stands at (\a x, \a y). **Only an answer counts**: being put
    /// far away in the first place moves it too, and the patience runs out if
    /// the desktop never moves it back. **Measured from the window**: the
    /// desktop answers relative to it and Qt adds the window's own `x`, which on
    /// Wayland is where its screen starts, so on any screen but the first the
    /// difference is what the desktop said.
    function answer(x: real, y: real) {
        if (x > probe.farAway && y > probe.farAway) {
            probe.finish(x - probe.window.x, y - probe.window.y);
        }
    }

    function finish(x: real, y: real) {
        patience.stop();
        const answered = x > probe.farAway && y > probe.farAway;
        if (answered) {
            probe.origin = Qt.point(-x, -y);
        }
        probe.known = answered;
        probe.askingWith?.destroy();
        probe.askingWith = null;
        probe.measured();
    }

    readonly property Timer patience: Timer {
        id: patience

        interval: probe.patienceMs
        onTriggered: probe.finish(probe.farAway, probe.farAway)
    }

    readonly property Component popup: Component {
        id: popup

        Window {
            id: asking

            flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
                | Qt.WindowTransparentForInput
            color: "transparent"
            width: 1
            height: 1
            x: probe.farAway
            y: probe.farAway
            visible: true
            // The two arrive together, from one configure. Read a turn later,
            // when both have.
            onXChanged: Qt.callLater(asking.report)
            onYChanged: Qt.callLater(asking.report)

            function report() {
                probe.answer(asking.x, asking.y);
            }
        }
    }
}
