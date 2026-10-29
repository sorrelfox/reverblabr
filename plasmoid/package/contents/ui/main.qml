// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid
import io.github.reverblabr.plasmoid
import "status.js" as Status

// **Reverblabr Sessions, the panel widget** (docs/adr/0024): Reverblabr's
// icon in the panel, showing whether anything runs or records, and every
// saved session with Run and Stop in its popup. It works with the window
// closed, through the background engine.
//
// The words are in the widget's own translation domain,
// plasma_applet_io.github.reverblabr.sessions, which Plasma gives every
// i18n call here by the widget's id.
PlasmoidItem {
    id: root

    SessionList {
        id: engineSessions

        watching: root.expanded
    }

    // **Out of sight in the system tray while nothing runs**, and among the
    // visible icons while something does.
    Plasmoid.status: engineSessions.runningCount > 0 ? PlasmaCore.Types.ActiveStatus : PlasmaCore.Types.PassiveStatus
    Plasmoid.icon: "io.github.reverblabr.Reverblabr"

    toolTipMainText: i18nc("@title the panel widget's name", "Reverblabr Sessions")
    toolTipSubText: Status.summary(engineSessions)

    preferredRepresentation: compactRepresentation

    compactRepresentation: CompactRepresentation {
        sessions: engineSessions
        expanded: root.expanded
        onToggled: root.expanded = !root.expanded
    }

    fullRepresentation: FullRepresentation {
        sessions: engineSessions
    }
}
