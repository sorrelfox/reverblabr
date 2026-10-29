// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.ki18n

// The line along the bottom: what is being recorded, what the graph is
// running at, how hard it is working, and what to try next.
//
// **A dash, not a zero.** A session that is not running has no load and no
// rate, and showing 0 % would be a measurement of something. The dash says
// there is nothing to measure.
QQC2.ToolBar {
    id: statusBar

    /// What the recorder is doing. Empty until there is a recorder (M2);
    /// the slot is here so the line does not change shape when there is.
    property string recordingText: ""

    /// The hint at the right-hand end — something to try, rather than
    /// something that has happened. **It follows the connection mode**:
    /// it used to name Ctrl-drag whatever the session was doing,
    /// which in an automatic session is a hint about the wrong mode.
    property string hintText: SessionModel.autoConnect
        ? KI18n.i18nc("@info:status a hint about how to use the canvas, when blocks connect themselves",
                      "Automatic connections: new blocks connect themselves")
        : KI18n.i18nc("@info:status a hint about how to use the canvas, when blocks are connected by hand",
                      "Manual connections: Ctrl-drag")

    readonly property string dash: "—"

    /// What the engine is talking to, and whether it can. **A name from the
    /// engine, not a word from here**: the interface has no connection of
    /// its own and no business claiming what is at the other end of one.
    /// The rate and the cycle, for a test.
    readonly property alias rateLabel: rate

    readonly property string serverName: AudioSystem.serverName.length > 0
        ? AudioSystem.serverName
        : KI18n.i18nc("@info:status the audio server, before it has said what it is called", "Audio system")
    readonly property string connectionText: AudioSystem.connected
        ? KI18n.i18nc("@info:status the engine is talking to the audio system", "connected")
        : AudioSystem.state === "connecting"
            ? KI18n.i18nc("@info:status the engine is opening a connection", "connecting")
            : KI18n.i18nc("@info:status the engine cannot reach the audio system", "not running")

    // A hairline between the groups, so that four short readings do not
    // read as one long sentence. **Only while following the system**: the
    // prototype puts sixteen pixels of nothing between them.
    component Divider: Kirigami.Separator {
        visible: AppMetrics.followsSystem
        Layout.fillHeight: true
        Layout.topMargin: Kirigami.Units.smallSpacing
        Layout.bottomMargin: Kirigami.Units.smallSpacing
    }

    /// The window's corner, which the bar's own bottom corners follow when
    /// the window draws its own frame.
    property real frameRadius: 0

    // The prototype's 30 pixels in the Reverblabr appearance.
    Binding on implicitHeight {
        when: !AppMetrics.followsSystem
        value: AppMetrics.statusBarHeight
    }

    // **The window's bottom corners are the status bar's** in a window with
    // no frame, which is transparent outside its rounded shape.
    Binding {
        target: statusBar
        property: "background"
        when: statusBar.frameRadius > 0
        value: Kirigami.ShadowedRectangle {
            color: AppColors.window
            corners.bottomLeftRadius: statusBar.frameRadius
            corners.bottomRightRadius: statusBar.frameRadius

            Rectangle {
                width: parent.width
                height: 1
                color: AppColors.border
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        // The window's margin at both ends, which the readings and
        // the hint used to start and end right against; the prototype's
        // twelve in the Reverblabr appearance.
        anchors.leftMargin: AppMetrics.followsSystem ? Kirigami.Units.largeSpacing : AppMetrics.panelPadding
        anchors.rightMargin: AppMetrics.followsSystem ? Kirigami.Units.largeSpacing : AppMetrics.panelPadding
        spacing: AppMetrics.followsSystem ? Kirigami.Units.largeSpacing : AppMetrics.wideSpacing

        // **All of it to the right in the prototype**: the room is
        // at the start of the row there, and before the hint while
        // following the system.
        Item {
            visible: !AppMetrics.followsSystem
            Layout.fillWidth: true
        }

        QQC2.Label {
            text: statusBar.recordingText
            color: AppColors.muted
            font: AppFonts.statusBar
            visible: text.length > 0
            elide: Text.ElideRight
            Layout.maximumWidth: statusBar.width / 3
        }

        Divider {
            visible: statusBar.recordingText.length > 0 && AppMetrics.followsSystem
        }

        // The rate and the cycle the graph is really running at, which are
        // the audio system's to choose and not ours to ask for. The cycle
        // is given in frames and in milliseconds, because one of those is
        // what a person means by latency and the other is what the audio
        // system is actually counting.
        QQC2.Label {
            id: rate

            // **The session's figures while one runs, the graph's while none
            // does**: the audio system has a rate and a cycle either
            // way, and a dash is for when neither has said.
            readonly property int shownRate: AppShell.hasStatus ? AppShell.sampleRate : AudioSystem.rate
            readonly property int shownQuantum: AppShell.hasStatus ? AppShell.quantum : AudioSystem.quantum
            readonly property real shownCycleMs: AppShell.hasStatus ? AppShell.latencyMs : AudioSystem.cycleMs

            text: rate.shownRate > 0 && rate.shownQuantum > 0
                ? KI18n.i18nc("@info:status rate, cycle and what that is worth in time",
                              "%1 kHz · %2 · %3 ms", Math.round(rate.shownRate / 100) / 10, rate.shownQuantum,
                              Math.round(rate.shownCycleMs * 10) / 10)
                : KI18n.i18nc("@info:status nothing is running, so there is no rate to show",
                              "%1 kHz", statusBar.dash)
            color: AppColors.muted
            font: AppFonts.mono
            QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip",
                                           "The rate the graph is running at, and how long one cycle of it lasts")
            QQC2.ToolTip.visible: rateHover.hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

            HoverHandler {
                id: rateHover
            }
        }

        // **How late the session's own processing makes the sound**:
        // a limiter's lookahead and the like, on top of the cycle. Shown
        // only when there is some, which it is also telling the audio
        // system so that a recorder downstream can line its tracks up.
        Divider {
            visible: processing.visible && AppMetrics.followsSystem
        }

        QQC2.Label {
            id: processing

            visible: AppShell.hasStatus && AppShell.processingLatencyMs > 0
            text: KI18n.i18nc("@info:status how much later the session's processing makes the sound",
                              "+%1 ms processing", Math.round(AppShell.processingLatencyMs * 10) / 10)
            color: AppColors.muted
            font: AppFonts.mono
            QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip",
                                           "How much later the session's blocks make the sound, on its longest path. Paths that meet are lined up with it, and programs that record the session are told.")
            QQC2.ToolTip.visible: processingHover.hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

            HoverHandler {
                id: processingHover
            }
        }

        Divider {}

        QQC2.Label {
            id: load

            text: AppShell.hasStatus
                ? KI18n.i18nc("@info:status how much of a cycle the audio is taking", "DSP %1 %",
                              Math.round(AppShell.dspLoad * 100))
                : KI18n.i18nc("@info:status nothing is running, so there is no load to show", "DSP %1",
                              statusBar.dash)
            color: AppShell.dspLoad > 0.8 ? AppColors.dangerText : AppColors.muted
            font: AppFonts.mono
            QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip",
                                           "The largest share of one audio cycle the session has needed")
            QQC2.ToolTip.visible: loadHover.hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

            HoverHandler {
                id: loadHover
            }
        }

        Divider {}

        // **Shown whether or not there are any.** A count that appears only
        // when something has gone wrong is a count nobody knows to look for.
        QQC2.Label {
            id: xruns

            text: AppShell.hasStatus
                ? KI18n.i18ncp("@info:status how many audio cycles were missed",
                               "%1 xrun", "%1 xruns", AppShell.xruns)
                : KI18n.i18nc("@info:status nothing is running, so there is nothing to count", "%1 xruns",
                              statusBar.dash)
            color: AppShell.xruns > 0 ? AppColors.warningText : AppColors.muted
            font: AppFonts.mono
            QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip",
                                           "Audio cycles the session did not finish in time. Any at all can be heard.")
            QQC2.ToolTip.visible: xrunHover.hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

            HoverHandler {
                id: xrunHover
            }
        }

        Divider {}

        // **Always here, whether or not anything is wrong.** A reading that
        // appears only when the audio system has gone is a reading nobody
        // knows to look for; this way the place it will say so is a place
        // somebody has already seen.
        QQC2.Label {
            id: server

            text: KI18n.i18nc("@info:status the audio server and the connection to it, 'PipeWire · connected'",
                              "%1 · %2", statusBar.serverName, statusBar.connectionText)
            color: AudioSystem.connected ? AppColors.muted : AppColors.warningText
            font: AppFonts.statusBar
            elide: Text.ElideRight
            Layout.maximumWidth: statusBar.width / 3
        }

        Item {
            visible: AppMetrics.followsSystem
            Layout.fillWidth: true
        }

        QQC2.Label {
            text: statusBar.hintText
            color: AppColors.muted
            font: AppFonts.statusBar
            elide: Text.ElideRight
            Layout.maximumWidth: statusBar.width / 3
        }
    }
}
