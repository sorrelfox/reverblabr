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

// One recording in the Files tab, as the prototype draws one: a dot,
// the file's name, its outline, and its length, format and size.
//
// **A recording still being written says so.** Its dot is in the danger
// colour, as a recording light is, its outline is a flat line in the live
// colour — there is nothing to draw until the file is finished — and where
// the length goes it says "Recording". A finished one's dot is the ok
// colour and its outline the muted one.
QQC2.ItemDelegate {
    id: card

    required property int index
    required property string path
    required property string name
    required property var started
    required property real seconds
    required property string format
    required property real sizeBytes
    required property bool inProgress
    required property var peaks
    required property var tags

    /// Whether this is the one the list has chosen.
    property bool selected: false
    /// The list this card is in, which writes lengths and sizes the way
    /// the reader reads them.
    property RecordingList list: null

    /// Double-click or Enter: what the Recording Inspector will open on.
    signal activated()
    /// A right click, or the menu key: the actions for this recording.
    signal menuRequested(real x, real y)

    /// Whether this is the recording being listened to, playing or
    /// paused.
    readonly property bool previewing: RecordingPreview.path === card.path
    readonly property bool previewPlaying: card.previewing && RecordingPreview.playing
    /// How far the preview has got through this recording, from 0 to 1.
    readonly property real played: card.previewing ? RecordingPreview.progress : 0

    /// The play button, so that a test presses what a person presses.
    readonly property alias playButton: playButton
    readonly property alias outline: outline

    /// The line under the outline, in words: "58:12 · FLAC · 111.6 MB".
    readonly property string detail: card.inProgress
        ? KI18n.i18nc("@info a recording still being written, and its format", "Recording · %1",
                      card.formatName())
        : KI18n.i18nc("@info a recording's length, format and size", "%1 · %2 · %3",
                      card.list ? card.list.formatDuration(card.seconds) : "",
                      card.formatName(),
                      card.list ? card.list.formatSize(card.sizeBytes) : "")

    function formatName(): string {
        return card.list ? card.list.formatName(card.format) : card.format;
    }

    /// How many bars the outline is drawn with, as the prototype draws it.
    readonly property int barCount: 36

    /// \a count bars from the peaks the engine sent, each the loudest of its
    /// share. Empty peaks draw a flat line.
    function bars(count: int): var {
        const list = card.peaks ?? [];
        const drawn = [];
        for (let bar = 0; bar < count; ++bar) {
            if (list.length === 0) {
                drawn.push(0);
                continue;
            }
            const from = Math.floor(bar * list.length / count);
            const to = Math.max(from + 1, Math.floor((bar + 1) * list.length / count));
            let loudest = 0;
            for (let i = from; i < to && i < list.length; ++i) {
                loudest = Math.max(loudest, list[i]);
            }
            drawn.push(loudest);
        }
        return drawn;
    }

    padding: AppMetrics.followsSystem ? Kirigami.Units.smallSpacing * 2 : AppMetrics.mediumSpacing
    hoverEnabled: true
    highlighted: card.selected

    Accessible.role: Accessible.ListItem
    Accessible.name: card.tagLine !== "" ? card.name + ", " + card.tagLine : card.name
    Accessible.description: card.previewing
        ? KI18n.i18nc("@info a recording's details, and how far it has been listened to: '… · playing, 0:12 of 0:58'",
                      "%1 · %2, %3 of %4", card.detail,
                      card.previewPlaying ? KI18n.i18nc("@info a recording being listened to", "playing")
                                          : KI18n.i18nc("@info a recording whose preview is paused", "paused"),
                      card.list ? card.list.formatDuration(RecordingPreview.seconds) : "",
                      card.list ? card.list.formatDuration(RecordingPreview.duration) : "")
        : card.detail

    /// What the file's tags say it is, "Friday show — ep. 41 · Reverblabr
    /// FM", or empty: saved in the Recording Inspector, shown here.
    readonly property string tagLine: [card.tags?.title ?? "", card.tags?.artist ?? ""].filter(part => part !== "").join(" · ")

    QQC2.ToolTip.text: card.tagLine !== ""
        ? KI18n.i18nc("@info:tooltip what a recording's tags say it is, then its file: 'Friday show · Reverblabr FM\n/home/…/Talk.wav'",
                      "%1\n%2", card.tagLine, card.path)
        : card.path
    QQC2.ToolTip.visible: card.hovered
    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

    background: Rectangle {
        radius: AppMetrics.smallRadius
        color: card.selected || card.hovered ? AppColors.surface : "transparent"
        border.width: 1
        border.color: card.selected ? AppColors.accentMark : "transparent"
    }

    contentItem: ColumnLayout {
        spacing: AppMetrics.followsSystem ? Kirigami.Units.smallSpacing : AppMetrics.mediumSpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.followsSystem ? Kirigami.Units.smallSpacing : AppMetrics.mediumSpacing

            Rectangle {
                id: dot

                implicitWidth: AppMetrics.stateDotSize
                implicitHeight: AppMetrics.stateDotSize
                radius: width / 2
                color: card.inProgress ? AppColors.dangerMark : AppColors.okMark
                Accessible.ignored: true
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: card.name
                font.family: AppFonts.monoFamily
                font.weight: AppFonts.demiBold
                elide: Text.ElideMiddle
                Accessible.ignored: true
            }

            // **Listen to it**: on the chosen card, the one under the
            // pointer and the one being previewed. A file still being
            // written has nothing to play yet.
            QQC2.ToolButton {
                id: playButton

                visible: !card.inProgress && (card.selected || card.hovered || card.previewing)
                icon.name: card.previewPlaying ? "media-playback-pause" : "media-playback-start"
                display: QQC2.AbstractButton.IconOnly
                text: card.previewPlaying ? KI18n.i18nc("@action:button pause listening to a recording", "Pause")
                                          : KI18n.i18nc("@action:button listen to a recording", "Play")
                Layout.preferredHeight: AppMetrics.smallControlHeight
                Layout.preferredWidth: AppMetrics.smallControlHeight
                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                onClicked: RecordingPreview.toggle(card.path)
            }
        }

        // The outline: the loudest of each share of the file, from its start
        // to its end.
        Row {
            id: outline

            Layout.fillWidth: true
            Layout.preferredHeight: AppMetrics.controlHeight * 0.65
            spacing: 1

            Repeater {
                model: card.bars(card.barCount)

                delegate: Rectangle {
                    required property real modelData

                    width: Math.max(1, (outline.width - outline.spacing * (card.barCount - 1)) / card.barCount)
                    // A floor of one pixel, so a silent share is still drawn.
                    height: Math.max(1, outline.height * modelData)
                    // From the baseline up, as the design draws the bars.
                    y: outline.height - height
                    radius: AppMetrics.barRadius
                    // What the preview has played so far, in the accent.
                    color: card.inProgress ? AppColors.liveMark
                        : card.previewing && (index + 0.5) / card.barCount <= card.played ? AppColors.accent
                        : AppColors.muted

                    required property int index
                }
            }

            // **A click on the outline listens from there.**
            TapHandler {
                enabled: !card.inProgress
                onTapped: eventPoint => RecordingPreview.seekTo(card.path, eventPoint.position.x / outline.width)
            }
        }

        QQC2.Label {
            Layout.fillWidth: true
            text: card.detail
            font: AppFonts.libraryDetail
            color: card.inProgress ? AppColors.dangerText : AppColors.muted
            elide: Text.ElideRight
            Accessible.ignored: true
        }
    }

    onDoubleClicked: card.activated()
    // **Space listens**, on the card as on the list around it: a card with
    // the keyboard's focus would otherwise take Space as a click.
    Keys.onSpacePressed: event => {
        if (!card.inProgress) {
            RecordingPreview.toggle(card.path);
        }
        event.accepted = true;
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: (eventPoint, button) => card.menuRequested(eventPoint.position.x, eventPoint.position.y)
    }
}
