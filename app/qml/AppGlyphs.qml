// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma Singleton

import QtQuick

// **The prototype's own glyphs** ([ADR-0012]): the handful of places
// where it draws a line icon of its own rather than one an icon theme would
// have. Everywhere else the icon theme's stay, in every appearance (ADR-0012,
// "icons stay the icon theme's").
//
// Each is a single-colour mask, tinted where it is drawn, and each is
// already at the size the prototype draws it inside the icon it stands in
// for: the "+" is its 14 pixels inside a tool button's 16, Run and Stop
// their 10 inside 16. The theme and window buttons are here for the
// window's own title bar, which draws them.
QtObject {
    id: glyphs

    readonly property string prefix: "qrc:/qt/qml/io/github/reverblabr/icons/glyph."

    /// The header's "+", for a new session.
    readonly property url add: glyphs.prefix + "add.svg"
    /// The Run button's triangle, and its square while the session runs.
    readonly property url run: glyphs.prefix + "run.svg"
    readonly property url stop: glyphs.prefix + "stop.svg"
    /// The tick in front of a chosen menu row.
    readonly property url check: glyphs.prefix + "check.svg"
    /// The arrow of a menu row that opens another menu.
    readonly property url submenu: glyphs.prefix + "submenu.svg"
    /// The header's menu button, which holds every menu while the
    /// menu bar is hidden.
    readonly property url menu: glyphs.prefix + "menu.svg"
    /// The theme button: the sun while the window is dark, the moon while
    /// it is light — what pressing it would turn the window into.
    readonly property url sun: glyphs.prefix + "sun.svg"
    readonly property url moon: glyphs.prefix + "moon.svg"
    /// The header's Virtual Devices button: a sound card in dashes,
    /// our own — the prototype has no such button.
    readonly property url virtualDevices: glyphs.prefix + "virtualdevices.svg"
    /// The window's own buttons.
    readonly property url minimize: glyphs.prefix + "window-minimize.svg"
    readonly property url maximize: glyphs.prefix + "window-maximize.svg"
    readonly property url restore: glyphs.prefix + "window-restore.svg"
    readonly property url close: glyphs.prefix + "window-close.svg"
    /// The sidebar's Blocks tab: four squares. Its Info tab has the
    /// circled "i" below.
    readonly property url blocks: glyphs.prefix + "blocks.svg"
    readonly property url files: glyphs.prefix + "files.svg"
    /// The circled "i" in front of a line of explanation.
    readonly property url info: glyphs.prefix + "info.svg"
    /// The circled "?" of a block's help button, drawn as the "i" is.
    /// Our own: the prototype has no help button, and the icon theme's help
    /// is a circled "i" too, which beside an explanation's would say the
    /// wrong thing.
    readonly property url help: glyphs.prefix + "help.svg"
    /// The sidebar's Schedule tab: a clock. Our own, drawn as the others.
    readonly property url schedule: glyphs.prefix + "schedule.svg"
    /// A closed padlock beside what asks for an administrator's password:
    /// the all-users virtual devices. Our own.
    readonly property url lock: glyphs.prefix + "lock.svg"
    /// The zoom control's "+" and "−".
    readonly property url zoomIn: glyphs.prefix + "zoom-in.svg"
    readonly property url zoomOut: glyphs.prefix + "zoom-out.svg"
}
