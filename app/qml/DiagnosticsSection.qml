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

// **Diagnostics**, under the audio system on the Info tab: how each
// open session is keeping up, our part of the graph, which recording
// formats work here, what the plugin scan found, and a button that puts all
// of it, with the engine's last log lines, on the clipboard for a bug
// report.
//
// **Asked for only while it is open.** The answer carries the log, which is
// not worth sending twice a second to a page nobody is looking at; folded,
// the section asks nothing.
CollapsibleSection {
    id: section

    title: KI18n.i18nc("@title:group what the engine can say about itself for a bug report", "Diagnostics")

    /// Whether the section asks the engine, for a test.
    readonly property bool asking: refreshTimer.running
    /// The parts a test presses or counts.
    readonly property alias copyButton: copyButton
    readonly property alias sessionRepeater: sessionRepeater
    readonly property alias linkRepeater: linkRepeater
    readonly property alias formatsLabel: formatsLabel
    readonly property alias pluginsLabel: pluginsLabel
    readonly property alias incompleteLabel: incompleteLabel

    /// "12%" for a share of a cycle.
    function percent(share: real): string {
        return KI18n.i18nc("@info a share of the audio cycle, as a percentage", "%1%", Math.round(share * 100));
    }

    onExpandedChanged: {
        if (section.expanded) {
            Diagnostics.refresh();
        }
    }

    Timer {
        id: refreshTimer

        interval: 2000
        repeat: true
        running: section.expanded && section.visible
        onTriggered: Diagnostics.refresh()
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Button {
            id: copyButton

            text: KI18n.i18nc("@action:button put the diagnostics on the clipboard", "Copy Diagnostics")
            icon.name: "edit-copy"
            enabled: Diagnostics.ready
            onClicked: {
                Diagnostics.copyBundle();
                copiedLabel.visible = true;
                copiedTimer.restart();
            }
        }

        QQC2.Label {
            id: copiedLabel

            visible: false
            text: KI18n.i18nc("@info the diagnostics are now on the clipboard", "Copied")
            color: AppColors.muted
        }

        Timer {
            id: copiedTimer

            interval: 3000
            onTriggered: copiedLabel.visible = false
        }
    }

    QQC2.Label {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: AppColors.muted
        text: KI18n.i18nc("@info under the Copy Diagnostics button",
                          "What a bug report needs: versions, how each session is keeping up, the audio graph around Reverblabr and the last log lines. Your home folder, user name and computer name are left out.")
    }

    // --- how each session is keeping up ---

    Kirigami.Heading {
        level: 5
        wrapMode: Text.Wrap
        text: KI18n.i18nc("@title:group", "Sessions")
        Layout.fillWidth: true
    }

    QQC2.Label {
        visible: sessionRepeater.count === 0
        text: KI18n.i18nc("@info no session is open", "No session is open.")
        color: AppColors.muted
    }

    Repeater {
        id: sessionRepeater

        model: Diagnostics.sessions

        delegate: ColumnLayout {
            id: sessionRow

            required property var modelData
            /// The line that says it is not real-time, for a test.
            readonly property alias realtimeWarning: realtimeWarning

            Layout.fillWidth: true
            spacing: 0

            // A long name is whole on hover.
            ElidedLabel {
                Layout.fillWidth: true
                font: AppFonts.strong
                color: AppColors.text
                text: sessionRow.modelData.name
            }

            QQC2.Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: AppColors.muted
                text: sessionRow.modelData.running
                    ? KI18n.i18nc("@info a running session's load, for example 'Load 12% (peak 30%), 2 cycles late'",
                                  "Load %1 (peak %2), %3 cycles late",
                                  section.percent(sessionRow.modelData.dspLoad),
                                  section.percent(sessionRow.modelData.peakDspLoad),
                                  sessionRow.modelData.xruns)
                    : KI18n.i18nc("@info the session is not running", "Not running")
            }

            // **Said plainly when it is not real-time**: rtkit can
            // refuse, and a session without real-time priority is the
            // likeliest reason for dropouts nothing else explains.
            QQC2.Label {
                id: realtimeWarning

                Layout.fillWidth: true
                wrapMode: Text.Wrap
                visible: sessionRow.modelData.running && sessionRow.modelData.realtime === false
                color: AppColors.warningText
                text: KI18n.i18nc("@info the session's audio thread did not get real-time scheduling",
                                  "Running without real-time priority")
            }
        }
    }

    // --- what this system can do ---

    Kirigami.Heading {
        level: 5
        wrapMode: Text.Wrap
        text: KI18n.i18nc("@title:group what this system can record and host", "Formats and plugins")
        Layout.fillWidth: true
    }

    QQC2.Label {
        id: formatsLabel

        readonly property var missing: Diagnostics.recordingFormats.filter(format => !format.available)
                                                                   .map(format => format.id)

        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: AppColors.text
        text: formatsLabel.missing.length === 0
            ? KI18n.i18nc("@info every recording format can be written here", "Every recording format is available.")
            : KI18n.i18nc("@info the recording formats that cannot be written here, for example 'aac'",
                          "Recording formats not available: %1", formatsLabel.missing.join(", "))
    }

    QQC2.Label {
        id: pluginsLabel

        readonly property int total: Diagnostics.plugins.total ?? 0
        readonly property int blocked: (Diagnostics.plugins.blocked ?? []).length

        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: AppColors.text
        text: pluginsLabel.blocked > 0
            ? KI18n.i18ncp("@info plugins found by the scan, and how many it could not load",
                           "%1 plugin found, %2 left out because it crashed or hung",
                           "%1 plugins found, %2 left out because they crashed or hung",
                           pluginsLabel.total, pluginsLabel.blocked)
            : KI18n.i18ncp("@info plugins found by the scan", "%1 plugin found", "%1 plugins found",
                           pluginsLabel.total)
    }

    // A file the scan got only part of the way through.
    QQC2.Label {
        id: incompleteLabel

        readonly property int count: (Diagnostics.plugins.incomplete ?? []).length

        Layout.fillWidth: true
        visible: incompleteLabel.count > 0
        wrapMode: Text.Wrap
        color: AppColors.text
        text: KI18n.i18ncp("@info the plugin scan stopped part of the way through some files",
                           "%1 file was only partly scanned; some of its plugins may be missing",
                           "%1 files were only partly scanned; some of their plugins may be missing",
                           incompleteLabel.count)
    }

    // --- our part of the graph ---

    Kirigami.Heading {
        level: 5
        wrapMode: Text.Wrap
        text: KI18n.i18nc("@title:group the audio graph around Reverblabr's own nodes", "Connections")
        Layout.fillWidth: true
    }

    QQC2.Label {
        visible: linkRepeater.count === 0
        text: KI18n.i18nc("@info nothing of ours is connected in the audio graph", "Nothing is connected.")
        color: AppColors.muted
    }

    Repeater {
        id: linkRepeater

        model: Diagnostics.links

        // Cut in the middle, where the port names are alike, and whole on
        // hover.
        delegate: ElidedLabel {
            required property var modelData

            Layout.fillWidth: true
            elide: Text.ElideMiddle
            color: AppColors.text
            font: AppFonts.mono
            text: KI18n.i18nc("@info a link in the audio graph, from one port to another", "%1 → %2",
                              modelData.from, modelData.to)
        }
    }
}
