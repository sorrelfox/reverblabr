// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import QtQuick.Window

// **The window a settings panel sits in once it is outside the main window**
// (ADR-0032): nothing but room for the card and its shadow. The panel is the
// same item it was on the canvas, moved here, so its edits and its scroll
// position come along and nothing is built twice.
//
// **What it has to make up for** is what a panel gets from the canvas without
// asking: the keys it does not use, which go on to the canvas, and the main
// window's shortcuts, which Qt offers only to the window that has the keyboard.
//
// This is the plain window, which is what X11 gets, and Wayland without a
// layer shell. PanelLayerWindow adds its attached properties to this.
Window {
    id: surface

    /// The panel it holds.
    property SettingsPanel panel: null
    /// The canvas the panel belongs to. Not typed, as the canvas names this
    /// type.
    property var canvas: null
    /// Whether it has been told everything it needs to be shown. **A layer
    /// surface picks its screen once, as it first appears**, so it must not
    /// appear until `screen` is the right one: the first screen would keep it,
    /// with its place measured from there.
    property bool ready: false
    /// The desktop moves it, on a press. A plain window on Wayland can't move
    /// itself, and is never told where it went.
    property bool movesByDesktop: Qt.platform.pluginName === "wayland"

    /// Room for the shadow around the card, which a window has to draw inside
    /// itself.
    readonly property int inset: AppMetrics.liftedShadowSize + AppMetrics.liftedShadowOffset
    /// The screen its place is measured on.
    readonly property var placedOn: surface.canvas?.screenForOutside(surface.panel?.outsideScreen ?? "") ?? null
    /// How far the window sits from that screen's work area.
    readonly property int leftEdge: Math.round((surface.panel?.outsideLeft ?? 0) - surface.inset)
    readonly property int topEdge: Math.round((surface.panel?.outsideTop ?? 0) - surface.inset)
    /// Room for the card, whatever its size is by now.
    readonly property int wantedWidth: (surface.panel?.width ?? 0) + surface.inset * 2
    readonly property int wantedHeight: (surface.panel?.height ?? 0) + surface.inset * 2
    /// Where the panel goes.
    readonly property alias holder: holder
    readonly property alias watch: watch
    /// The main window's shortcuts here, for a test.
    readonly property alias shortcuts: shortcuts

    /// A drag of the header by (\a dx, \a dy). **The window moves under the
    /// pointer**, so a position is measured from the old place until a move
    /// has reached the screen, and the same distance would be added again with
    /// each event: the same wait as a floating window's (ADR-0030).
    function follow(dx: real, dy: real) {
        if (!landing.running && surface.panel !== null && surface.canvas !== null) {
            surface.canvas.moveOutsidePanel(surface.panel.blockId, dx, dy);
            landing.restart();
        }
    }

    /// **The desktop's answer can overtake a newer size.** A card coming back
    /// grows as its body arrives, after its window has asked for the size it
    /// had, and that answer leaves the window smaller than the card with
    /// nothing in the bindings having changed to put it right.
    function fit() {
        if (surface.width !== surface.wantedWidth) {
            surface.width = Qt.binding(() => surface.wantedWidth);
        }
        if (surface.height !== surface.wantedHeight) {
            surface.height = Qt.binding(() => surface.wantedHeight);
        }
    }

    function keepOnScreen() {
        if (surface.panel !== null && surface.canvas !== null) {
            surface.canvas.moveOutsidePanel(surface.panel.blockId, 0, 0);
        }
    }

    transientParent: surface.canvas?.Window.window ?? null
    screen: surface.placedOn
    x: (surface.placedOn?.virtualX ?? 0) + surface.leftEdge
    y: (surface.placedOn?.virtualY ?? 0) + surface.topEdge
    width: surface.wantedWidth
    height: surface.wantedHeight
    color: "transparent"
    flags: Qt.Tool | Qt.FramelessWindowHint
    title: surface.panel?.displayName ?? ""
    // **With the main window**, which is what ending the application waits
    // for as well: a window left up would keep it running with no way back.
    visible: surface.ready && (surface.canvas?.mainWindowShowing ?? false)

    // **A layer surface's place takes effect with its next commit**, and a
    // card with nothing new to draw makes none (ADR-0030).
    onLeftEdgeChanged: surface.update()
    onTopEdgeChanged: surface.update()
    onWidthChanged: Qt.callLater(surface.fit)
    onHeightChanged: Qt.callLater(surface.fit)
    // **A card that grows stays on the screen**: one opened low, or an
    // Advanced section unfolded, moves up as far as it has to, as a drag
    // would have kept it.
    onWantedHeightChanged: Qt.callLater(surface.keepOnScreen)

    WindowWatch {
        id: watch

        window: surface
        panel: surface.panel
        dialogOverlay: surface.canvas?.mainOverlay ?? null
        inputArea: Qt.rect(surface.inset, surface.inset, surface.panel?.width ?? 0, surface.panel?.height ?? 0)
    }

    Timer {
        id: landing

        // Two frames at 60 Hz, and a compositor's own frame.
        interval: 50
    }

    Item {
        id: holder

        anchors.fill: parent
        // So that a key has somewhere to arrive with nothing in the panel
        // holding the keyboard.
        focus: true
        // What a panel does not use goes up to the canvas, as it does on the
        // canvas: Del and Esc, and the keys that draw a wire.
        Keys.forwardTo: surface.canvas !== null ? [surface.canvas] : []
    }

    // **The main window's shortcuts work here too**, as they do with the
    // keyboard in a panel on the canvas: undo, above all.
    Instantiator {
        id: shortcuts

        model: surface.canvas?.windowActions ?? []

        delegate: Shortcut {
            required property T.Action modelData

            // A list, as a standard key can be several: Redo is two.
            sequences: [modelData.shortcut]
            // Space belongs to the button or the box that has the keyboard,
            // as it does in the main window.
            enabled: modelData.enabled
                && !(String(modelData.shortcut) === "Space"
                     && (surface.activeFocusItem instanceof T.AbstractButton
                         || surface.activeFocusItem instanceof T.ComboBox))
            onActivated: modelData.trigger()
        }
    }
}
