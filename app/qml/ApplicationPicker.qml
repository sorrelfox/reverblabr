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

// Naming an application: the one a block captures, and the ones a block
// leaves out.
//
// **It gives back a match rule, never a stream.** A stream lasts as long as
// the sound does and a session file outlives both, so what leaves this
// picker is the rule the engine matches by — `binary:firefox`,
// `flatpak:org.videolan.VLC` — which is the same thing whether the program
// is open, closed, or has not been installed yet.
//
// **Which is why it can be typed as well as chosen.** The applications that
// are playing are the easy half and they are offered first; a program that
// is not running cannot appear in a list of what is running, and it is
// precisely the one somebody wants to leave out *before* they start it.
//
// **And why the ones seen before are offered under them.** Typing works
// only for somebody who knows what a match rule looks like, which is the
// thing this exists to hide; so a program that has played on this machine
// is offered by name whether it is playing now or not.
//
// **One picker for both jobs, not two.** The list, the search and the rules
// are the same question asked twice; what differs is the words and one
// thing about which rules are acceptable, and both of those follow from
// `leavingOut`.
QQC2.Popup {
    id: picker

    /// Which job the picker is doing: leaving applications out of a block
    /// that takes everything, or choosing the one a block takes. It decides
    /// the wording, and it decides whether a node-scoped rule will do —
    /// see `storable()`.
    property bool leavingOut: false

    /// The rules already spoken for, which are not offered a second time.
    property var alreadyChosen: []

    /// A rule somebody settled on.
    signal chosen(rule: string)

    /// Whether a rule names an application in the terms a session file
    /// holds: a binary, a Flatpak id or a name the program calls itself.
    ///
    /// **A node-scoped rule does not.** `node:…` is one stream on one boot,
    /// and a block pointed at it would be pointed at nothing tomorrow. An
    /// exclusion is matched while the session runs, so there it is a rule
    /// like any other and the question is not asked.
    function storable(rule: string): bool {
        const fields = Inventory.applicationFields(rule);
        return fields.binary !== "" || fields.flatpakId !== "" || fields.name !== "";
    }

    /// Whether a rule is one this picker may hand back at all.
    function acceptable(rule: string): bool {
        return rule !== "" && !picker.alreadyChosen.includes(rule) && (picker.leavingOut || picker.storable(rule));
    }

    /// What is playing that is not spoken for already.
    readonly property var entries: {
        const live = Inventory.revision >= 0 ? Inventory.applications : [];
        const wanted = nameField.text.trim().toLowerCase();
        return live.filter(application => picker.acceptable(application.key)
                           && (wanted === "" || application.name.toLowerCase().includes(wanted)
                               || application.key.toLowerCase().includes(wanted)));
    }

    /// What played before and is not playing now. **The half of the list
    /// that is not in the inventory**: the program somebody came here to
    /// name is usually one they have already closed, and offering it by
    /// name is only possible because something wrote the name down.
    readonly property var recentEntries: {
        const seen = RecentApplications.entries;
        const wanted = nameField.text.trim().toLowerCase();
        return seen.filter(entry => picker.acceptable(entry.key)
                           && !(Inventory.revision >= 0 && Inventory.hasApplication(entry.key))
                           && (wanted === "" || entry.name.toLowerCase().includes(wanted)
                               || entry.key.toLowerCase().includes(wanted)));
    }

    /// What typing would name, as a rule, or empty when it would name
    /// something this picker cannot hand back.
    readonly property string typedRule: {
        const rule = Inventory.applicationRule(nameField.text);
        return picker.leavingOut || picker.storable(rule) ? rule : "";
    }
    /// Whether typing is worth offering: there is something to make a rule
    /// out of, and it is not one of the entries above or spoken for.
    readonly property bool offersTyped: picker.acceptable(picker.typedRule)
        && !picker.entries.some(application => application.key === picker.typedRule)

    /// The parts, so that a test drives what a person drives.
    readonly property alias field: nameField
    readonly property alias running: runningApplications
    readonly property alias recent: recentApplications
    readonly property alias typedEntry: typedDelegate

    modal: true
    focus: true
    padding: Kirigami.Units.smallSpacing
    implicitWidth: Kirigami.Units.gridUnit * 16

    onOpened: {
        nameField.clear();
        nameField.forceActiveFocus();
    }

    function take(rule: string) {
        if (rule === "") {
            return;
        }
        picker.chosen(rule);
        picker.close();
    }

    /// The line under an application's name: how many sounds it has going
    /// at the moment, and the Flatpak id when it has one — which is what
    /// tells a sandboxed installation from a packaged one by the same name.
    function detail(application: var): string {
        const streams = application.streamCount ?? 0;
        const flatpakId = application.flatpakId ?? "";
        return flatpakId !== ""
            ? KI18n.i18ncp("@info:listbox a sandboxed application's id, and how many sounds it is playing",
                           "%2 · %1 stream", "%2 · %1 streams", streams, flatpakId)
            : KI18n.i18ncp("@info:listbox how many sounds an application is playing",
                           "%1 stream", "%1 streams", streams);
    }

    // The popup's own content: its width follows the popup, and the popup's
    // height follows however many applications there are to offer.
    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        QQC2.TextField {
            id: nameField

            Layout.fillWidth: true
            placeholderText: KI18n.i18nc("@info:placeholder in an application picker",
                                         "Search, or name a program")
            Accessible.name: picker.leavingOut
                ? KI18n.i18nc("@label:textbox", "Application to leave out")
                : KI18n.i18nc("@label:textbox", "Application to capture")
            // Enter takes what was typed, which is the whole point of being
            // able to type: the program is not running, so there is nothing
            // in the list below to press instead.
            onAccepted: picker.take(picker.typedRule)
        }

        QQC2.Label {
            Layout.fillWidth: true
            visible: picker.entries.length === 0 && picker.recentEntries.length === 0
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: AppColors.muted
            font: AppFonts.small
            text: !Inventory.ready
                ? KI18n.i18nc("@info:placeholder while the engine is still answering",
                              "Reading what is playing…")
                : picker.leavingOut
                ? KI18n.i18nc("@info:placeholder nothing is playing that could be listed",
                              "Nothing else is playing. Type a program's name to leave it out anyway.")
                : KI18n.i18nc("@info:placeholder nothing is playing that could be captured",
                              "Nothing is playing. Type a program's name to capture it when it starts.")
        }

        // **The two groups are labelled only when there are two.** One
        // heading over one list is a heading that tells nobody anything.
        QQC2.Label {
            Layout.fillWidth: true
            visible: picker.entries.length > 0 && picker.recentEntries.length > 0
            text: KI18n.i18nc("@title:group applications that are playing at the moment", "Playing now")
            color: AppColors.muted
            font: AppFonts.small
        }

        Repeater {
            id: runningApplications

            model: picker.entries

            delegate: QQC2.ItemDelegate {
                id: applicationDelegate

                required property var modelData

                Layout.fillWidth: true
                // The row draws two lines, so there is no `text` for a
                // screen reader to read: it is given the same two facts.
                Accessible.name: applicationDelegate.modelData.name
                Accessible.description: picker.detail(applicationDelegate.modelData)
                onClicked: picker.take(applicationDelegate.modelData.key)

                contentItem: RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Icon {
                        // The program's own icon where the desktop file gave
                        // one. Empty is not a broken picture: Kirigami draws
                        // nothing.
                        source: applicationDelegate.modelData.icon ?? ""
                        implicitWidth: Kirigami.Units.iconSizes.small
                        implicitHeight: Kirigami.Units.iconSizes.small
                        Accessible.ignored: true
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        QQC2.Label {
                            Layout.fillWidth: true
                            text: applicationDelegate.modelData.name
                            color: AppColors.text
                            elide: Text.ElideRight
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            text: picker.detail(applicationDelegate.modelData)
                            color: AppColors.muted
                            font: AppFonts.small
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        QQC2.Label {
            Layout.fillWidth: true
            visible: picker.recentEntries.length > 0
            text: KI18n.i18nc("@title:group applications that played before but are not playing now",
                              "Recently seen")
            color: AppColors.muted
            font: AppFonts.small
        }

        Repeater {
            id: recentApplications

            model: picker.recentEntries

            // One line rather than the two above, because there is nothing
            // to put on a second: nothing of it is playing, so there are no
            // streams to count and no icon the desktop file gave us.
            delegate: QQC2.ItemDelegate {
                id: recentDelegate

                required property var modelData

                Layout.fillWidth: true
                icon.name: "document-open-recent"
                text: recentDelegate.modelData.name
                onClicked: picker.take(recentDelegate.modelData.key)
            }
        }

        QQC2.ItemDelegate {
            id: typedDelegate

            Layout.fillWidth: true
            visible: picker.offersTyped
            icon.name: "list-add"
            text: picker.leavingOut
                ? KI18n.i18nc("@action:button leave out a program that is not playing, by name",
                              "Leave out “%1”", Inventory.applicationName(picker.typedRule))
                : KI18n.i18nc("@action:button capture a program that is not playing, by name",
                              "Capture “%1”", Inventory.applicationName(picker.typedRule))
            onClicked: picker.take(picker.typedRule)
        }
    }
}
