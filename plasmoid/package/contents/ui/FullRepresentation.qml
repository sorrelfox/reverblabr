// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3
import org.kde.plasma.extras as PlasmaExtras
import io.github.reverblabr.plasmoid

// **What the widget's popup shows**: every saved session, with how loud it
// is, whether it records, and Run or Stop. The same list as the window's
// tray popover, drawn with the panel's own controls and colours.
PlasmaExtras.Representation {
    id: full

    required property SessionList sessions

    /// The parts, for the tests.
    readonly property alias sessionList: sessionList
    readonly property alias placeholder: placeholder
    readonly property alias failureMessage: failureMessage

    Layout.minimumWidth: Kirigami.Units.gridUnit * 14
    Layout.preferredWidth: Kirigami.Units.gridUnit * 20
    Layout.minimumHeight: Kirigami.Units.gridUnit * 8
    Layout.preferredHeight: Kirigami.Units.gridUnit * 18

    collapseMarginsHint: true

    header: PlasmaExtras.PlasmoidHeading {
        PlasmaExtras.Heading {
            anchors.fill: parent
            level: 3
            text: i18nc("@title the sessions listed in the panel widget", "Sessions")
            elide: Text.ElideRight
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        // **What kept a session from running**, until the next Run or until
        // the popup closes.
        Kirigami.InlineMessage {
            id: failureMessage

            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            type: Kirigami.MessageType.Error
            /// The session was deleted since the list was read.
            readonly property bool gone: (full.sessions?.failure.code ?? "") === "session.notFound"

            visible: (full.sessions?.failure.session ?? "") !== ""
            text: failureMessage.gone
                ? i18nc("@info %1 is a session", "\"%1\" is no longer saved.", full.sessions?.failure.name ?? "")
                : i18nc("@info %1 is a session", "\"%1\" could not be started. Open Reverblabr to see why.",
                        full.sessions?.failure.name ?? "")
        }

        PlasmaComponents3.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true

            contentItem: ListView {
                id: sessionList

                model: full.sessions
                clip: true
                spacing: Kirigami.Units.smallSpacing
                topMargin: Kirigami.Units.smallSpacing
                bottomMargin: Kirigami.Units.smallSpacing
                boundsBehavior: Flickable.StopAtBounds

                delegate: SessionRow {
                    x: Kirigami.Units.smallSpacing * 2
                    width: ListView.view.width - Kirigami.Units.smallSpacing * 4
                    live: full.sessions?.watching ?? false
                    onRunRequested: full.sessions.run(sessionId)
                    onStopRequested: full.sessions.stop(sessionId)
                }

                PlasmaExtras.PlaceholderMessage {
                    id: placeholder

                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.gridUnit * 2
                    visible: sessionList.count === 0 && full.sessions !== null
                             && (!full.sessions.installed || full.sessions.ready)
                    iconName: (full.sessions?.installed ?? true) ? "io.github.reverblabr.Reverblabr" : "dialog-warning"
                    text: (full.sessions?.installed ?? true)
                        ? i18nc("@info in the panel widget", "No saved sessions yet")
                        : i18nc("@info in the panel widget", "Reverblabr's background engine is not installed")
                }
            }
        }
    }
}
