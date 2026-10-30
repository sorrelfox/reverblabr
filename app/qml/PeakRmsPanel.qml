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

// The Peak/RMS block's own settings panel: the two sides at full height
// with the scale between them, and what to do about a peak that has gone.
//
// **Almost all meter, as the design draws it**. Bar, scale, bar, with
// a clip box above each side and its name under it. The RMS is in the live
// colour, the tip up to the peak paler, anything above −6 dBFS red, and the
// line that holds and falls white. Then whether the tip is drawn at all,
// the tray and the hold, and nothing else. The numbers that used to be written out in a table are each side's
// tooltip and what a screen reader says of it. A lit clip box goes out when
// clicked. The card is narrow and has no presets row, because a meter's
// only settings are how it behaves on the screen.
//
// **Peak comes from the graph and the root mean square from the block.**
// Every block's output is metered where it is made, which gives the peak,
// the clip latch and the activity flag for nothing; what this block adds is
// the calibrated part, a root mean square over a window of a fixed length,
// which it publishes as an amplitude.
//
// **The waiting is the interface's.** Wall-clock time lives here — in the
// meter hub, where a hold looks the same on a 60 Hz screen and a 144 Hz one
// — so the block keeps only which hold was chosen. It is a setting per
// block and not one for the window: two meters at different points
// in a lane are exactly where different holds earn their keep.
//
// **And the panel does not tell the hub about it**. It writes the
// choice to the session like every other control, and BlockModel carries
// every meter block's choice to the hub from there. A panel that told the
// hub itself was why a meter whose panel nobody had opened held for the
// hub's default instead of for its own setting: what the hub knows cannot
// depend on what is on the screen.
ColumnLayout {
    id: panel

    /// The block whose settings these are. **Not a required property**: the
    /// panel is made from a Component in the register, which cannot see the
    /// loader's context, so the settings panel binds it once it exists.
    property string blockId: ""

    /// Re-read when the session changes, so a value set anywhere — undo, a
    /// second window — is what the panel shows.
    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(panel.blockId) : ({})
    readonly property string holdChoice: panel.params.peakHold ?? PeakHold.defaultChoice
    readonly property bool inTray: panel.params.showInTray === true
    /// On unless it was turned off, so a session from before the setting
    /// draws the peak as it always did.
    readonly property bool showPeak: panel.params.showPeak !== false

    readonly property var levels: Meters.frame >= 0 ? Meters.levels(panel.blockId) : ({})
    readonly property bool active: Meters.live && (panel.levels.active ?? false)
    readonly property bool clipped: panel.levels.clipped ?? false

    /// The held peak of each side, in dBFS. **The held one and not the
    /// instant one**: a number that changed sixty times a second would be
    /// something to watch rather than something to read.
    readonly property real peakLeftDb: panel.levelAt(panel.levels.holdDb, 0)
    readonly property real peakRightDb: panel.levelAt(panel.levels.holdDb, 1)
    /// The block's own root mean square, published as an amplitude.
    readonly property real rmsLeftDb: MeterScale.dbOf(panel.levelAt(panel.levels.values, 0))
    readonly property real rmsRightDb: MeterScale.dbOf(panel.levelAt(panel.levels.values, 1))

    /// The locale the numbers are written in. The user's, unless a test
    /// says otherwise.
    property var locale: Qt.locale()

    /// The controls themselves, so that a test can drive what a person
    /// drives rather than the function behind it.
    readonly property alias holdBox: holdControl
    readonly property alias holdMenu: holdMenu
    readonly property alias traySwitch: trayControl
    readonly property alias peakSwitch: peakControl
    readonly property alias meter: meterColumn

    /// What this panel asks of the card it is in: a narrow one, and no
    /// presets row.
    /// Narrow, as the design draws it, and still wide enough for the
    /// shared header to say the block's name.
    readonly property real panelWidth: Kirigami.Units.gridUnit * 15
    readonly property bool wantsPresets: false

    /// Where a bar turns red: the last 6 dB before full scale.
    readonly property real hotDb: -6

    /// The marks down the middle: full scale to the floor, as the design
    /// writes them.
    readonly property var ticks: [0, -3, -6, -9, -12, -18, -24, -30, -36, -48, -60]

    /// What a side reads, in words, for its tooltip and a screen reader.
    function sideText(channel: int): string {
        const peak = channel === 0 ? panel.peakLeftDb : panel.peakRightDb;
        const rms = channel === 0 ? panel.rmsLeftDb : panel.rmsRightDb;
        return KI18n.i18nc("@info:tooltip one side's held peak and its root mean square, 'Peak −12.0 dB, RMS −15.0 dB'",
                           "Peak %1, RMS %2", panel.formatDb(peak), panel.formatDb(rms));
    }

    /// A hold setting, short, for beside its label.
    function holdShort(choiceId: string): string {
        switch (choiceId) {
        case "off":
            return KI18n.i18nc("@item the peak line does not wait at all, short", "Off");
        case "1s":
            return KI18n.i18nc("@item how long the peak line waits, short", "1 s");
        case "2s":
            return KI18n.i18nc("@item how long the peak line waits, short", "2 s");
        case "5s":
            return KI18n.i18nc("@item how long the peak line waits, short", "5 s");
        case "forever":
            return KI18n.i18nc("@item the peak line never comes down, short", "Until reset");
        }
        return choiceId;
    }

    spacing: 0

    function levelAt(values, channel: int): real {
        const list = values ?? [];
        return channel < list.length ? list[channel] : MeterScale.floorDb;
    }

    // **A tenth of a decibel, and no more.** It is what a person can act on
    // and what the measurement is worth; anything finer would be reading a
    // number that moves under the eye.
    function formatDb(db: real): string {
        if (db <= MeterScale.floorDb) {
            return KI18n.i18nc("@info a level too quiet to put a number on", "—");
        }
        return KI18n.i18nc("@info a level in decibels below full scale", "%1 dB",
                           Number(db).toLocaleString(panel.locale, 'f', 1));
    }

    // The name a person reads for one of the hold settings. **Not from the
    // engine**: the catalogue holds stable ids and the words are the
    // interface's, translated and keyed by them.
    function holdLabel(choiceId: string): string {
        return ChoiceWords.label("meter.peakrms", "peakHold", choiceId);
    }

    // --- the meters themselves ---
    LevelMeter {
        id: meterColumn

        blockId: panel.blockId
        scaleBetween: true
        showScale: true
        ticks: panel.ticks
        clipBoxes: true
        channelLabels: true
        channelClips: true
        showPeak: panel.showPeak
        barWidth: Math.round(Kirigami.Units.gridUnit * 0.9)
        splitSpacing: Kirigami.Units.gridUnit * 1.6
        rmsColor: AppColors.live
        peakColor: AppColors.meterHot
        hotDb: panel.hotDb
        hotColor: AppColors.danger
        holdColor: AppColors.text
        Layout.fillWidth: true
        Layout.preferredHeight: Kirigami.Units.gridUnit * 16
        Layout.topMargin: Kirigami.Units.largeSpacing
        Layout.bottomMargin: Kirigami.Units.largeSpacing

        // Each side's numbers, where the pointer is and for a screen
        // reader: the table they used to be written in is gone.
        Repeater {
            model: 2

            delegate: Item {
                id: sideTip

                required property int index
                readonly property Item side: meterColumn.sideBars[sideTip.index] ?? null

                x: sideTip.side ? sideTip.side.mapToItem(meterColumn, 0, 0).x : 0
                width: sideTip.side ? sideTip.side.width : 0
                height: meterColumn.height

                Accessible.role: Accessible.Indicator
                Accessible.name: sideTip.index === 0
                    ? KI18n.i18nc("@info the left channel's meter", "Left")
                    : KI18n.i18nc("@info the right channel's meter", "Right")
                Accessible.description: panel.sideText(sideTip.index)

                QQC2.ToolTip.text: panel.sideText(sideTip.index)
                QQC2.ToolTip.visible: tipHover.hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

                HoverHandler {
                    id: tipHover
                }
            }
        }
    }

    Kirigami.Separator {
        Layout.fillWidth: true
    }

    // --- the peak bar, the tray, and how long a peak stays up ---
    ColumnLayout {
        Layout.fillWidth: true
        Layout.margins: Kirigami.Units.smallSpacing * 2
        spacing: Kirigami.Units.smallSpacing

        QQC2.CheckBox {
            id: peakControl

            text: KI18n.i18nc("@option:check", "Show peak bar")
            Layout.fillWidth: true
            QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip",
                                           "Draws the paler bar that jumps up to each peak above the RMS bar")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            onToggled: SessionModel.setParam(panel.blockId, "showPeak", peakControl.checked)

            SessionBound {
                control: peakControl
                property: "checked"
                value: panel.showPeak
            }
        }

        QQC2.CheckBox {
            id: trayControl

            text: KI18n.i18nc("@option:check", "Show in system tray")
            Layout.fillWidth: true
            // **In the popover of Reverblabr's tray icon**, where a
            // click on it brings this session forward.
            QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip",
                                           "Shows this meter when you click Reverblabr's icon in the system tray")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            onToggled: SessionModel.setParam(panel.blockId, "showInTray", trayControl.checked)

            SessionBound {
                control: trayControl
                property: "checked"
                value: panel.inTray
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                text: KI18n.i18nc("@label:listbox how long the peak line waits before falling", "Hold peaks")
                color: AppColors.muted
                Layout.fillWidth: true
                elide: Text.ElideRight
            }

            // **The value, as the design writes it, and still a choice.** A
            // flat button shows the setting at the end of the row; pressing
            // it offers the others by their full names. Not a combo box: the
            // desktop style draws a combo box's text itself, and a second
            // label over it wrote the value twice.
            QQC2.ToolButton {
                id: holdControl

                text: panel.holdShort(panel.holdChoice)
                font.family: AppFonts.monoFamily
                font.weight: Font.DemiBold
                Accessible.name: KI18n.i18nc("@label:listbox how long the peak line waits before falling", "Hold peaks")
                Accessible.description: panel.holdLabel(panel.holdChoice)
                onClicked: holdMenu.popup(holdControl, 0, holdControl.height)

                QQC2.Menu {
                    id: holdMenu

                    Instantiator {
                        model: PeakHold.choices

                        delegate: QQC2.MenuItem {
                            required property string modelData

                            text: panel.holdLabel(modelData)
                            checkable: true
                            checked: panel.holdChoice === modelData
                            // An enum travels as its choice's stable id,
                            // never as its position: a later version may
                            // reorder the choices.
                            onTriggered: SessionModel.setParam(panel.blockId, "peakHold", modelData)
                        }

                        onObjectAdded: (index, object) => holdMenu.insertItem(index, object)
                        onObjectRemoved: (index, object) => holdMenu.removeItem(object)
                    }
                }
            }
        }
    }
}
