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

// The Mixer's settings: its five strips, a fade of them all, the
// master, and the five scenes.
//
// **Every timed change is one edit, sent in order**: the strip's ramp,
// its volume, and the ramp put back to a drag's 20 ms (SessionModel.
// setParams). The block takes the volume over the ramp it was sent with,
// so a fade takes half a second whatever the queue does, and undoing it is
// one step that fades back the same way.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    /// Wider than the standard card: a strip is a row of seven controls.
    readonly property real panelWidth: AppMetrics.followsSystem ? Kirigami.Units.gridUnit * 34 : 540

    /// The block's own times (blocks::Mixer::kFadeMs and the rest), written
    /// down again because the interface does not include the block; the
    /// Mixer's QML test holds each against what a button sends.
    readonly property real dragRampMs: 20
    readonly property real fadeMs: 500
    readonly property real slowFadeMs: 2000
    readonly property real sceneRecallMs: 1000
    readonly property int strips: 5
    readonly property var scenes: ["A", "B", "C", "D", "E"]

    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(panel.blockId) : ({})
    /// Every wire into the block, for naming what feeds each strip.
    readonly property var arriving: SessionModel.revision >= 0 ? SessionModel.wiresTouching(panel.blockId) : []

    readonly property alias stripRows: stripRepeater
    readonly property alias fadeAllDownButton: fadeAllDown
    readonly property alias fadeAllUpButton: fadeAllUp
    readonly property alias sceneButtons: sceneRepeater
    readonly property alias saveSceneMenu: saveMenu
    readonly property alias masterSlider: masterControl

    spacing: AppMetrics.panelRowGap

    // --- reading ---

    function number(id: string, otherwise: real): real {
        const value = panel.params[id];
        return value === undefined ? otherwise : Number(value);
    }

    function flag(id: string): bool {
        return panel.params[id] === true;
    }

    function sceneSaved(scene: string): bool {
        return panel.flag("scene" + scene + "Saved");
    }

    /// What feeds strip \a strip, by the names the canvas gives the blocks:
    /// the nickname, or the block's own name. Several are joined; nothing
    /// wired in is empty.
    function sourceOf(strip: int): string {
        const names = [];
        for (const wire of panel.arriving) {
            if (wire.toBlock !== panel.blockId || Math.floor(wire.toPort / 2) !== strip) {
                continue;
            }
            const block = SessionModel.block(wire.fromBlock);
            const name = (block.nickname ?? "") !== "" ? block.nickname : BlockPresentation.name(block.type ?? "");
            if (names.indexOf(name) < 0) {
                names.push(name);
            }
        }
        return names.join(" + ");
    }

    // --- what the buttons send ---

    function slowHeld(): bool {
        return (Keyboard.modifiers() & Qt.ShiftModifier) !== 0;
    }

    function exclusiveHeld(): bool {
        return (Keyboard.modifiers() & Qt.AltModifier) !== 0;
    }

    /// Fades \a which strips to \a target, an amplitude, over half a second
    /// or, \a slow, two. \a target may be a list, one a strip.
    function fadeStrips(which: var, target: var, slow: bool) {
        panel.rampStrips(which, target, slow ? panel.slowFadeMs : panel.fadeMs);
    }

    function rampStrips(which: var, target: var, ms: real) {
        const changes = [];
        for (const strip of which) {
            changes.push(["ramp" + (strip + 1), ms]);
        }
        which.forEach((strip, position) => {
            const to = Array.isArray(target) ? target[position] : target;
            changes.push(["volume" + (strip + 1), to]);
        });
        for (const strip of which) {
            changes.push(["ramp" + (strip + 1), panel.dragRampMs]);
        }
        SessionModel.setParams(panel.blockId, changes);
    }

    function allStrips(): var {
        return [0, 1, 2, 3, 4];
    }

    /// Solo is additive; \a exclusive makes it this strip alone, in one edit.
    function toggleSolo(strip: int, exclusive: bool) {
        const id = "solo" + (strip + 1);
        if (!exclusive) {
            SessionModel.setParam(panel.blockId, id, !panel.flag(id));
            return;
        }
        const changes = [];
        for (let other = 0; other < panel.strips; ++other) {
            changes.push(["solo" + (other + 1), other === strip]);
        }
        SessionModel.setParams(panel.blockId, changes);
    }

    /// Keeps the five volumes as scene \a scene.
    function saveScene(scene: string) {
        const changes = [];
        for (let strip = 1; strip <= panel.strips; ++strip) {
            changes.push(["scene" + scene + strip, panel.number("volume" + strip, 1)]);
        }
        changes.push(["scene" + scene + "Saved", true]);
        SessionModel.setParams(panel.blockId, changes);
    }

    /// Brings scene \a scene's volumes back, over a second.
    function recallScene(scene: string) {
        if (!panel.sceneSaved(scene)) {
            return;
        }
        const levels = [];
        for (let strip = 1; strip <= panel.strips; ++strip) {
            levels.push(panel.number("scene" + scene + strip, 1));
        }
        panel.rampStrips(panel.allStrips(), levels, panel.sceneRecallMs);
    }

    // --- the strips ---

    Repeater {
        id: stripRepeater

        model: panel.strips

        delegate: MixerStrip {
            mixer: panel
        }
    }

    // --- the master ---
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label:slider the Mixer's volume over all its inputs", "Master")
            color: AppColors.text
            Layout.preferredWidth: Kirigami.Units.gridUnit * 4
        }

        QQC2.Slider {
            id: masterControl

            Layout.fillWidth: true
            Layout.preferredWidth: Kirigami.Units.gridUnit * 4
            from: 0
            to: 200
            Accessible.name: KI18n.i18nc("@label:slider", "Master volume")

            onMoved: SessionModel.setParam(panel.blockId, "master", value / 100, true)
            onPressedChanged: if (!pressed) {
                SessionModel.commitParam(panel.blockId, "master");
            }

            SessionBound {
                control: masterControl
                property: "value"
                value: panel.number("master", 1) * 100
                suspended: masterControl.pressed
            }
        }

        QQC2.Label {
            text: KI18n.i18nc("@info a volume as a percentage", "%1 %", Math.round(masterControl.value))
            color: AppColors.accentText
            font.family: AppFonts.monoFamily
            horizontalAlignment: Text.AlignRight
            // Room for a longer unit in another language.
            Layout.preferredWidth: Math.max(Kirigami.Units.gridUnit * 2.5, implicitWidth)
        }
    }

    // --- fading every strip ---
    // **A row that wraps**: the label and its two buttons asked for
    // 316 pixels, the scenes below for 369, in a card the smallest window
    // leaves 245 of; and every row of the panel, the strips too, was then
    // laid out that wide. Where there is room it reads as one line, as the
    // design draws it; where there is not, the buttons go onto the next.
    Flow {
        Layout.fillWidth: true
        // Its width is the panel's, never its own row's.
        Layout.minimumWidth: 0
        Layout.preferredWidth: 0
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@title:group fading every Mixer strip at once", "Fade all")
            color: AppColors.muted
            // A translation goes onto a second line rather than under what is
            // beside it.
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            width: Kirigami.Units.gridUnit * 4
            height: Math.max(implicitHeight, fadeAllDown.height)
            verticalAlignment: Text.AlignVCenter
        }

        QQC2.Button {
            id: fadeAllDown

            text: KI18n.i18nc("@action:button fade every Mixer strip to silence", "Fade All to 0 %")
            QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip", "Over half a second; with Shift, two seconds")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            onClicked: panel.fadeStrips(panel.allStrips(), 0, panel.slowHeld())
        }

        QQC2.Button {
            id: fadeAllUp

            text: KI18n.i18nc("@action:button fade every Mixer strip up to its full level", "Fade All to 100 %")
            QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip", "Over half a second; with Shift, two seconds")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            onClicked: panel.fadeStrips(panel.allStrips(), 1, panel.slowHeld())
        }
    }

    // --- the scenes ---
    // Wraps as the row above does.
    Flow {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredWidth: 0
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@title:group saved sets of the Mixer's volumes", "Scenes")
            color: AppColors.muted
            elide: Text.ElideRight
            width: Kirigami.Units.gridUnit * 4
            height: Math.max(implicitHeight, saveButton.height)
            verticalAlignment: Text.AlignVCenter
        }

        Repeater {
            id: sceneRepeater

            model: panel.scenes

            delegate: QQC2.Button {
                required property string modelData

                text: modelData
                // **A scene nobody has saved has nothing to recall**, and
                // says so by being unavailable, as the design draws it.
                enabled: panel.sceneSaved(modelData)
                implicitWidth: Kirigami.Units.gridUnit * 2
                Accessible.name: KI18n.i18nc("@action:button bring a saved set of volumes back", "Recall scene %1",
                                             modelData)
                QQC2.ToolTip.text: Accessible.name
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                onClicked: panel.recallScene(modelData)
            }
        }

        QQC2.Button {
            id: saveButton

            text: KI18n.i18nc("@action:button keep the volumes as a scene", "Save Scene")
            onClicked: saveMenu.popup(saveButton, 0, saveButton.height)

            QQC2.Menu {
                id: saveMenu

                Instantiator {
                    model: panel.scenes

                    delegate: QQC2.MenuItem {
                        required property string modelData

                        text: KI18n.i18nc("@action:inmenu keep the volumes as one of five scenes",
                                          "Save as Scene %1", modelData)
                        onTriggered: panel.saveScene(modelData)
                    }

                    onObjectAdded: (index, object) => saveMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => saveMenu.removeItem(object)
                }
            }
        }
    }
}
