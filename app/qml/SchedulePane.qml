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

// The sidebar's Schedule tab: the session's timers, each with its switch,
// and the dialog that makes and changes them.
//
// **The timers are the session's**, kept in its file and changed by edits
// like everything else in it, so undo takes a change back. The background
// engine turns each that is on into a systemd user timer, which starts the
// session at its time whether or not this window is open; a window running
// an engine of its own says that nothing will start it.
FocusScope {
    id: pane

    readonly property alias list: list
    readonly property alias addButton: addButton
    readonly property alias emptyMessage: empty
    readonly property alias needsBackground: needsBackground
    readonly property alias dialog: dialog

    /// When a timer comes round, as a person says it: "Every day at 07:30".
    function whenText(timer: var): string {
        const at = Qt.locale().toString(new Date(2000, 0, 1, Number(timer.time.slice(0, 2)), Number(timer.time.slice(3, 5))),
                                        Locale.ShortFormat);
        switch (timer.repeat) {
        case "once":
            return KI18n.i18nc("@info a timer that runs once: %1 is a date, %2 a time", "%1 at %2",
                               Qt.locale().toString(Date.fromLocaleString(Qt.locale("C"), timer.date, "yyyy-MM-dd"),
                                                    Locale.ShortFormat), at);
        case "daily":
            return KI18n.i18nc("@info %1 is a time", "Every day at %1", at);
        case "weekdays":
            return KI18n.i18nc("@info %1 is a time", "Monday to Friday at %1", at);
        default: {
            const codes = ["mon", "tue", "wed", "thu", "fri", "sat", "sun"];
            const names = (timer.days ?? []).map(code => Qt.locale().dayName((codes.indexOf(code) + 1) % 7,
                                                                                Locale.ShortFormat));
            return KI18n.i18nc("@info %1 is a list of days of the week, %2 a time", "%1 at %2",
                               names.join(KI18n.i18nc("@info the separator between days of the week", ", ")), at);
        }
        }
    }

    /// How long it runs, and when it next starts the session.
    function detailText(timer: var): string {
        const hours = Math.floor(timer.duration / 3600);
        const minutes = Math.floor((timer.duration % 3600) / 60);
        let length;
        if (hours > 0 && minutes > 0) {
            length = KI18n.i18nc("@info a duration: %1 is hours, %2 minutes", "%1 h %2 min", hours, minutes);
        } else if (hours > 0) {
            length = KI18n.i18ncp("@info a duration", "%1 hour", "%1 hours", hours);
        } else if (minutes > 0) {
            length = KI18n.i18ncp("@info a duration", "%1 minute", "%1 minutes", minutes);
        } else {
            length = KI18n.i18ncp("@info a duration", "%1 second", "%1 seconds", timer.duration);
        }
        let next;
        if (!timer.enabled) {
            next = KI18n.i18nc("@info a timer that is switched off", "off");
        } else if (timer.nextRun !== undefined) {
            next = KI18n.i18nc("@info when a timer next starts the session; %1 is a date and time", "next %1",
                               Qt.locale().toString(timer.nextRun, Locale.ShortFormat));
        } else {
            next = KI18n.i18nc("@info a timer that will not start the session again", "not again");
        }
        const done = timer.quitApplications
            ? KI18n.i18nc("@info a timer quits the captured applications when it is done", ", then quits the apps")
            : "";
        return KI18n.i18nc("@info a timer: %1 is how long it runs, %2 when it next does, %3 what it does after",
                           "For %1%3 · %2", length, next, done);
    }

    function addTimer() {
        dialog.openFor(SessionModel.newTimer(), true);
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: AppMetrics.libraryInset
        spacing: AppMetrics.hairSpacing

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: AppMetrics.libraryInset
            Layout.topMargin: AppMetrics.followsSystem ? Kirigami.Units.smallSpacing : AppMetrics.libraryInset * 2

            QQC2.Label {
                Layout.fillWidth: true
                text: KI18n.i18nc("@title:group the timers in the Schedule tab", "Timers")
                font: AppFonts.libraryHeading
                color: AppColors.muted
                Accessible.role: Accessible.Heading
            }

            QQC2.ToolButton {
                id: addButton

                enabled: AppShell.hasSession
                text: KI18n.i18nc("@action:button make a new timer", "Add Timer…")
                display: QQC2.AbstractButton.IconOnly
                icon.name: "list-add"
                icon.source: AppMetrics.followsSystem ? "" : AppGlyphs.add
                QQC2.ToolTip.text: addButton.text
                QQC2.ToolTip.visible: addButton.hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                onClicked: pane.addTimer()
            }
        }

        QQC2.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            visible: list.count > 0

            ListView {
                id: list

                clip: true
                model: SessionModel.timers
                spacing: AppMetrics.hairSpacing
                keyNavigationEnabled: true
                activeFocusOnTab: true
                focus: true
                Accessible.role: Accessible.List
                Accessible.name: KI18n.i18nc("@info the list of timers in the Schedule tab", "Timers")

                delegate: QQC2.ItemDelegate {
                    id: row

                    required property var modelData
                    required property int index

                    width: ListView.view.width
                    highlighted: ListView.isCurrentItem
                    Accessible.name: pane.whenText(row.modelData)
                    Accessible.description: pane.detailText(row.modelData)
                    onClicked: list.currentIndex = row.index
                    onDoubleClicked: dialog.openFor(row.modelData, false)

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.Switch {
                            objectName: "timerSwitch"
                            checked: row.modelData.enabled
                            Accessible.name: KI18n.i18nc("@option:check switch a timer on or off", "On")
                            onToggled: SessionModel.setTimerEnabled(row.modelData.id, checked)
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            QQC2.Label {
                                Layout.fillWidth: true
                                text: pane.whenText(row.modelData)
                                font: AppFonts.libraryTitle
                                color: row.modelData.enabled ? AppColors.text : AppColors.muted
                                elide: Text.ElideRight
                            }

                            QQC2.Label {
                                Layout.fillWidth: true
                                text: pane.detailText(row.modelData)
                                color: AppColors.muted
                                font: AppFonts.libraryDetail
                                elide: Text.ElideRight
                            }
                        }

                        QQC2.ToolButton {
                            objectName: "editTimer"
                            text: KI18n.i18nc("@action:button", "Edit Timer…")
                            display: QQC2.AbstractButton.IconOnly
                            icon.name: "document-edit"
                            QQC2.ToolTip.text: text
                            QQC2.ToolTip.visible: hovered
                            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                            onClicked: dialog.openFor(row.modelData, false)
                        }

                        // **No question asked**: deleting a timer is an edit,
                        // and undo puts it back.
                        QQC2.ToolButton {
                            objectName: "deleteTimer"
                            text: KI18n.i18nc("@action:button", "Delete Timer")
                            display: QQC2.AbstractButton.IconOnly
                            icon.name: "edit-delete"
                            QQC2.ToolTip.text: text
                            QQC2.ToolTip.visible: hovered
                            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                            onClicked: SessionModel.removeTimer(row.modelData.id)
                        }
                    }
                }

                Keys.onDeletePressed: if (list.currentIndex >= 0) {
                    SessionModel.removeTimer(SessionModel.timers[list.currentIndex].id);
                }
                Keys.onReturnPressed: if (list.currentIndex >= 0) {
                    dialog.openFor(SessionModel.timers[list.currentIndex], false);
                }
            }
        }

        // **An empty list says what would fill it.**
        PlaceholderMessage {
            id: empty

            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: list.count === 0
            text: KI18n.i18nc("@info:placeholder the Schedule tab of a session with no timers",
                              "No timers. Add one to start this session at a set time, even with Reverblabr closed.")
        }

        QQC2.Label {
            id: needsBackground

            Layout.fillWidth: true
            Layout.margins: AppMetrics.libraryInset
            visible: !AppShell.engineInBackground
            text: KI18n.i18nc("@info under the Schedule tab's timers",
                              "Timers need Reverblabr's background engine, which isn't installed here. The session keeps them.")
            color: AppColors.muted
            wrapMode: Text.WordWrap
        }
    }

    // **Over the whole window**, not the sidebar: a dialog laid out
    // inside a pane the person can resize follows it about.
    TimerDialog {
        id: dialog

        parent: QQC2.Overlay.overlay
    }
}
