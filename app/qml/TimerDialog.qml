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

// One timer, made or changed: when it starts the session, how often, for
// how long, and whether the applications the session captures are quit
// when it is done.
//
// **Nothing is sent until Save**, and then as one edit, so one undo takes
// the whole change back. Save waits until the session would take the timer
// (SessionModel.timerIsValid): a timer on chosen days with no day chosen,
// or a date that is not one, is not a timer.
AppDialog {
    id: dialog

    /// The timer being edited, as the file holds it. Set by openFor().
    property var timer: ({})
    /// Whether it is a new one, for the title.
    property bool adding: true

    /// The days of the week as the file names them, Monday first, which is
    /// also how QML's Locale numbers them after Sunday's 0.
    readonly property var dayCodes: ["mon", "tue", "wed", "thu", "fri", "sat", "sun"]
    /// The codes, in the order this person's week starts.
    readonly property var weekOrder: {
        const first = Qt.locale().firstDayOfWeek; // 0 is Sunday
        const order = [];
        for (let offset = 0; offset < 7; ++offset) {
            const day = (first + offset) % 7;
            order.push(dialog.dayCodes[(day + 6) % 7]);
        }
        return order;
    }
    /// The four the file holds, in the order the box lists them.
    readonly property var repeatCodes: ["once", "daily", "weekdays", "days"]

    /// The timer as the fields now say it.
    readonly property var draft: {
        const draft = Object.assign({}, dialog.timer);
        draft.repeat = dialog.repeatCodes[repeatBox.currentIndex] ?? "daily";
        draft.time = dialog.twoDigits(hourBox.value) + ":" + dialog.twoDigits(minuteBox.value);
        draft.duration = hoursBox.value * 3600 + minutesBox.value * 60;
        draft.quitApplications = quitBox.checked;
        delete draft.date;
        delete draft.days;
        delete draft.nextRun;
        if (draft.repeat === "once") {
            draft.date = dateField.text;
        }
        if (draft.repeat === "days") {
            draft.days = dialog.dayCodes.filter(code => dialog.chosenDays.indexOf(code) >= 0);
        }
        return draft;
    }
    /// The days chosen for a timer on chosen days.
    property var chosenDays: []

    readonly property alias repeatBox: repeatBox
    readonly property alias dateField: dateField
    readonly property alias hourBox: hourBox
    readonly property alias minuteBox: minuteBox
    readonly property alias hoursBox: hoursBox
    readonly property alias minutesBox: minutesBox
    readonly property alias quitBox: quitBox
    readonly property alias quitPrompt: quitPrompt
    readonly property alias saveAction: saveAction

    /// Opens the dialog on \a timer: a new one from SessionModel.newTimer(),
    /// or one of SessionModel.timers.
    function openFor(timer: var, adding: bool) {
        dialog.timer = timer;
        dialog.adding = adding;
        repeatBox.currentIndex = Math.max(0, dialog.repeatCodes.indexOf(timer.repeat));
        const [hour, minute] = (timer.time ?? "00:00").split(":");
        hourBox.value = Number(hour);
        minuteBox.value = Number(minute);
        // A new one-off is for today, which is the likeliest day and shows
        // the format to type.
        dateField.text = timer.date ?? Qt.formatDate(new Date(), "yyyy-MM-dd");
        dialog.chosenDays = timer.days ?? [];
        hoursBox.value = Math.floor((timer.duration ?? 3600) / 3600);
        minutesBox.value = Math.floor(((timer.duration ?? 3600) % 3600) / 60);
        quitBox.checked = timer.quitApplications === true;
        dialog.open();
    }

    function twoDigits(value: int): string {
        return value < 10 ? "0" + value : String(value);
    }

    function toggleDay(code: string, on: bool) {
        const days = dialog.chosenDays.filter(day => day !== code);
        if (on) {
            days.push(code);
        }
        dialog.chosenDays = days;
    }

    title: dialog.adding ? KI18n.i18nc("@title:window", "Add Timer") : KI18n.i18nc("@title:window", "Edit Timer")
    preferredWidth: Kirigami.Units.gridUnit * 26
    standardButtons: Kirigami.Dialog.Cancel
    customFooterActions: [
        Kirigami.Action {
            id: saveAction

            text: KI18n.i18nc("@action:button keep the timer as set", "Save")
            icon.name: "document-save"
            enabled: SessionModel.timerIsValid(dialog.draft)
            onTriggered: {
                SessionModel.setTimer(dialog.draft);
                dialog.close();
            }
        }
    ]

    Kirigami.FormLayout {
        QQC2.ComboBox {
            id: repeatBox

            Kirigami.FormData.label: KI18n.i18nc("@label:listbox how often a timer starts the session", "Repeat:")
            model: [
                KI18n.i18nc("@item:inlistbox a timer that runs once", "Once"),
                KI18n.i18nc("@item:inlistbox a timer that runs every day", "Every day"),
                KI18n.i18nc("@item:inlistbox a timer that runs on working days", "Monday to Friday"),
                KI18n.i18nc("@item:inlistbox a timer that runs on the days chosen below", "On these days")
            ]
        }

        QQC2.TextField {
            id: dateField

            Kirigami.FormData.label: KI18n.i18nc("@label:textbox the day a timer runs once", "Date:")
            visible: repeatBox.currentIndex === 0
            inputMask: "9999-99-99"
            // ISO, as the file holds it: a date written the same way in
            // every language is one nobody reads as the wrong day.
            placeholderText: KI18n.i18nc("@info:placeholder a date: year, month, day", "YYYY-MM-DD")
            Accessible.name: KI18n.i18nc("@label:textbox", "Date, as year, month and day")
        }

        // The days in the order this person's week runs, each a letter
        // with its whole name on hover and for a screen reader.
        RowLayout {
            Kirigami.FormData.label: KI18n.i18nc("@label the days of the week a timer runs on", "Days:")
            visible: repeatBox.currentIndex === 3
            spacing: 0

            Repeater {
                model: dialog.weekOrder

                QQC2.ToolButton {
                    required property string modelData

                    objectName: "day-" + modelData
                    checkable: true
                    checked: dialog.chosenDays.indexOf(modelData) >= 0
                    text: Qt.locale().dayName((dialog.dayCodes.indexOf(modelData) + 1) % 7, Locale.NarrowFormat)
                    Accessible.name: Qt.locale().dayName((dialog.dayCodes.indexOf(modelData) + 1) % 7,
                                                         Locale.LongFormat)
                    QQC2.ToolTip.text: Accessible.name
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    onToggled: dialog.toggleDay(modelData, checked)
                }
            }
        }

        RowLayout {
            Kirigami.FormData.label: KI18n.i18nc("@label the time a timer starts the session", "Starts at:")
            spacing: Kirigami.Units.smallSpacing

            QQC2.SpinBox {
                id: hourBox

                from: 0
                to: 23
                editable: true
                textFromValue: value => dialog.twoDigits(value)
                Accessible.name: KI18n.i18nc("@label:spinbox", "Hour")
            }

            QQC2.Label {
                text: ":"
                color: AppColors.text
            }

            QQC2.SpinBox {
                id: minuteBox

                from: 0
                to: 59
                editable: true
                wrap: true
                textFromValue: value => dialog.twoDigits(value)
                Accessible.name: KI18n.i18nc("@label:spinbox", "Minute")
            }
        }

        RowLayout {
            Kirigami.FormData.label: KI18n.i18nc("@label how long a timer keeps the session running", "Runs for:")
            spacing: Kirigami.Units.smallSpacing

            QQC2.SpinBox {
                id: hoursBox

                from: 0
                // A week, the longest a timer runs.
                to: 168
                editable: true
                Accessible.name: KI18n.i18nc("@label:spinbox", "Hours")
            }

            QQC2.Label {
                text: KI18n.i18nc("@label after a number of hours", "h")
                color: AppColors.text
            }

            QQC2.SpinBox {
                id: minutesBox

                from: 0
                to: 59
                editable: true
                Accessible.name: KI18n.i18nc("@label:spinbox", "Minutes")
            }

            QQC2.Label {
                text: KI18n.i18nc("@label after a number of minutes", "min")
                color: AppColors.text
            }
        }

        // **Asked before it is set**: quitting somebody's applications is
        // not something to find out about when the timer ends.
        QQC2.CheckBox {
            id: quitBox

            Kirigami.FormData.label: KI18n.i18nc("@label what happens when the timer's run is over", "When done:")
            text: KI18n.i18nc("@option:check", "Quit the applications this session captures")
            onToggled: if (quitBox.checked) {
                quitBox.checked = false;
                quitPrompt.open();
            }
        }

        // **What systemd is told**, for somebody who wants to check it in
        // `systemctl --user list-timers`.
        QQC2.Label {
            Kirigami.FormData.label: KI18n.i18nc("@label the timer as the system's scheduler has it", "Calendar:")
            text: SessionModel.timerCalendar(dialog.draft) || "—"
            color: AppColors.muted
            font: AppFonts.mono
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
    }

    AppPromptDialog {
        id: quitPrompt

        readonly property alias quitAction: quitAction

        parent: QQC2.Overlay.overlay
        title: KI18n.i18nc("@title:window", "Quit Applications When Done")
        subtitle: KI18n.i18nc("@info",
                              "When this timer's run is over, Reverblabr will ask every application the session captures to quit. Anything unsaved in them may be lost.")
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                id: quitAction

                text: KI18n.i18nc("@action:button", "Quit Them When Done")
                icon.name: "application-exit"
                onTriggered: {
                    quitBox.checked = true;
                    quitPrompt.close();
                }
            }
        ]
    }
}
