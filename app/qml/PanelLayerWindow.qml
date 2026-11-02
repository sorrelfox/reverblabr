// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.layershell as LayerShell

// A settings panel's window as a layer-shell surface (ADR-0032): the one kind
// of window that can place itself on Wayland, and so come back where it was.
//
// **Named here and in FloatingLayerWindow and nowhere else**, for the reason
// given there: the module may be missing, and that must cost a fallback and
// not a build.
PanelWindow {
    id: surface

    movesByDesktop: false
    transientParent: null

    LayerShell.Window.scope: "reverblabr-panel"
    // **Top and not Overlay**: a full-screen program goes over it.
    LayerShell.Window.layer: LayerShell.Window.LayerTop
    LayerShell.Window.anchors: LayerShell.Window.AnchorTop | LayerShell.Window.AnchorLeft
    // A click gives it the keyboard, for the fields in a panel, and showing
    // it takes the keyboard from nobody.
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityOnDemand
    LayerShell.Window.activateOnShow: false
    // **Zero measures the margins from the work area**, which is the area the
    // main window's origin is measured from (WindowOriginProbe).
    LayerShell.Window.exclusionZone: 0
    LayerShell.Window.margins.left: surface.leftEdge
    LayerShell.Window.margins.top: surface.topEdge
}
