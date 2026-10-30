// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs as QtDialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.ki18n

// The Live Stream block's settings: what its stream is doing, and three
// tabs — Setup (the service, the key, the quality and the encoder),
// Picture (the words, what is playing, what moves with the sound, and the
// pictures) and Status (the connection's and the video's numbers).
//
// **Every control but the key is one of the block's own parameters**,
// written through SessionModel and put back by a SessionBound, as the
// Broadcast panel's are. **The key is not**: it goes to the engine once, as
// a Broadcast block's password does, which keeps it in the desktop's secret
// store, and the field is emptied again.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""
    /// Which tab is showing: "setup", "picture" or "status".
    property string section: "setup"

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(panel.blockId) : ({})
    readonly property string quality: panel.params.quality ?? "higher"
    readonly property string encoder: panel.params.encoder ?? "auto"
    readonly property string visualisation: panel.params.visualisation ?? "waveform"
    /// The background a block has until somebody changes it: the engine's.
    readonly property string defaultBackground: BlockCatalogue.ready
        ? (BlockCatalogue.describeParam("output.livestream", "background")["default"] ?? "") : ""

    /// What the engine offers, and the encoders it has not got here.
    property var encoderParam: BlockCatalogue.ready ? BlockCatalogue.describeParam("output.livestream", "encoder") : ({})
    readonly property var unavailable: panel.encoderParam.unavailableChoices ?? []
    /// **No H.264 encoder at all**: the block cannot stream here, and says
    /// why before anybody tries.
    readonly property bool noEncoder: panel.unavailable.some(entry => entry.id === "auto")

    readonly property var status: BroadcastStatus.revision >= 0
        ? BroadcastStatus.status(SessionModel.sessionId, panel.blockId) : ({})
    readonly property string streamState: panel.status.state ?? "idle"
    readonly property bool onAir: panel.streamState === "connected"
    readonly property bool keyStored: panel.status.passwordStored ?? false
    property int connectedSeconds: 0

    /// The controls, so that a test drives what a person drives.
    readonly property alias stateLabel: stateText
    readonly property alias timeLabel: timeText
    readonly property alias sections: sectionFrame
    readonly property alias urlField: urlControl
    readonly property alias keyField: keyControl
    readonly property alias keyLine: keyHint
    readonly property alias forgetButton: forgetControl
    readonly property alias qualityBox: qualityControl
    readonly property alias encoderBox: encoderControl
    readonly property alias noEncoderLine: noEncoderHint
    readonly property alias titleField: titleControl
    readonly property alias subtitleField: subtitleControl
    readonly property alias templateField: templateControl
    readonly property alias nowPlaying: nowPlayingRows
    readonly property alias visualisationBox: visualisationControl
    readonly property alias backgroundField: backgroundControl
    readonly property alias backgroundImageField: backgroundImageRow.field
    readonly property alias logoField: logoRow.field
    readonly property alias trackLine: trackText
    readonly property alias statusGrid: statusGrid
    readonly property alias sentLabel: sentText
    readonly property alias rateLabel: rateText
    readonly property alias videoLabel: videoText
    readonly property alias framesLabel: framesText
    readonly property alias reconnectsLabel: reconnectsText
    readonly property alias problemLabel: problemText
    readonly property alias words: words

    spacing: AppMetrics.panelRowGap

    LiveStreamWords {
        id: words
    }

    function set(paramId: string, value: var) {
        SessionModel.setParam(panel.blockId, paramId, value);
    }

    function refresh() {
        panel.connectedSeconds = BroadcastStatus.connectedSeconds(SessionModel.sessionId, panel.blockId);
    }

    /// Hands what was typed to the engine, and lets go of it here.
    function storeKey() {
        if (keyControl.text === "") {
            return;
        }
        BroadcastStatus.setPassword(SessionModel.sessionId, panel.blockId, keyControl.text);
        keyControl.text = "";
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
        wrapMode: Text.Wrap
        maximumLineCount: 2
        Layout.preferredWidth: AppMetrics.panelLabelWidth
    }

    /// A line of text that writes one parameter when it is done with.
    component ParamField: QQC2.TextField {
        id: field

        required property string paramId
        property string fallback: ""

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

    /// A list of the block's choices for one parameter.
    component ChoiceBox: QQC2.ComboBox {
        id: box

        required property string paramId
        required property var choices
        required property var nameOf
        required property string current

        Layout.preferredWidth: 1
        Layout.fillWidth: true
        model: box.choices.map(id => box.nameOf(id))
        onActivated: index => panel.set(box.paramId, String(box.choices[index]))

        SessionBound {
            control: box
            property: "currentIndex"
            value: box.choices.indexOf(box.current)
        }
    }

    /// A picture's file, with a button to choose one.
    component PictureRow: RowLayout {
        id: pictureRow

        required property string paramId
        required property string label
        property alias field: pictureField

        Layout.fillWidth: true
        spacing: AppMetrics.labelSpacing

        FieldLabel {
            text: pictureRow.label
        }

        ParamField {
            id: pictureField

            paramId: pictureRow.paramId
            placeholderText: KI18n.i18nc("@info:placeholder a picture file", "None")
            Accessible.name: pictureRow.label
        }

        QQC2.ToolButton {
            icon.name: "document-open"
            display: QQC2.AbstractButton.IconOnly
            text: KI18n.i18nc("@action:button choose a picture for a live stream", "Choose Picture…")
            Accessible.name: text
            QQC2.ToolTip.text: text
            QQC2.ToolTip.visible: hovered
            onClicked: {
                pictureDialog.paramId = pictureRow.paramId;
                pictureDialog.open();
            }
        }
    }

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
                { id: "setup", name: KI18n.i18nc("@title:tab a Live Stream block's settings", "Setup") },
                { id: "picture", name: KI18n.i18nc("@title:tab a Live Stream block's settings", "Picture") },
                { id: "status", name: KI18n.i18nc("@title:tab a Live Stream block's settings", "Status") }
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

        InfoLine {
            id: noEncoderHint

            visible: panel.noEncoder
            text: KI18n.i18nc("@info", "This system has no H.264 encoder that works, so it can't stream video. Install Cisco's OpenH264 (on Fedora, the openh264 package), or the full FFmpeg.")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.labelSpacing

            FieldLabel {
                text: KI18n.i18nc("@label:textbox a video service's RTMP address", "Server")
            }

            ParamField {
                id: urlControl

                paramId: "url"
                placeholderText: KI18n.i18nc("@info:placeholder the start of an RTMP address; keep it as it is",
                                             "rtmps://…")
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                Accessible.name: KI18n.i18nc("@label:textbox", "Server")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.labelSpacing

            FieldLabel {
                text: KI18n.i18nc("@label:textbox the secret key a video service gives for streaming", "Stream key")
            }

            QQC2.TextField {
                id: keyControl

                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                Layout.preferredWidth: 1
                Layout.fillWidth: true
                placeholderText: panel.keyStored
                    ? KI18n.i18nc("@info:placeholder a stream key is stored; typing replaces it", "Stored. Type a new one to replace it.")
                    : KI18n.i18nc("@info:placeholder", "Not set")
                Accessible.name: KI18n.i18nc("@label:textbox", "Stream key")
                // Qt Quick hides the name of a field in password mode, and
                // reads its description.
                Accessible.description: KI18n.i18nc("@info a stream key field, as a screen reader reads it",
                                                    "Stream key for the service")
                onAccepted: panel.storeKey()
                onEditingFinished: panel.storeKey()
            }

            QQC2.ToolButton {
                id: forgetControl

                visible: panel.keyStored
                icon.name: "edit-clear"
                display: QQC2.AbstractButton.IconOnly
                text: KI18n.i18nc("@action:button remove a live stream's stored key", "Forget Stream Key")
                Accessible.name: text
                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
                onClicked: BroadcastStatus.setPassword(SessionModel.sessionId, panel.blockId, "")
            }
        }

        InfoLine {
            id: keyHint

            text: KI18n.i18nc("@info where a live stream's key is kept",
                              "Kept in your system's password store, never in the session file.")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.labelSpacing

            FieldLabel {
                text: KI18n.i18nc("@label:listbox a live stream's size and frame rate", "Quality")
            }

            ChoiceBox {
                id: qualityControl

                paramId: "quality"
                choices: words.qualities
                nameOf: id => words.qualityName(id)
                current: panel.quality
                Accessible.name: KI18n.i18nc("@label:listbox", "Quality")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.labelSpacing

            FieldLabel {
                text: KI18n.i18nc("@label:listbox what makes a live stream's video", "Encoder")
            }

            ChoiceBox {
                id: encoderControl

                paramId: "encoder"
                // Only the ones this system has, and the one the block holds.
                choices: words.encoders.filter(id => id === panel.encoder
                    || !panel.unavailable.some(entry => entry.id === id))
                nameOf: id => words.encoderName(id)
                current: panel.encoder
                Accessible.name: KI18n.i18nc("@label:listbox", "Encoder")
            }
        }
    }

    // --- Picture ---
    ColumnLayout {
        visible: panel.section === "picture"
        Layout.fillWidth: true
        spacing: AppMetrics.panelRowGap

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.labelSpacing

            FieldLabel {
                text: KI18n.i18nc("@label:textbox the large words on a live stream's picture", "Title")
            }

            ParamField {
                id: titleControl

                paramId: "title"
                fallback: words.defaultTitle
                Accessible.name: KI18n.i18nc("@label:textbox", "Title")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.labelSpacing

            FieldLabel {
                text: KI18n.i18nc("@label:textbox the words under a live stream's title", "Subtitle")
            }

            ParamField {
                id: subtitleControl

                paramId: "subtitle"
                Accessible.name: KI18n.i18nc("@label:textbox", "Subtitle")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.labelSpacing

            FieldLabel {
                text: KI18n.i18nc("@label:textbox how what is playing is shown on a live stream's picture", "Now playing as")
            }

            ParamField {
                id: templateControl

                paramId: "titleTemplate"
                fallback: words.stream.defaultTemplate
                Accessible.name: KI18n.i18nc("@label:textbox", "Now playing as")
            }
        }

        InfoLine {
            text: KI18n.i18nc("@info the tokens of a live stream's words; keep the %-words as they are",
                              "%artist, %title and %album are what is playing; %session is the session's name.")
        }

        NowPlayingRows {
            id: nowPlayingRows

            blockId: panel.blockId
            params: panel.params
            Layout.fillWidth: true
        }

        InfoLine {
            id: trackText

            visible: (panel.status.title ?? "") !== ""
            text: KI18n.i18nc("@info %1 is what a live stream's picture says is playing now", "The picture says: %1",
                              panel.status.title ?? "")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.labelSpacing

            FieldLabel {
                text: KI18n.i18nc("@label:listbox what moves with the sound in a live stream's picture", "Moving")
            }

            ChoiceBox {
                id: visualisationControl

                paramId: "visualisation"
                choices: words.visualisations
                nameOf: id => words.visualisationName(id)
                current: panel.visualisation
                Accessible.name: KI18n.i18nc("@label:listbox", "Moving")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.labelSpacing

            FieldLabel {
                text: KI18n.i18nc("@label:textbox a live stream's background colour", "Background")
            }

            Rectangle {
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: Kirigami.Units.iconSizes.small
                radius: AppMetrics.smallRadius
                border.color: AppColors.border
                color: /^#([0-9a-fA-F]{3}|[0-9a-fA-F]{6})$/.test(panel.params.background ?? "")
                    ? panel.params.background : panel.defaultBackground
            }

            ParamField {
                id: backgroundControl

                paramId: "background"
                fallback: panel.defaultBackground
                placeholderText: panel.defaultBackground
                Accessible.name: KI18n.i18nc("@label:textbox", "Background")
            }
        }

        PictureRow {
            id: backgroundImageRow

            paramId: "backgroundImage"
            label: KI18n.i18nc("@label:textbox a picture covering a live stream's frame", "Background picture")
        }

        PictureRow {
            id: logoRow

            paramId: "logo"
            label: KI18n.i18nc("@label:textbox a picture at the corner of a live stream's frame", "Logo")
        }

        InfoLine {
            text: KI18n.i18nc("@info", "The words are light or dark, whichever reads best over what is behind them.")
        }
    }

    QtDialogs.FileDialog {
        id: pictureDialog

        property string paramId: ""

        title: KI18n.i18nc("@title:window", "Choose a Picture")
        nameFilters: [KI18n.i18nc("@item:inlistbox a file type", "Pictures (*.png *.jpg *.jpeg *.webp *.bmp)"),
            KI18n.i18nc("@item:inlistbox a file type", "All files (*)")]
        onAccepted: panel.set(pictureDialog.paramId, RecordingStatus.folderFromUrl(pictureDialog.selectedFile))
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
            text: KI18n.i18nc("@label the rate a stream is going out at, measured", "Upload")
        }

        StatusValue {
            id: rateText

            text: panel.onAir
                ? KI18n.i18nc("@info a measured bit rate", "%1 kbps", Math.round(panel.status.kbps ?? 0))
                : "—"
        }

        FieldLabel {
            text: KI18n.i18nc("@label a live stream's picture size, frame rate and encoder", "Video")
        }

        StatusValue {
            id: videoText

            text: panel.onAir && (panel.status.encoder ?? "") !== ""
                ? KI18n.i18nc("@info %1 × %2 is a picture's size, %3 frames a second, %4 the encoder",
                              "%1 × %2, %3 fps, %4", panel.status.width ?? 0, panel.status.height ?? 0,
                              panel.status.fps ?? 0, words.encoderName(panel.status.encoder))
                : "—"
        }

        FieldLabel {
            text: KI18n.i18nc("@label frames a live stream sent, and left out", "Frames")
        }

        StatusValue {
            id: framesText

            text: KI18n.i18nc("@info %1 frames sent, %2 left out because the computer fell behind",
                              "%1 sent, %2 left out", panel.status.framesSent ?? 0, panel.status.framesDropped ?? 0)
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
