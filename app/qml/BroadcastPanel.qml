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

// The Broadcast block's settings: what its stream is doing, and three
// tabs — Setup (the server, the password and the format), Metadata (the
// station and the title listeners see) and Status (the connection's
// numbers).
//
// **Every control but the password is one of the block's own
// parameters**, written through SessionModel and put back by a
// SessionBound, so an undo, a preset or a second window moves it like
// anything else, and it is saved with the session. **The password is not**:
// it goes to the engine once, which keeps it in the desktop's secret store,
// and the field is emptied again. What the panel knows of it is whether
// there is one.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""
    /// Which tab is showing: "setup", "metadata" or "status".
    property string section: "setup"

    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(panel.blockId) : ({})
    readonly property string server: panel.params.server ?? "icecast"
    readonly property bool icecast: panel.server === "icecast"
    readonly property string format: panel.params.format ?? "mp3"
    readonly property bool mono: (panel.params.channels ?? "stereo") === "mono"
    /// **Only the rates the format takes for so many channels**, which the
    /// engine says: Vorbis wants more than 32 kbit/s for stereo and takes
    /// no more than 192 for mono. A setting outside them is streamed at the
    /// nearest one, which is what is shown.
    readonly property var bitrateRange: BroadcastStatus.bitrateRange(panel.format, panel.mono)
    readonly property var bitrates: words.bitrates.filter(
        rate => rate >= panel.bitrateRange.lowest && rate <= panel.bitrateRange.highest)
    readonly property int bitrate: Math.min(Math.max(panel.params.bitrate ?? 128, panel.bitrateRange.lowest),
                                            panel.bitrateRange.highest)

    /// What the engine offers, and what it cannot here.
    property var formatParam: BlockCatalogue.ready ? BlockCatalogue.describeParam("output.broadcast", "format") : ({})
    readonly property var formats: panel.formatParam.choices ?? words.formats
    readonly property var unavailable: panel.formatParam.unavailableChoices ?? []

    readonly property var status: BroadcastStatus.revision >= 0
        ? BroadcastStatus.status(SessionModel.sessionId, panel.blockId) : ({})
    readonly property string streamState: panel.status.state ?? "idle"
    readonly property bool onAir: panel.streamState === "connected"
    readonly property bool passwordStored: panel.status.passwordStored ?? false
    property int connectedSeconds: 0

    /// The controls, so that a test drives what a person drives.
    readonly property alias stateLabel: stateText
    readonly property alias timeLabel: timeText
    readonly property alias sections: sectionFrame
    readonly property alias serverBox: serverControl
    readonly property alias hostField: hostControl
    readonly property alias portBox: portControl
    readonly property alias mountField: mountControl
    readonly property alias userField: userControl
    readonly property alias passwordField: passwordControl
    readonly property alias passwordLine: passwordHint
    readonly property alias forgetButton: forgetControl
    readonly property alias formatBox: formatControl
    readonly property alias bitrateBox: bitrateControl
    readonly property alias stereoButton: stereoChoice
    readonly property alias monoButton: monoChoice
    readonly property alias unavailableLine: unavailableHint
    readonly property alias stationFields: stationFields
    readonly property alias listedCheck: listedControl
    readonly property alias templateField: templateControl
    readonly property alias nowPlayingBox: nowPlayingRows.sourceBox
    readonly property alias nowPlayingFileField: nowPlayingRows.fileField
    readonly property alias playerBox: nowPlayingRows.playerBox
    readonly property alias playerLine: nowPlayingRows.playerLine
    readonly property alias titleLine: titleText
    readonly property alias statusGrid: statusGrid
    readonly property alias sentLabel: sentText
    readonly property alias rateLabel: rateText
    readonly property alias reconnectsLabel: reconnectsText
    readonly property alias connectedLabel: connectedText
    readonly property alias problemLabel: problemText
    readonly property alias words: words

    spacing: AppMetrics.panelRowGap

    BroadcastWords {
        id: words
    }

    function set(paramId: string, value: var) {
        SessionModel.setParam(panel.blockId, paramId, value);
    }

    function refresh() {
        panel.connectedSeconds = BroadcastStatus.connectedSeconds(SessionModel.sessionId, panel.blockId);
    }

    /// Hands what was typed to the engine, and lets go of it here.
    function storePassword() {
        if (passwordControl.text === "") {
            return;
        }
        BroadcastStatus.setPassword(SessionModel.sessionId, panel.blockId, passwordControl.text);
        passwordControl.text = "";
    }

    onStreamStateChanged: panel.refresh()
    onBlockIdChanged: BroadcastStatus.request(SessionModel.sessionId, panel.blockId)
    Component.onCompleted: {
        BroadcastStatus.request(SessionModel.sessionId, panel.blockId);
        panel.refresh();
    }

    Connections {
        target: SessionModel

        function onSessionIdChanged() {
            BroadcastStatus.request(SessionModel.sessionId, panel.blockId);
        }
    }

    Timer {
        interval: 1000
        repeat: true
        running: panel.onAir
        onTriggered: panel.refresh()
    }

    component FieldLabel: QQC2.Label {
        color: AppColors.muted
        horizontalAlignment: Text.AlignRight
        elide: Text.ElideRight
        // A label column is fixed; a longer word goes onto a second line
        // rather than out of sight.
        wrapMode: Text.Wrap
        maximumLineCount: 2
        Layout.preferredWidth: AppMetrics.panelLabelWidth
    }

    /// A line of text that writes one parameter when it is done with.
    component ParamField: QQC2.TextField {
        id: field

        required property string paramId
        property string fallback: ""

        // **Takes what the row has left, and asks for nothing**: a field
        // that asked for its own width made the card wider than the card.
        Layout.preferredWidth: 1
        Layout.fillWidth: true
        onEditingFinished: {
            if (field.text !== (panel.params[field.paramId] ?? field.fallback)) {
                panel.set(field.paramId, field.text);
            }
        }

        SessionBound {
            control: field
            property: "text"
            value: panel.params[field.paramId] ?? field.fallback
            suspended: field.activeFocus
        }
    }

    /// One number of the Status tab, with its label.
    component StatusValue: QQC2.Label {
        font: AppFonts.mono
        elide: Text.ElideRight
        Layout.fillWidth: true
    }

    // --- what it is doing ---
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: stateRow.implicitHeight
        radius: AppMetrics.smallRadius
        color: AppColors.field
        border.color: AppColors.border

        RowLayout {
            id: stateRow

            anchors {
                fill: parent
                leftMargin: AppMetrics.largeSpacing
                rightMargin: AppMetrics.largeSpacing
            }
            spacing: AppMetrics.largeSpacing

            Rectangle {
                implicitWidth: 9
                implicitHeight: 9
                radius: height / 2
                color: panel.onAir ? AppColors.dangerMark : AppColors.muted
            }

            QQC2.Label {
                id: stateText

                text: words.stateText(panel.streamState, panel.status.error ?? "", panel.status.retryIn ?? 0)
                font.weight: Font.DemiBold
                // Why a stream stopped can be a sentence: it goes onto a
                // second line rather than out of sight.
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.topMargin: AppMetrics.mediumSpacing
                Layout.bottomMargin: AppMetrics.mediumSpacing
            }

            QQC2.Label {
                id: timeText

                visible: panel.onAir
                text: RecordingStatus.clock(panel.connectedSeconds)
                font: AppFonts.mono
            }
        }
    }

    SegmentedFrame {
        id: sectionFrame

        Layout.alignment: Qt.AlignHCenter

        QQC2.ButtonGroup {
            id: sectionGroup
        }

        Repeater {
            model: [
                { id: "setup", name: KI18n.i18nc("@title:tab a Broadcast block's settings", "Setup") },
                { id: "metadata", name: KI18n.i18nc("@title:tab a Broadcast block's settings", "Metadata") },
                { id: "status", name: KI18n.i18nc("@title:tab a Broadcast block's settings", "Status") }
            ]

            delegate: SegmentedChoice {
                id: tab

                required property var modelData

                text: tab.modelData.name
                checked: panel.section === tab.modelData.id
                QQC2.ButtonGroup.group: sectionGroup
                onClicked: panel.section = tab.modelData.id
            }
        }
    }

    // --- Setup ---
    ColumnLayout {
        visible: panel.section === "setup"
        Layout.fillWidth: true
        spacing: AppMetrics.panelRowGap

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.labelSpacing

            FieldLabel {
                text: KI18n.i18nc("@label:listbox the kind of internet radio server", "Server")
            }

            QQC2.ComboBox {
                id: serverControl

                Layout.preferredWidth: 1
                Layout.fillWidth: true
                model: words.servers.map(id => words.serverName(id))
                Accessible.name: KI18n.i18nc("@label:listbox", "Server")
                onActivated: index => panel.set("server", words.servers[index])

                SessionBound {
                    control: serverControl
                    property: "currentIndex"
                    value: words.servers.indexOf(panel.server)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.labelSpacing

            FieldLabel {
                text: KI18n.i18nc("@label:textbox a server's name or address", "Host")
            }

            ParamField {
                id: hostControl

                paramId: "host"
                fallback: "localhost"
                placeholderText: KI18n.i18nc("@info:placeholder", "radio.example.org")
                Accessible.name: KI18n.i18nc("@label:textbox", "Host")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.labelSpacing

            FieldLabel {
                text: KI18n.i18nc("@label:spinbox a server's port", "Port")
            }

            QQC2.SpinBox {
                id: portControl

                // As wide as it likes where there is room, and narrower in a
                // narrow card rather than wider than it.
                Layout.fillWidth: true
                Layout.maximumWidth: implicitWidth
                Layout.minimumWidth: Kirigami.Units.gridUnit * 5
                from: 1
                to: 65535
                editable: true
                // A port is a name, not an amount: no thousands separator.
                textFromValue: (value, locale) => String(value)
                valueFromText: (text, locale) => parseInt(text)
                Accessible.name: KI18n.i18nc("@label:spinbox a server's port", "Port")
                onValueModified: panel.set("port", portControl.value)

                SessionBound {
                    control: portControl
                    property: "value"
                    value: panel.params.port ?? 8000
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.labelSpacing
            // SHOUTcast has one stream a server, and no mount point.
            visible: panel.icecast

            FieldLabel {
                text: KI18n.i18nc("@label:textbox an Icecast server's mount point", "Mount point")
            }

            ParamField {
                id: mountControl

                paramId: "mount"
                fallback: "/stream"
                Accessible.name: KI18n.i18nc("@label:textbox", "Mount point")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.labelSpacing
            visible: panel.icecast

            FieldLabel {
                text: KI18n.i18nc("@label:textbox the user a stream logs in to its server as", "User")
            }

            ParamField {
                id: userControl

                paramId: "user"
                fallback: "source"
                Accessible.name: KI18n.i18nc("@label:textbox", "User")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.labelSpacing

            FieldLabel {
                text: KI18n.i18nc("@label:textbox the password a stream logs in to its server with", "Password")
            }

            QQC2.TextField {
                id: passwordControl

                echoMode: TextInput.Password
                // Nothing typed here is kept: no undo, no suggestions.
                inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                Layout.preferredWidth: 1
                Layout.fillWidth: true
                placeholderText: panel.passwordStored
                    ? KI18n.i18nc("@info:placeholder a password is stored; typing replaces it", "Stored. Type a new one to replace it.")
                    : KI18n.i18nc("@info:placeholder", "Not set")
                Accessible.name: KI18n.i18nc("@label:textbox", "Password")
                // **What a screen reader reads**: Qt Quick hides the name of
                // a field in password mode, and reads its description.
                Accessible.description: KI18n.i18nc("@info a password field, as a screen reader reads it",
                                                    "Password for the server")
                onAccepted: panel.storePassword()
                onEditingFinished: panel.storePassword()
            }

            QQC2.ToolButton {
                id: forgetControl

                visible: panel.passwordStored
                icon.name: "edit-clear"
                display: QQC2.AbstractButton.IconOnly
                text: KI18n.i18nc("@action:button remove a stream's stored password", "Forget Password")
                Accessible.name: text
                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
                onClicked: BroadcastStatus.setPassword(SessionModel.sessionId, panel.blockId, "")
            }
        }

        InfoLine {
            id: passwordHint

            text: KI18n.i18nc("@info where a stream's password is kept",
                              "Kept in your system's password store, never in the session file.")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.labelSpacing

            FieldLabel {
                text: KI18n.i18nc("@label:listbox a stream's format", "Format")
            }

            QQC2.ComboBox {
                id: formatControl

                Layout.preferredWidth: 1
                Layout.fillWidth: true
                model: panel.formats
                displayText: words.formatName(panel.format)
                Accessible.name: KI18n.i18nc("@label:listbox", "Format")

                delegate: QQC2.ItemDelegate {
                    required property string modelData
                    required property int index

                    width: formatControl.width
                    text: words.formatName(modelData)
                    highlighted: formatControl.highlightedIndex === index
                }

                onActivated: index => panel.set("format", String(panel.formats[index]))

                SessionBound {
                    control: formatControl
                    property: "currentIndex"
                    value: panel.formats.indexOf(panel.format)
                }
            }
        }

        InfoLine {
            id: unavailableHint

            visible: panel.unavailable.length > 0
            text: KI18n.i18nc("@info %1 is a list of formats",
                              "This system's FFmpeg can't make %1.",
                              panel.unavailable.map(entry => words.formatName(entry.id)).join(", "))
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.labelSpacing

            FieldLabel {
                text: KI18n.i18nc("@label:listbox a stream's bit rate", "Bit rate")
            }

            QQC2.ComboBox {
                id: bitrateControl

                Layout.preferredWidth: 1
                Layout.fillWidth: true
                model: panel.bitrates.map(rate => KI18n.i18nc("@item a bit rate", "%1 kbps", rate))
                Accessible.name: KI18n.i18nc("@label:listbox", "Bit rate")
                onActivated: index => panel.set("bitrate", panel.bitrates[index])

                SessionBound {
                    control: bitrateControl
                    property: "currentIndex"
                    value: panel.bitrates.indexOf(panel.bitrate)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.labelSpacing

            FieldLabel {
                text: KI18n.i18nc("@label stereo or mono", "Channels")
            }

            SegmentedFrame {
                QQC2.ButtonGroup {
                    id: channelGroup
                }

                SegmentedChoice {
                    id: stereoChoice

                    text: KI18n.i18nc("@action:button two channels", "Stereo")
                    checked: (panel.params.channels ?? "stereo") === "stereo"
                    QQC2.ButtonGroup.group: channelGroup
                    onClicked: panel.set("channels", "stereo")
                }

                SegmentedChoice {
                    id: monoChoice

                    text: KI18n.i18nc("@action:button one channel, both sides mixed", "Mono")
                    checked: (panel.params.channels ?? "stereo") === "mono"
                    QQC2.ButtonGroup.group: channelGroup
                    onClicked: panel.set("channels", "mono")
                }
            }

            Item {
                Layout.fillWidth: true
            }
        }
    }

    // --- Metadata ---
    ColumnLayout {
        visible: panel.section === "metadata"
        Layout.fillWidth: true
        spacing: AppMetrics.panelRowGap

        GridLayout {
            id: stationFields

            Layout.fillWidth: true
            columns: 1
            rowSpacing: AppMetrics.smallSpacing

            Repeater {
                model: [
                    { id: "stationName", name: KI18n.i18nc("@label:textbox a radio station's name", "Station") },
                    { id: "genre", name: KI18n.i18nc("@label:textbox a radio station's genre", "Genre") },
                    { id: "url", name: KI18n.i18nc("@label:textbox a radio station's web page", "Web page") },
                    { id: "description", name: KI18n.i18nc("@label:textbox a radio station's description", "Description") }
                ]

                delegate: RowLayout {
                    id: stationRow

                    required property var modelData

                    Layout.fillWidth: true
                    spacing: AppMetrics.labelSpacing

                    FieldLabel {
                        text: stationRow.modelData.name
                    }

                    ParamField {
                        paramId: stationRow.modelData.id
                        Accessible.name: stationRow.modelData.name
                    }
                }
            }
        }

        QQC2.CheckBox {
            id: listedControl

            Layout.leftMargin: AppMetrics.panelLabelWidth + AppMetrics.labelSpacing
            text: KI18n.i18nc("@option:check let the server list a station in a public directory",
                              "List in public directories")
            onToggled: panel.set("listed", listedControl.checked)

            SessionBound {
                control: listedControl
                property: "checked"
                value: panel.params.listed ?? false
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.labelSpacing

            FieldLabel {
                text: KI18n.i18nc("@label:textbox how the title listeners see is made", "Title")
            }

            ParamField {
                id: templateControl

                paramId: "titleTemplate"
                fallback: words.defaultTemplate
                Accessible.name: KI18n.i18nc("@label:textbox", "Title")
            }
        }

        InfoLine {
            text: KI18n.i18nc("@info the tokens of a stream's title; keep the %-words as they are",
                              "%artist, %title and %album are what is playing; %session is the session's name.")
        }

        NowPlayingRows {
            id: nowPlayingRows

            blockId: panel.blockId
            params: panel.params
            Layout.fillWidth: true
        }

        InfoLine {
            id: titleText

            visible: (panel.status.title ?? "") !== ""
            text: KI18n.i18nc("@info %1 is the title a stream's listeners see now", "Listeners see: %1",
                              panel.status.title ?? "")
        }
    }

    // --- Status ---
    GridLayout {
        id: statusGrid

        visible: panel.section === "status"
        Layout.fillWidth: true
        columns: 2
        columnSpacing: AppMetrics.labelSpacing
        rowSpacing: AppMetrics.smallSpacing

        FieldLabel {
            text: KI18n.i18nc("@label how long a stream has been connected", "Connected for")
        }

        StatusValue {
            id: connectedText

            text: panel.onAir ? RecordingStatus.clock(panel.connectedSeconds) : "—"
        }

        FieldLabel {
            text: KI18n.i18nc("@label how much a stream has sent to its server", "Sent")
        }

        StatusValue {
            id: sentText

            text: BroadcastStatus.bytesText(panel.status.bytesSent ?? 0)
        }

        FieldLabel {
            text: KI18n.i18nc("@label the rate a stream is going out at, measured", "Bit rate")
        }

        StatusValue {
            id: rateText

            text: panel.onAir
                ? KI18n.i18nc("@info a measured bit rate", "%1 kbps", Math.round(panel.status.kbps ?? 0))
                : "—"
        }

        FieldLabel {
            text: KI18n.i18nc("@label how many times a stream connected again", "Reconnects")
        }

        StatusValue {
            id: reconnectsText

            text: String(panel.status.reconnects ?? 0)
        }

        FieldLabel {
            text: KI18n.i18nc("@label what last went wrong with a stream", "Last problem")
        }

        QQC2.Label {
            id: problemText

            text: (panel.status.error ?? "") !== "" ? words.errorText(panel.status.error) : "—"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }
}
