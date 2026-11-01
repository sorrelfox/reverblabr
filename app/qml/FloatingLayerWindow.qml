// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.layershell as LayerShell

// A floating window as a layer-shell surface on the overlay layer (ADR-0030):
// placed by its margins from the top left of the screen, taking no focus and
// reserving no space.
//
// **The one file that names the module**, which a desktop may not have. It is
// left out of qmllint and of the compiled cache, and loaded by name
// (FloatingWindows), so that its absence costs a fallback and not a build.
FloatingWindow {
    id: surface

    movedByMargins: true

    LayerShell.Window.scope: "reverblabr"
    LayerShell.Window.layer: LayerShell.Window.LayerOverlay
    LayerShell.Window.anchors: LayerShell.Window.AnchorTop | LayerShell.Window.AnchorLeft
    LayerShell.Window.keyboardInteractivity: surface.placing ? LayerShell.Window.KeyboardInteractivityExclusive
                                                             : LayerShell.Window.KeyboardInteractivityNone
    LayerShell.Window.activateOnShow: false
    // Minus one measures the margins from the screen's edges, not from what
    // a panel leaves over, and moves nobody else.
    LayerShell.Window.exclusionZone: -1
    LayerShell.Window.margins.left: surface.leftEdge
    LayerShell.Window.margins.top: surface.topEdge
}
