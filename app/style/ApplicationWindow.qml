// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Templates as T
import io.github.reverblabr

// The window, on the design's window colour, **and the type
// everything in it inherits**.
//
// **A label takes its face from the window, not from the application.**
// Qt Quick gives each kind of control a default font of the platform's,
// and on a KDE desktop that is the desktop's font at the desktop's size —
// so a plain label read 10-point desktop type in a window whose every
// control said 12.5-pixel Noto Sans. What a control inherits from the
// window beats that default, so the window says the design's body text
// once, and every label, heading, menu and dialog in it follows.
T.ApplicationWindow {
    id: window

    color: AppColors.window
    font: AppFonts.body
}
