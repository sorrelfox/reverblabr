// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.ki18n

// The Info tab: what this session is, how it behaves, and what it is
// running on.
//
// **The settings a session carries, not the application's.** What is here
// belongs to the session and travels with its file; anything that is true of
// every session belongs in the application's own settings and not in a tab
// that changes when another session is opened.
//
// **The audio system box is read-only on purpose.** What the graph is doing
// is WirePlumber's business ([ADR-0002]) and not ours to set from here;
// showing it is how somebody finds out why the numbers in the status bar
// are what they are.
QQC2.ScrollView {
    id: pane

    // The window's margin around the forms, which started at the
    // pane's very edge; the prototype's 10 and 12 in the Reverblabr
    // appearance.
    padding: AppMetrics.followsSystem ? Kirigami.Units.largeSpacing : AppMetrics.panelPadding
    topPadding: AppMetrics.followsSystem ? Kirigami.Units.largeSpacing : AppMetrics.rowSpacing

    /// **The figures of whatever is running**: the session's own, as
    /// its node saw them, while one of ours runs; the graph's, as the audio
    /// system's clock settings give them, while nothing of ours does. A dash
    /// only when neither has said.
    readonly property int shownRate: AppShell.hasStatus ? AppShell.sampleRate : AudioSystem.rate
    readonly property int shownQuantum: AppShell.hasStatus ? AppShell.quantum : AudioSystem.quantum
    readonly property real shownCycleMs: AppShell.hasStatus ? AppShell.latencyMs : AudioSystem.cycleMs

    /// The rate the graph is running at, said the way a person says it.
    readonly property string rateText: pane.shownRate > 0
        ? KI18n.i18nc("@info a sample rate in kilohertz", "%1 kHz", Math.round(pane.shownRate / 100) / 10)
        : pane.dash
    /// How long one cycle is, in frames and in milliseconds.
    readonly property string quantumText: pane.shownQuantum > 0
        ? KI18n.i18nc("@info the audio cycle length, for example '1024 frames · 21.3 ms'", "%1 frames · %2 ms",
                      pane.shownQuantum, Math.round(pane.shownCycleMs * 10) / 10)
        : pane.dash

    readonly property string dash: "—"

    /// The notes box, for a test that types in it.
    readonly property alias notesField: notesField
    readonly property alias autoRunSwitch: autoRunSwitch
    readonly property alias autoRunNeedsBackground: autoRunNeedsBackground
    /// The diagnostics section, for a test that opens it.
    readonly property alias diagnosticsSection: diagnosticsSection
    readonly property alias listSwitch: listSwitch

    contentWidth: availableWidth

    ColumnLayout {
        width: pane.availableWidth
        spacing: AppMetrics.followsSystem ? Kirigami.Units.largeSpacing : AppMetrics.groupSpacing

        Kirigami.FormLayout {
            id: sessionForm

            Layout.fillWidth: true

            QQC2.TextField {
                id: titleField

                Kirigami.FormData.label: KI18n.i18nc("@label:textbox what the session is called", "Title:")
                enabled: AppShell.hasSession
                Accessible.name: KI18n.i18nc("@label:textbox", "Session title")
                onEditingFinished: SessionModel.renameSession(titleField.text)

                // A control's full 28 pixels, as the prototype's name field.
                Binding on implicitHeight {
                    when: !AppMetrics.followsSystem
                    value: AppMetrics.controlHeight
                }

                // **Left alone while somebody is typing.** A field that
                // rewrites itself under the cursor because a patch came
                // back is a field nothing can be typed into; the session
                // has the last word again as soon as the cursor leaves,
                // which is also when an empty name is put back.
                SessionBound {
                    control: titleField
                    property: "text"
                    value: SessionModel.name
                    suspended: titleField.activeFocus
                }
            }

            QQC2.TextArea {
                id: notesField

                Kirigami.FormData.label: KI18n.i18nc("@label:textbox what the session is for", "Notes:")
                enabled: AppShell.hasSession
                wrapMode: TextEdit.Wrap
                // A hint longer than the field is whole on hover.
                QQC2.ToolTip.text: placeholderText
                QQC2.ToolTip.visible: hovered && text === "" && placeholderText !== ""
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                placeholderText: KI18n.i18nc("@info:placeholder",
                                             "What this session is for, and anything worth remembering about it")
                Accessible.name: KI18n.i18nc("@label:textbox", "Session notes")
                Layout.fillWidth: true
                Layout.preferredHeight: Kirigami.Units.gridUnit * 5
                // **A text area has no editingFinished**, so the cursor
                // leaving is what ends an edit. Nothing is compared here:
                // an empty note is a note, and the model is the one that
                // knows whether anything actually changed.
                onActiveFocusChanged: {
                    if (!notesField.activeFocus) {
                        SessionModel.setNotes(notesField.text);
                    }
                }

                // Left alone while somebody is typing, for the reason the
                // title field above gives.
                SessionBound {
                    control: notesField
                    property: "text"
                    value: SessionModel.notes
                    suspended: notesField.activeFocus
                }
            }

            QQC2.Switch {
                id: autoConnectSwitch

                Kirigami.FormData.label: KI18n.i18nc("@label wire new blocks up as they are added",
                                                     "Automatic connections:")
                enabled: AppShell.hasSession
                text: KI18n.i18nc("@option:check", "Connect blocks as they're added")
                onToggled: SessionModel.setAutoConnect(autoConnectSwitch.checked)

                SessionBound {
                    control: autoConnectSwitch
                    property: "checked"
                    value: SessionModel.autoConnect
                }
            }

            // **The same switch as View → Show as List**: not the
            // session's, the person's, so it is on for every session.
            QQC2.Switch {
                id: listSwitch

                Kirigami.FormData.label: KI18n.i18nc("@label how the session's blocks are shown", "Blocks:")
                text: KI18n.i18nc("@option:check show the session's blocks as a list instead of on the canvas",
                                  "Show as list")
                checked: WindowState.listShown
                onToggled: WindowState.listShown = listSwitch.checked
            }

            RowLayout {
                Kirigami.FormData.label: KI18n.i18nc("@label the rate the session runs at", "Sample rate:")
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    // The only choice there is: the rate belongs to the
                    // whole graph, and a session that asked for its own
                    // would be asking every other application to change.
                    text: KI18n.i18nc("@item the sample rate follows the audio system", "Automatic")
                    color: AppColors.text
                }

                QQC2.Label {
                    text: pane.shownRate > 0
                        ? KI18n.i18nc("@info the rate in use now, in brackets after 'Automatic'", "(%1 now)",
                                      pane.rateText)
                        : KI18n.i18nc("@info nothing is running, so there is no rate to show", "(nothing running)")
                    color: AppColors.muted
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }

            QQC2.ComboBox {
                id: latencyBox

                Kirigami.FormData.label: KI18n.i18nc("@label how much delay the session asks for", "Latency:")
                enabled: AppShell.hasSession
                // The three the session file holds, in order of how little
                // delay they ask the audio system for.
                readonly property var codes: ["default", "low", "high"]
                model: [
                    KI18n.i18nc("@item:inlistbox latency", "Default"),
                    KI18n.i18nc("@item:inlistbox latency", "Low"),
                    KI18n.i18nc("@item:inlistbox latency", "Very low")
                ]
                onActivated: index => SessionModel.setLatency(latencyBox.codes[index])

                SessionBound {
                    control: latencyBox
                    property: "currentIndex"
                    value: Math.max(0, latencyBox.codes.indexOf(SessionModel.latency))
                }
            }

            // **Auto Run**: the background engine starts the session the
            // first time it starts in a login (ADR-0023), which with this
            // on is at login itself. The
            // setting is the session's and is kept either way; a window
            // running an engine of its own says that nothing will start it.
            ColumnLayout {
                Kirigami.FormData.label: KI18n.i18nc("@label start the session without opening the window",
                                                     "Auto Run:")
                spacing: Kirigami.Units.smallSpacing
                Layout.fillWidth: true

                QQC2.Switch {
                    id: autoRunSwitch

                    objectName: "autoRunSwitch"
                    enabled: AppShell.hasSession
                    text: KI18n.i18nc("@option:check the session starts at login", "Run when I log in")
                    onToggled: SessionModel.setAutoRun(autoRunSwitch.checked ? "login" : "off")

                    SessionBound {
                        control: autoRunSwitch
                        property: "checked"
                        value: SessionModel.autoRun === "login"
                    }
                }

                QQC2.Label {
                    id: autoRunNeedsBackground

                    objectName: "autoRunNeedsBackground"
                    visible: !AppShell.engineInBackground
                    text: KI18n.i18nc("@info under the Auto Run switch",
                                      "Needs Reverblabr's background engine, which isn't installed here. The session keeps the setting.")
                    color: AppColors.muted
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        // --- what it is all running on ---

        Kirigami.Heading {
            text: KI18n.i18nc("@title:group the audio server and its session manager", "Audio system")
            level: 4
            Layout.fillWidth: true
        }

        Kirigami.FormLayout {
            id: systemForm

            Layout.fillWidth: true

            QQC2.Label {
                Kirigami.FormData.label: KI18n.i18nc("@label the audio server", "Server:")
                text: AudioSystem.serverName.length > 0
                    ? (AudioSystem.serverVersion.length > 0
                        ? KI18n.i18nc("@info a name and a version", "%1 %2", AudioSystem.serverName,
                                      AudioSystem.serverVersion)
                        : AudioSystem.serverName)
                    : pane.dash
                color: AppColors.text
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            QQC2.Label {
                Kirigami.FormData.label: KI18n.i18nc("@label what decides the audio policy", "Session manager:")
                text: AudioSystem.sessionManagerName.length > 0
                    ? (AudioSystem.sessionManagerVersion.length > 0
                        ? KI18n.i18nc("@info a name and a version", "%1 %2", AudioSystem.sessionManagerName,
                                      AudioSystem.sessionManagerVersion)
                        : AudioSystem.sessionManagerName)
                    : pane.dash
                color: AppColors.text
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            QQC2.Label {
                Kirigami.FormData.label: KI18n.i18nc("@label the rate the whole graph runs at", "Graph rate:")
                text: pane.rateText
                color: AppColors.text
            }

            QQC2.Label {
                Kirigami.FormData.label: KI18n.i18nc("@label how long one audio cycle is", "Cycle:")
                text: pane.quantumText
                color: AppColors.text
            }

            QQC2.Label {
                Kirigami.FormData.label: KI18n.i18nc("@label the device driving the graph", "Driver:")
                text: AudioSystem.driver.length > 0 ? AudioSystem.driver : pane.dash
                color: AppColors.text
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            RowLayout {
                Kirigami.FormData.label: KI18n.i18nc("@label whether the engine can reach the audio system",
                                                     "Connection:")
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    id: connectionLabel

                    text: AudioSystem.connected
                        ? KI18n.i18nc("@info the engine is talking to the audio system", "Connected")
                        : KI18n.i18nc("@info the engine cannot reach the audio system", "Not running")
                    color: AudioSystem.connected ? AppColors.okText : AppColors.warningText
                }

                QQC2.Button {
                    id: retryButton

                    visible: !AudioSystem.connected
                    text: KI18n.i18nc("@action:button try to reach the audio system again", "Retry")
                    icon.name: "view-refresh"
                    onClicked: AudioSystem.reconnect()
                }
            }
        }

        // **What a bug report needs**, folded until somebody opens it.
        DiagnosticsSection {
            id: diagnosticsSection

            Layout.fillWidth: true
        }
    }
}
