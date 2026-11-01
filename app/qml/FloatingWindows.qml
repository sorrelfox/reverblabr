// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma Singleton

import QtQuick

// **Which window a floating surface gets on this desktop** (ADR-0030): the
// overlay layer where the Wayland platform and the layer-shell module are
// both there, and a plain window with a hint anywhere else.
//
// Asked for by name, so that nothing depends on a module that may be absent:
// loading that one file is the probe. It is never loaded on any other
// platform, offscreen included.
QtObject {
    id: windows

    /// The two surfaces (FloatingSurface::kLabel, kStrip).
    readonly property string labelStack: "label"
    readonly property string strip: "strip"

    /// The screens there are, as a list. Through a plain property, as
    /// qmllint does not know this member of Qt.application.
    readonly property var application: Qt.application
    readonly property var screens: Array.from(windows.application.screens)

    /// The screen called \a name, or undefined. No name is no screen, and
    /// some screens have none.
    function screenNamed(name: string): var {
        return name === "" ? undefined : windows.screens.find(screen => screen.name === name);
    }

    /// Place Strip is under way: the strip has a frame and the keyboard.
    property bool placing: false
    /// Where it stood when that began, for Esc to go back to.
    property var beforePlacing: ({})
    /// Test Strip is running: the strip's mark goes left, centre, right.
    property bool sweeping: false
    property real sweepPan: 0
    readonly property var sweepStops: [-0.8, 0, 0.8]
    property int sweepStop: 0
    /// How long the mark stands at each stop. Three stops are the three seconds.
    readonly property int sweepStopMs: 1000
    /// Whether the strip has to be up though no block asks for it.
    readonly property bool stripInUse: windows.placing || windows.sweeping

    /// Moves the mark across once, on the strip alone.
    function sweepStrip() {
        windows.sweepStop = 0;
        windows.sweepPan = windows.sweepStops[0];
        windows.sweeping = true;
        windows.sweepTimer.restart();
    }

    readonly property Timer sweepTimer: Timer {
        interval: windows.sweepStopMs
        repeat: true
        onTriggered: {
            if (++windows.sweepStop >= windows.sweepStops.length) {
                windows.sweeping = false;
                windows.sweepTimer.stop();
            } else {
                windows.sweepPan = windows.sweepStops[windows.sweepStop];
            }
        }
    }

    function startPlacing() {
        // A second press must not make the moved place the one Esc goes to.
        if (windows.placing) {
            return;
        }
        const placement = FloatingSurface.placement(windows.strip);
        windows.beforePlacing = { left: placement.left, top: placement.top };
        windows.placing = true;
    }

    /// Ends Place Strip: \a keep writes the place, and otherwise it goes back.
    function endPlacing(keep: bool) {
        if (windows.placing) {
            FloatingSurface.setPlacement(windows.strip, keep ? {} : windows.beforePlacing);
            windows.placing = false;
        }
    }

    readonly property Component layer: Qt.platform.pluginName === "wayland"
        ? Qt.createComponent("FloatingLayerWindow.qml") : null
    readonly property Component plain: Component {
        FloatingWindow {}
    }
    /// Whether the desktop can be held to keeping a surface on top. Without
    /// it the card says so.
    readonly property bool keepsOnTop: windows.layer !== null && windows.layer.status === Component.Ready
    readonly property Component window: windows.keepsOnTop ? windows.layer : windows.plain
}
