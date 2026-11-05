// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.ki18n

// What a 10-Band EQ shows on the canvas: its ten bands as ten bars.
//
// **As the design draws a bars face**: narrow bars side by side, standing
// on one line, in the effects colour. Each is as tall as its band is set,
// from a stub at −12 dB to full height at +12, so a flat EQ is a level row
// and a lift or a cut is seen at a glance as the shape it is. Nothing to
// drag: ten sliders need the panel.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(face.blockId) : ({})
    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(face.blockId).enabled ?? true)
    /// The effects colour, or grey for a block that is off.
    readonly property color tint: face.blockEnabled
        ? AppColors.categoryTint(BlockPresentation.category("effect.equalizer10"))
        : AppColors.muted
    /// Each band's gain, low to high.
    readonly property var gains: bands.ids.map(id => face.params[id] ?? 0)
    readonly property bool flat: face.gains.every(gain => Math.abs(gain) < 0.05)

    /// The tallest a bar gets, and the stub one at −12 dB still shows.
    readonly property real tallest: Math.round(Kirigami.Units.gridUnit * 2.5)
    readonly property real shortest: 3

    /// Everything there is to say, for the tooltip: the bands that
    /// do something, low to high, or that it is flat.
    readonly property string detail: {
        const lifted = [];
        for (let index = 0; index < bands.ids.length; ++index) {
            if (Math.abs(face.gains[index]) >= 0.05) {
                lifted.push(KI18n.i18nc("@info:tooltip one band of an equaliser, '1 kHz +3.0 dB'", "%1 %2",
                                        words.bandText(bands.frequencies[index]), words.gainText(face.gains[index])));
            }
        }
        return lifted.length === 0 ? KI18n.i18nc("@info:tooltip an equaliser with every band at 0 dB", "Flat")
                                   : lifted.join(KI18n.i18nc("@info:tooltip between two bands of an equaliser", ", "));
    }

    /// The bars, for a test.
    readonly property alias bars: barRow

    implicitWidth: Kirigami.Units.gridUnit * 12
    implicitHeight: Kirigami.Units.gridUnit * 6

    EqualizerBands {
        id: bands
    }

    FilterWords {
        id: words
    }

    /// How tall a bar is for a gain.
    function heightOf(gainDb: real): real {
        const fraction = (Math.max(-bands.rangeDb, Math.min(bands.rangeDb, gainDb)) + bands.rangeDb) / (2 * bands.rangeDb);
        return Math.round(face.shortest + fraction * (face.tallest - face.shortest));
    }

    Row {
        id: barRow

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        height: face.tallest
        spacing: 4

        Repeater {
            model: bands.ids.length

            delegate: Rectangle {
                required property int index

                anchors.bottom: parent.bottom
                width: 6
                height: face.heightOf(face.gains[index])
                radius: AppMetrics.barRadius
                color: face.tint
                Accessible.ignored: true
            }
        }
    }
}
