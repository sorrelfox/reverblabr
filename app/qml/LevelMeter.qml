// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.ki18n

// The level meter, wherever one is wanted: a block's tile, its panel, the
// tray, the mixer.
//
// **One component and one scale.** A meter drawn with one curve beside a
// scale drawn with another is a meter that lies about itself, and it is the
// kind of lie nobody notices until they are trying to match two sources by
// eye — so both read MeterScale.
//
// **Idle is not silence.** A session that is not running and a session
// playing nothing look completely different: silence is a meter at the
// bottom, idle is a meter switched off. A person glancing at the window has
// to be able to tell which without reading anything.
Item {
    id: meter

    /// The block whose levels these are.
    required property string blockId
    /// Whether to write the numbers beside the bars.
    property bool showScale: false
    /// Which numbers, when they are shown.
    property var ticks: meter.showScale ? MeterScale.fullTicks : MeterScale.compactTicks
    property bool horizontal: false
    /// Whether the clip light is a dot a side rather than one strip across
    /// the meter. **Which side it was is the first thing somebody wants to
    /// know**, and a meter with room for two dots can say it.
    property bool channelClips: false
    /// **The scale between the bars**: a bar a side, each
    /// `barWidth` wide, and the numbers in the middle, which is how the
    /// design draws a Peak/RMS tile. For one or two channels; a meter with
    /// more falls back to bars side by side, and one channel shows on both.
    property bool scaleBetween: false
    property real barWidth: Math.round(Kirigami.Units.gridUnit * 0.4)
    /// The colour of the RMS bar, and of the tip above it up to the peak.
    property color rmsColor: AppColors.accentMark
    property color peakColor: Kirigami.ColorUtils.linearInterpolation(meter.rmsColor, AppColors.text, 0.45)
    /// With the scale between the bars, a clip box above each bar rather
    /// than a strip across its top, and each side's channel under it.
    property bool clipBoxes: false
    /// The colour of the line that holds and falls.
    property color holdColor: AppColors.text
    property bool channelLabels: false
    /// How far apart the bars and the scale sit.
    property real splitSpacing: Kirigami.Units.smallSpacing * 2
    /// Where a bar turns hot, and what colour it turns: above full scale is
    /// never, which is every meter's unless it asks.
    property real hotDb: 1
    property color hotColor: AppColors.dangerMark
    /// The two sides' bars, for a test and for a tooltip.
    readonly property var sideBars: [leftBar.item, rightBar.item]

    readonly property var levels: Meters.frame >= 0 ? Meters.levels(meter.blockId) : ({})
    readonly property bool active: Meters.live && (meter.levels.active ?? false)
    readonly property bool clipped: meter.levels.clipped ?? false
    readonly property var clippedChannels: meter.channelClips ? (meter.levels.clippedChannels ?? []) : []
    readonly property int channelCount: meter.levels.channelCount ?? 2
    readonly property bool split: meter.scaleBetween && !meter.horizontal && meter.channelCount <= 2

    /// One channel's worth of \a values, as a list of one.
    function channelOf(values: var, channel: int): var {
        const all = values ?? [];
        return all.length === 0 ? [] : [all[Math.min(channel, all.length - 1)]];
    }

    /// Where a level sits in a bar \a height tall, from its top. **The
    /// bar's own geometry**: MeterItem keeps a strip at the top for the clip
    /// light, and a number placed against the whole height would sit a few
    /// pixels off the level it names.
    function yOf(db: real, height: real): real {
        const clip = Math.min(height * 0.04, 6);
        const well = Math.max(0, height - clip - 1);
        return clip + 1 + well * (1 - MeterScale.positionOf(db));
    }

    implicitWidth: meter.split ? splitRow.implicitWidth
                 : meter.horizontal ? Kirigami.Units.gridUnit * 8 : Kirigami.Units.gridUnit * 2
    implicitHeight: meter.horizontal ? Kirigami.Units.gridUnit * 2 : Kirigami.Units.gridUnit * 8

    Accessible.role: Accessible.ProgressBar
    Accessible.name: KI18n.i18nc("@info:whatsthis the level meter of a block, 'Volume level'", "%1 level",
                                 meter.blockName)
    // **Throttled on purpose.** A screen reader reading sixty values a
    // second says nothing; the loudest channel, as a whole number of
    // decibels brought up to date twice a second, is something a person
    // can follow.
    Accessible.description: meter.active
        ? KI18n.i18nc("@info:whatsthis the loudest channel, in decibels", "%1 dB", meter.value)
        : KI18n.i18nc("@info:whatsthis nothing is running", "Idle")

    /// What the block is called, for what the meter is read out as.
    readonly property string blockName: {
        const block = SessionModel.block(meter.blockId);
        return (block.nickname ?? "") !== "" ? block.nickname : BlockPresentation.name(block.type ?? "");
    }

    /// **The meter's reading as a progress bar has one**, which is where a
    /// screen reader looks for it: `value` between `from` and `to`, in whole
    /// decibels, the loudest channel's, at most twice a second.
    property real value: MeterScale.floorDb
    readonly property real from: MeterScale.floorDb
    readonly property real to: MeterScale.topDb

    Timer {
        interval: 500
        repeat: true
        triggeredOnStart: true
        running: meter.visible && meter.active
        onTriggered: meter.value = Math.max(MeterScale.floorDb, Math.min(MeterScale.topDb, Math.round(loudest.value)))
        onRunningChanged: if (!running) {
            meter.value = MeterScale.floorDb;
        }
    }

    // **A clip is said out loud**, once, and not again for a while:
    // the light only helps somebody who is looking at it.
    onClippedChanged: if (meter.clipped && meter.visible) {
        Announcer.say(meter, "clip:" + meter.blockId,
                      KI18n.i18nc("@info spoken: a block's meter has clipped, 'Volume clipped'", "%1 clipped",
                                  meter.blockName));
    }

    QtObject {
        id: loudest

        readonly property real value: {
            const peaks = meter.levels.peakDb ?? [];
            return peaks.length === 0 ? MeterScale.floorDb : Math.max(...peaks);
        }
    }

    MeterItem {
        id: bars

        visible: !meter.split
        anchors.fill: parent
        anchors.rightMargin: meter.showScale && !meter.horizontal ? Kirigami.Units.gridUnit * 2 : 0
        anchors.bottomMargin: meter.showScale && meter.horizontal ? Kirigami.Units.gridUnit : 0

        horizontal: meter.horizontal
        channelCount: meter.channelCount
        rmsDb: meter.levels.rmsDb ?? []
        peakDb: meter.levels.peakDb ?? []
        holdDb: meter.levels.holdDb ?? []
        active: meter.active
        clipped: meter.clipped
        clippedChannels: meter.clippedChannels

        wellColor: AppColors.well
        rmsColor: meter.rmsColor
        peakColor: meter.peakColor
        holdColor: meter.holdColor
        clipColor: AppColors.dangerMark
    }

    // --- bar, scale, bar ---
    //
    // Each side is a column: its clip box if it has one, its bar, its
    // channel's name if it has one. The scale between them is as tall as the
    // bars and no taller, so its numbers sit against the bars.
    readonly property real clipBoxHeight: meter.clipBoxes ? Math.round(Kirigami.Units.smallSpacing * 1.25) : 0
    readonly property real sideGap: Kirigami.Units.smallSpacing * 1.5
    readonly property real topPart: meter.clipBoxes ? meter.clipBoxHeight + meter.sideGap : 0
    readonly property real bottomPart: meter.channelLabels ? channelMetrics.height + meter.sideGap : 0
    readonly property real barHeight: Math.max(0, splitRow.height - meter.topPart - meter.bottomPart)

    /// What a side is called under its bar.
    function channelName(channel: int): string {
        if (meter.channelCount < 2) {
            return KI18n.i18nc("@label a mono meter's one channel", "M");
        }
        return channel === 0 ? KI18n.i18nc("@label the left channel, under its meter", "L")
                             : KI18n.i18nc("@label the right channel, under its meter", "R");
    }

    Component {
        id: sideBar

        Column {
            id: side

            /// Which channel this side draws.
            property int channel: 0
            readonly property bool clippedHere: (meter.levels.clippedChannels ?? [])[Math.min(side.channel,
                                                    Math.max(0, meter.channelCount - 1))] === true
            readonly property alias bar: sideMeter

            spacing: meter.sideGap
            width: Math.max(meter.barWidth, meter.clipBoxes ? meter.barWidth : 0)

            // **The clip box**, dark until that side reaches full scale.
            Rectangle {
                visible: meter.clipBoxes
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.max(2, meter.barWidth - 2)
                height: meter.clipBoxHeight
                radius: AppMetrics.troughRadius
                color: side.clippedHere ? AppColors.dangerMark : AppColors.well
                Accessible.ignored: true
            }

            MeterItem {
                id: sideMeter

                width: meter.barWidth
                height: meter.barHeight

                horizontal: false
                channelCount: 1
                rmsDb: meter.channelOf(meter.levels.rmsDb, side.channel)
                peakDb: meter.channelOf(meter.levels.peakDb, side.channel)
                holdDb: meter.channelOf(meter.levels.holdDb, side.channel)
                active: meter.active
                // The box above says it; the strip would say it twice.
                clipped: meter.clipBoxes ? false : meter.clipped
                clippedChannels: meter.clipBoxes ? [] : meter.channelOf(meter.clippedChannels, side.channel)

                wellColor: AppColors.well
                rmsColor: meter.rmsColor
                peakColor: meter.peakColor
                holdColor: meter.holdColor
                clipColor: AppColors.dangerMark
                hotDb: meter.hotDb
                hotColor: meter.hotColor
            }

            Text {
                visible: meter.channelLabels
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: meter.channelName(side.channel)
                font: channelMetrics.font
                color: AppColors.muted
                Accessible.ignored: true
            }
        }
    }

    Row {
        id: splitRow

        visible: meter.split
        anchors.horizontalCenter: parent.horizontalCenter
        height: parent.height
        spacing: meter.splitSpacing

        Loader {
            id: leftBar

            active: meter.split
            sourceComponent: sideBar
            onLoaded: leftBar.item.channel = 0
        }

        // The numbers, between the bars and placed by the bars' own curve,
        // so each one sits at the level it names.
        Item {
            id: scaleColumn

            width: meter.showScale ? scaleMetrics.advanceWidth : 0
            height: splitRow.height

            Repeater {
                model: meter.split && meter.showScale ? meter.ticks : []

                delegate: Text {
                    required property real modelData

                    text: Math.round(modelData)
                    font: scaleMetrics.font
                    color: AppColors.muted
                    width: scaleColumn.width
                    horizontalAlignment: Text.AlignHCenter
                    y: meter.topPart + meter.yOf(modelData, meter.barHeight) - height / 2
                }
            }
        }

        Loader {
            id: rightBar

            active: meter.split
            sourceComponent: sideBar
            onLoaded: rightBar.item.channel = 1
        }
    }

    TextMetrics {
        id: channelMetrics

        font: AppFonts.small
        // Measured for its height only, in the words a side is labelled with.
        text: meter.channelName(0)
    }

    TextMetrics {
        id: scaleMetrics

        font: AppFonts.meterScale
        text: "-36"
    }

    // The numbers, placed by the same curve the bars are drawn with.
    Repeater {
        model: meter.showScale && !meter.split ? meter.ticks : []

        delegate: QQC2.Label {
            required property real modelData

            readonly property real place: MeterScale.positionOf(modelData)

            text: Math.round(modelData)
            font: AppFonts.small
            color: AppColors.muted
            horizontalAlignment: Text.AlignRight

            x: meter.horizontal ? bars.width * place - width / 2 : bars.width + Kirigami.Units.smallSpacing
            y: meter.horizontal ? bars.height + Kirigami.Units.smallSpacing
                                : bars.y + bars.height * (1 - place) - height / 2
        }
    }

    // **Clicking puts the light out.** A latch nobody can clear is a light
    // that stays on for the rest of the session and stops meaning anything.
    TapHandler {
        enabled: meter.clipped
        onTapped: Meters.resetClip(meter.blockId)
    }

    QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip", "Clipped — click to reset")
    QQC2.ToolTip.visible: meter.clipped && clipHover.hovered
    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

    HoverHandler {
        id: clipHover
    }
}
