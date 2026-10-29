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

// Everything the Application block can be asked: which program it listens
// to, which of that program's sounds it takes, and what else travels with
// them.
//
// **Its own panel rather than the generic one** because the first question
// is not a setting. The session stores the application as three fields —
// a binary, a Flatpak id, a name — in the order the engine matches them,
// and the generic panel would offer three boxes to type engine spellings
// into. What a person has is a program, usually one they can hear.
//
// **One application, three fields, one rule.** The picker deals in match
// rules, because a rule is the same thing whether the program is running,
// closed or not yet installed; the two spellings are turned into one
// another by the inventory, so that this panel and the block's face cannot
// come to different conclusions about which program is named.
ColumnLayout {
    id: panel

    /// The block being shown. The settings panel fills it in; a panel that
    /// required it could not be loaded by something that does not know what
    /// it is loading.
    property string blockId: ""

    /// The type whose words this panel borrows. It is the one type this
    /// panel is for, so it is written down rather than asked of the session.
    readonly property string typeId: "source.application"

    /// The block's own spellings of the three output modes. **The engine's
    /// constants** (`blocks::ApplicationSource::kOutputMode*`), which a
    /// session file holds; the interface may not reach into the block
    /// layer, so they are written here as well and a test holds the two
    /// together.
    readonly property string modeDefaultOutput: "default"
    readonly property string modeAnyOutput: "any"
    readonly property string modeChosenOutputs: "chosen"

    /// The application the block is pointed at, as one match rule.
    readonly property string rule: SessionModel.revision >= 0
        ? Inventory.applicationRuleFrom(SessionModel.param(panel.blockId, "binary") ?? "",
                                        SessionModel.param(panel.blockId, "flatpakId") ?? "",
                                        SessionModel.param(panel.blockId, "name") ?? "")
        : ""
    readonly property bool pointedAtSomething: panel.rule !== ""

    readonly property string outputMode: SessionModel.revision >= 0
        ? (SessionModel.param(panel.blockId, "outputMode") ?? panel.modeDefaultOutput)
        : panel.modeDefaultOutput
    /// The sinks the "only these" mode names, by `node.name`.
    readonly property var outputs: SessionModel.revision >= 0
        ? (SessionModel.param(panel.blockId, "outputs") ?? [])
        : []

    readonly property bool includeInput: SessionModel.revision >= 0
        && SessionModel.param(panel.blockId, "includeInput") === true
    readonly property bool splitChannels: SessionModel.revision >= 0
        && SessionModel.param(panel.blockId, "splitChannels") === true
    readonly property bool includeLowFrequency: SessionModel.revision >= 0
        && SessionModel.param(panel.blockId, "includeLowFrequency") === true
    readonly property bool openUrlOnStart: SessionModel.revision >= 0
        && SessionModel.param(panel.blockId, "openUrlOnStart") === true
    readonly property string url: SessionModel.revision >= 0
        ? (SessionModel.param(panel.blockId, "url") ?? "")
        : ""
    /// On by default, as the block declares it: a gap in what arrives is
    /// silence rather than nothing.
    readonly property bool fillGaps: SessionModel.revision >= 0
        && SessionModel.param(panel.blockId, "fillGapsWithSilence") !== false

    /// The three modes, with the one line each of them needs. **Built here
    /// rather than read from the descriptors**: the engine's choices are
    /// stable ids, and what each one means to a person is this panel's to
    /// say, in their language.
    readonly property var captureModes: [
        {
            value: panel.modeDefaultOutput,
            label: KI18n.i18nc("@option:radio capture what the application plays to the usual device",
                               "Capture audio played to the default output device"),
            hint: KI18n.i18nc("@info:whatsthis",
                              "What most people mean by the sound of an application. Anything it sends somewhere else, it sends there on purpose.")
        },
        {
            value: panel.modeAnyOutput,
            label: KI18n.i18nc("@option:radio capture everything the application plays",
                               "Capture audio played to all output devices"),
            hint: KI18n.i18nc("@info:whatsthis",
                              "Every sound the application makes, whichever device it sends it to.")
        },
        {
            value: panel.modeChosenOutputs,
            label: KI18n.i18nc("@option:radio capture only what goes to certain devices",
                               "Capture audio played to the specified devices"),
            hint: KI18n.i18nc("@info:whatsthis",
                              "For an application that plays the thing you want to one device and something else to another — a call on the headset and an alert on the speakers.")
        }
    ]

    /// The parts, so that a test drives what a person drives.
    readonly property alias sourceButton: sourceButton
    readonly property alias picker: applicationPicker
    readonly property alias modeButtons: modeButtons
    readonly property alias modeSection: modeSection
    readonly property alias outputRows: outputRows
    readonly property alias advancedSection: advanced
    readonly property alias advancedButton: advanced.toggle
    readonly property alias gapsCheck: gapsCheck
    readonly property alias microphoneCheck: microphoneCheck
    readonly property alias splitCheck: splitCheck
    readonly property alias urlCheck: urlCheck
    readonly property alias urlField: urlField

    spacing: AppMetrics.panelRowGap

    /// Points the block at an application. **All three fields are written,
    /// including the two the rule does not name**: a field left behind
    /// would go on being matched, and the block would capture the program
    /// somebody has just stopped asking for.
    ///
    /// Three writes because a parameter is what the engine takes; there is
    /// no edit that carries several at once, and each of the three is a
    /// value the plan reads.
    function choose(rule: string) {
        const fields = Inventory.applicationFields(rule);
        SessionModel.setParam(panel.blockId, "binary", fields.binary);
        SessionModel.setParam(panel.blockId, "flatpakId", fields.flatpakId);
        SessionModel.setParam(panel.blockId, "name", fields.name);
    }

    /// Takes one of the named outputs, or puts it back. **The whole list
    /// travels**, because that is what the parameter is: a list the engine
    /// matches against, not a sequence of additions it has to replay.
    function setOutputChosen(node: string, chosen: bool) {
        const kept = panel.outputs.filter(entry => entry !== node);
        SessionModel.setParam(panel.blockId, "outputs", chosen ? kept.concat([node]) : kept);
    }

    // --- the application, as the design draws it ---
    RowLayout {
        Layout.fillWidth: true
        spacing: AppMetrics.labelSpacing

        QQC2.Label {
            id: sourceLabel

            text: KI18n.i18nc("@label which application the block listens to", "Source")
            color: AppColors.muted
            // A translation goes onto a second line rather than under what is
            // beside it.
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignRight
            Layout.preferredWidth: panel.labelWidth
        }

        // **A list, because what it opens is a list** — a searchable one,
        // of the programs playing and the ones that played recently, which
        // a combo box's own list could not be. So it is a combo box whose
        // list is the picker: it is drawn as every other list in the
        // panel is, by whichever style the window has, with the style's own
        // arrow, and it is announced as a list that drops down.
        QQC2.ComboBox {
            id: sourceButton

            Layout.fillWidth: true
            displayText: panel.sourceName
            Accessible.name: KI18n.i18nc("@label:listbox", "Choose which application to capture")
            Accessible.description: panel.sourceName

            // **The program's icon is a hint, not a drawing**: the desktop
            // style draws a list's icon from Kirigami's style hint, and the
            // Reverblabr style reads the same hint, so the name is drawn
            // once, by the style, beside it.
            Kirigami.StyleHints.iconName: panel.sourceIcon

            // **Declared as the list rather than beside it**: the combo box
            // opens it, closes it and draws itself open while it is, and it
            // opens where the eye already is.
            popup: ApplicationPicker {
                id: applicationPicker

                y: sourceButton.height + Kirigami.Units.smallSpacing
                onChosen: rule => panel.choose(rule)
            }
        }
    }

    QQC2.Label {
        Layout.fillWidth: true
        Layout.leftMargin: panel.labelWidth + AppMetrics.labelSpacing
        // **Not an error.** A block pointed at a program that is closed is
        // the ordinary case: it starts capturing the moment the program
        // starts playing, which is the whole reason a rule is stored rather
        // than a stream.
        visible: panel.pointedAtSomething && !Inventory.hasApplication(panel.rule)
        wrapMode: Text.WordWrap
        color: AppColors.muted
        font: AppFonts.small
        text: KI18n.i18nc("@info the chosen application is not playing at the moment",
                          "Not playing at the moment. It will be captured as soon as it is.")
    }

    // --- a web address to open when the session starts ---
    QQC2.CheckBox {
        id: urlCheck

        Layout.fillWidth: true
        text: KI18n.i18nc("@option:check open a web address when the session starts", "Open URL")
        font.bold: true
        onToggled: SessionModel.setParam(panel.blockId, "openUrlOnStart", urlCheck.checked)

        SessionBound {
            control: urlCheck
            property: "checked"
            value: panel.openUrlOnStart
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: AppMetrics.labelSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label:textbox a web address", "URL")
            color: AppColors.muted
            enabled: panel.openUrlOnStart
            horizontalAlignment: Text.AlignRight
            Layout.preferredWidth: panel.labelWidth
        }

        QQC2.TextField {
            id: urlField

            Layout.fillWidth: true
            enabled: panel.openUrlOnStart
            // A hint longer than the field is whole on hover.
            QQC2.ToolTip.text: placeholderText
            QQC2.ToolTip.visible: hovered && text === "" && placeholderText !== ""
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            placeholderText: KI18n.i18nc("@info:placeholder what the web address is for",
                                         "Opened when the session starts")
            Accessible.name: KI18n.i18nc("@label:textbox", "Web address to open")
            onEditingFinished: SessionModel.setParam(panel.blockId, "url", urlField.text)

            SessionBound {
                control: urlField
                property: "text"
                value: panel.url
                // Left alone while somebody is typing an address into it.
                suspended: urlField.activeFocus
            }
        }
    }

    // --- which of its sounds ---
    //
    // **Three choices, all in sight**, as the design has them: a list box
    // hid two of them behind a click, and which one is chosen is the thing
    // most worth seeing about this block. Open to begin with, unlike
    // Advanced.
    CollapsibleSection {
        id: modeSection

        title: KI18n.i18nc("@title:group which of the application's sounds to take", "Audio device selection")
        expanded: true

        QQC2.ButtonGroup {
            id: modeGroup
        }

        Repeater {
            id: modeButtons

            model: panel.captureModes

            delegate: QQC2.RadioButton {
                id: modeButton

                required property var modelData

                Layout.fillWidth: true
                text: modeButton.modelData.label
                QQC2.ButtonGroup.group: modeGroup
                // What each one is for, where somebody wondering finds it.
                QQC2.ToolTip.text: modeButton.modelData.hint
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                onToggled: if (modeButton.checked) {
                    SessionModel.setParam(panel.blockId, "outputMode", modeButton.modelData.value);
                }

                contentItem: QQC2.Label {
                    leftPadding: modeButton.indicator.width + modeButton.spacing
                    text: modeButton.text
                    wrapMode: Text.WordWrap
                    verticalAlignment: Text.AlignVCenter
                }

                SessionBound {
                    control: modeButton
                    property: "checked"
                    value: panel.outputMode === modeButton.modelData.value
                }
            }
        }

        // --- the outputs the third choice names ---
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.gridUnit
            visible: panel.outputMode === panel.modeChosenOutputs
            spacing: Kirigami.Units.smallSpacing

            Repeater {
                id: outputRows

                model: Inventory.revision >= 0 ? Inventory.outputs : []

                delegate: QQC2.CheckBox {
                    id: outputCheck

                    required property var modelData

                    Layout.fillWidth: true
                    text: outputCheck.modelData.description !== ""
                        ? outputCheck.modelData.description
                        : outputCheck.modelData.node
                    onToggled: panel.setOutputChosen(outputCheck.modelData.node, outputCheck.checked)

                    SessionBound {
                        control: outputCheck
                        property: "checked"
                        value: panel.outputs.includes(outputCheck.modelData.node)
                    }
                }
            }

            QQC2.Label {
                Layout.fillWidth: true
                // **What the engine actually does with an empty list**, said
                // out loud: no condition is not the absence of a condition,
                // so a choice that names no outputs captures nothing at all.
                visible: panel.outputs.length === 0
                wrapMode: Text.WordWrap
                color: AppColors.warningText
                font: AppFonts.small
                text: KI18n.i18nc("@info no output has been named for the mode that needs one",
                                  "No output chosen, so nothing is being captured.")
            }
        }
    }

    // --- advanced ---
    //
    // Folded away, because most people want the sound of a program and
    // nothing else, and the ones who do not know exactly what they came
    // for. Each choice in bold with what it does under it, as the design
    // draws them.
    CollapsibleSection {
        id: advanced

        title: KI18n.i18nc("@title:group the less usual settings", "Advanced")

        ExplainedCheck {
            id: microphoneCheck

            text: KI18n.i18nc("@option:check take the microphone the application is recording from too",
                              "Include audio input")
            explanation: KI18n.i18nc("@info:whatsthis what including the input does",
                                     "Audio from input devices such as microphones is captured too — record both sides of a VoIP call.")
            onToggled: SessionModel.setParam(panel.blockId, "includeInput", microphoneCheck.checked)

            SessionBound {
                control: microphoneCheck
                property: "checked"
                value: panel.includeInput
            }
        }

        ExplainedCheck {
            id: splitCheck

            enabled: panel.includeInput
            text: KI18n.i18nc("@option:check put the two sides of a call on one channel each",
                              "Split between channels")
            explanation: KI18n.i18nc("@info:whatsthis what splitting a call does",
                                     "Inputs on the left channel, application audio on the right.")
            onToggled: SessionModel.setParam(panel.blockId, "splitChannels", splitCheck.checked)

            SessionBound {
                control: splitCheck
                property: "checked"
                value: panel.splitChannels
            }
        }

        ExplainedCheck {
            id: gapsCheck

            text: KI18n.i18nc("@option:check put silence where the application plays nothing",
                              "Fill playback gaps with silence")
            explanation: KI18n.i18nc("@info:whatsthis what filling the gaps does",
                                     "If the application plays nothing, silence is generated.")
            onToggled: SessionModel.setParam(panel.blockId, "fillGapsWithSilence", gapsCheck.checked)

            SessionBound {
                control: gapsCheck
                property: "checked"
                value: panel.fillGaps
            }
        }

        // **Kept, though the design has no room for it**: it is one of the
        // block's settings, and the only one that says what happens to a
        // surround stream's bass.
        ExplainedCheck {
            id: lowFrequencyCheck

            text: KI18n.i18nc("@option:check fold a surround stream's low-frequency channel into the mix",
                              "Include the bass channel")
            explanation: KI18n.i18nc("@info:whatsthis what including the bass channel does",
                                     "A surround stream's low-frequency channel is mixed in rather than left out.")
            onToggled: SessionModel.setParam(panel.blockId, "includeLowFrequency", lowFrequencyCheck.checked)

            SessionBound {
                control: lowFrequencyCheck
                property: "checked"
                value: panel.includeLowFrequency
            }
        }
    }

    /// What the source is called: what it calls itself while it is playing,
    /// what it called itself the last time it did, and the readable half of
    /// the rule for one this machine has never heard — in that order,
    /// because the first two are a name on a window and the third is a file
    /// name.
    readonly property string sourceName: panel.pointedAtSomething
        ? (RecentApplications.nameFor(panel.rule) || Inventory.applicationName(panel.rule))
        : KI18n.i18nc("@action:button no application has been chosen yet", "Choose an app")
    readonly property string sourceIcon: panel.pointedAtSomething
        ? (Inventory.application(panel.rule).icon ?? "")
        : "list-add"

    /// How wide the labels' column is, so the rows line up.
    readonly property real labelWidth: AppMetrics.panelNarrowLabelWidth

    /// A choice in bold with what it does under it, indented to its text.
    component ExplainedCheck: ColumnLayout {
        id: explained

        property alias text: box.text
        property alias checked: box.checked
        property string explanation
        signal toggled()

        function toggle() {
            box.toggle();
        }

        Layout.fillWidth: true
        // The prototype's four pixels between a choice and its reason.
        spacing: AppMetrics.followsSystem ? 0 : AppMetrics.smallSpacing

        QQC2.CheckBox {
            id: box

            Layout.fillWidth: true
            font.bold: true
            // A choice's words elide in a narrow panel; they are whole on hover.
            QQC2.ToolTip.text: box.text
            QQC2.ToolTip.visible: box.hovered && box.implicitContentWidth > box.availableWidth
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            onToggled: explained.toggled()
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.leftMargin: box.indicator.width + box.spacing
            wrapMode: Text.WordWrap
            color: AppColors.muted
            // Greyed further with its box when the choice cannot be made,
            // as "Split between channels" is until the input is included.
            opacity: explained.enabled ? 1 : 0.6
            font: AppFonts.small
            text: explained.explanation
        }
    }
}
