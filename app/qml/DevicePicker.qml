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
import org.kde.ki18n

// Which device a block plays to, or records from.
//
// **The system default comes first, and it is a reference rather than a
// name.** Most people want whatever they have just plugged in to be what
// they hear, and a block that names the device by hand stops following
// them; so the first entry writes the reference and every other entry
// writes a `node.name`.
//
// **The list is live.** A device appearing or going away changes what the
// engine sends, and that is all it takes: nothing here is remembered
// between inventories, so a headset plugged in while the panel is open is
// in the picker a moment later.
//
// **Virtual devices come last, under a heading of their own**. A
// virtual output applications are sent to is recorded from here, and a
// virtual microphone is played into from here; neither is a card, and a
// list that mixed them in with the cards would make the one a person set
// up for this hard to find. The heading is a row of the list that cannot be
// chosen.
RowLayout {
    id: picker

    /// The block being pointed at something.
    required property string blockId
    /// Whether it plays or records, which decides both which half of the
    /// inventory is offered and what the first entry is called.
    required property bool output
    /// A device the block names that is not in the lists. It is offered all
    /// the same, so that opening the panel of a block whose interface was
    /// unplugged does not quietly re-point it at something else.
    required property string chosen

    readonly property alias combo: combo
    readonly property alias label: label
    /// How wide the label's column is, so that a panel can line it up with
    /// the labels under it.
    property real labelWidth: AppMetrics.panelLabelWidth

    /// What the picker offers: the system default, then every card, then
    /// the virtual devices under their heading. The heading is the one
    /// entry with `heading` set, and it has no value.
    readonly property var entries: {
        const first = {
            value: Inventory.defaultDevice,
            label: picker.output
                ? KI18n.i18nc("@item:inlistbox follows whatever the system output is", "Default output")
                : KI18n.i18nc("@item:inlistbox follows whatever the system input is", "Default input")
        };
        const live = (Inventory.revision >= 0 ? (picker.output ? Inventory.outputs : Inventory.inputs) : []);
        const entryOf = device => ({
            value: device.node,
            label: device.description !== "" ? device.description : device.node
        });
        let entries = [first].concat(live.filter(device => !device.virtual).map(entryOf));
        const virtualDevices = live.filter(device => device.virtual);
        if (virtualDevices.length > 0) {
            entries.push({
                value: undefined,
                heading: true,
                label: KI18n.i18nc("@item:inlistbox a heading over the devices no hardware stands behind",
                                   "Virtual devices")
            });
            entries = entries.concat(virtualDevices.map(entryOf));
        }
        if (picker.chosen !== "" && !entries.some(entry => entry.value === picker.chosen)) {
            entries.push({
                value: picker.chosen,
                label: KI18n.i18nc("@item:inlistbox a device that is not plugged in, by its node name",
                                   "%1 (unavailable)", picker.chosen)
            });
        }
        return entries;
    }

    spacing: AppMetrics.labelSpacing

    // **Muted and against its box**, as the designs draw a form's labels:
    // the value is what is read, the label only says which it is.
    QQC2.Label {
        id: label

        text: KI18n.i18nc("@label:listbox the device a block plays to or records from", "Audio device")
        color: AppColors.muted
        elide: Text.ElideRight
        // A label column is fixed; a longer word goes onto a second line
        // rather than out of sight.
        wrapMode: Text.Wrap
        maximumLineCount: 2
        horizontalAlignment: Text.AlignRight
        Layout.preferredWidth: picker.labelWidth
    }

    QQC2.ComboBox {
        id: combo

        Layout.fillWidth: true
        model: picker.entries
        textRole: "label"
        valueRole: "value"
        Accessible.name: picker.output
            ? KI18n.i18nc("@label:listbox", "Output device")
            : KI18n.i18nc("@label:listbox", "Input device")
        // **A `node.name`, or the default reference.** Never the position
        // in the list: the list is live, and what was third a moment ago is
        // second as soon as somebody unplugs something.
        //
        // **The heading is stepped over.** A pointer cannot choose it, but
        // the arrow keys move through every row; landing on it goes on to
        // the next device in the direction the key was going.
        onActivated: index => {
            let chosen = index;
            if (picker.entries[chosen].heading === true) {
                const was = picker.entries.findIndex(entry => entry.value === picker.chosen);
                chosen = was >= 0 && was < index ? index + 1 : index - 1;
                combo.currentIndex = chosen;
            }
            SessionModel.setParam(picker.blockId, "device", picker.entries[chosen].value);
        }

        // **Drawn here for the heading's sake**: a heading is a muted line
        // that cannot be chosen, and every other row is what the style draws.
        delegate: QQC2.ItemDelegate {
            id: row

            required property var model
            required property int index
            readonly property bool heading: row.model.heading === true

            width: ListView.view ? ListView.view.width : implicitWidth
            text: row.model.label
            enabled: !row.heading
            // A heading is not a disabled device; it is not a device at all.
            opacity: 1
            highlighted: !row.heading && combo.highlightedIndex === row.index
            hoverEnabled: combo.hoverEnabled
            font.weight: row.heading || combo.currentIndex === row.index ? AppFonts.demiBold : Font.Normal
            Accessible.role: row.heading ? Accessible.Heading : Accessible.ListItem
            Accessible.name: row.model.label

            contentItem: QQC2.Label {
                text: row.text
                font: row.font
                color: row.heading ? AppColors.muted : AppColors.text
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
        }

        SessionBound {
            control: combo
            property: "currentIndex"
            value: Math.max(0, picker.entries.findIndex(entry => entry.value === picker.chosen))
        }
    }
}
