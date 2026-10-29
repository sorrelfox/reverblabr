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

// What a VU block shows on the canvas: two small dials side by side,
// left and right, as the design draws its VU tile.
//
// **Nothing is written on them.** What the needles read, in VU against the
// block's reference, is the tooltip and what a screen reader says; the dial
// itself is for the glance that tells "about right" from "too hot".
//
// **Idle is not silence**, as with every meter: with nothing running the
// needles rest on their pins in the muted colour, and the header's state dot
// says whether sound is going through.
Item {
    id: face

    /// The block whose face this is. **Not a required property**: a face is
    /// made from a Component in the register, which cannot see the context
    /// it is loaded into, so it is set once the face exists.
    property string blockId: ""

    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(face.blockId) : ({})
    readonly property string reference: face.params.reference ?? VuScale.defaultReference

    readonly property var levels: Meters.frame >= 0 ? Meters.levels(face.blockId) : ({})
    readonly property bool active: Meters.live && (face.levels.active ?? false)
    readonly property var clippedChannels: face.levels.clippedChannels ?? []
    /// Each needle's reading in VU, against the block's own reference.
    readonly property real leftVu: VuScale.vuOf((face.levels.values ?? [])[0] ?? 0, face.reference)
    readonly property real rightVu: VuScale.vuOf((face.levels.values ?? [])[1] ?? 0, face.reference)

    /// What the needles read, in words, for the tile's tooltip.
    readonly property string detail: face.active
        ? KI18n.i18nc("@info the two needles' readings, 'L −3.0 VU, R −2.5 VU'", "L %1, R %2",
                      face.formatVu(face.leftVu), face.formatVu(face.rightVu))
        : KI18n.i18nc("@info nothing is running", "Idle")

    /// The dials, so that a test can read what a person reads.
    readonly property alias leftDial: leftGauge
    readonly property alias rightDial: rightGauge

    function formatVu(vu: real): string {
        if (vu <= VuScale.bottomVu) {
            return KI18n.i18nc("@info a VU needle resting on its pin, 'Below −20 VU'", "Below %1 VU",
                               Number(VuScale.bottomVu).toLocaleString(Qt.locale(), 'f', 0));
        }
        return KI18n.i18nc("@info a VU reading", "%1 VU", Number(vu).toLocaleString(Qt.locale(), 'f', 1));
    }

    implicitWidth: Kirigami.Units.gridUnit * 6
    implicitHeight: Kirigami.Units.gridUnit * 4

    Accessible.role: Accessible.StaticText
    Accessible.name: face.detail

    Row {
        id: dials

        // The design's two 46-pixel dials, ten apart, as large as the face
        // allows and no larger.
        readonly property real dialWidth: Math.min((face.width - dials.spacing) / 2 - Kirigami.Units.smallSpacing,
                                                   face.height * 46 / 30)

        anchors.centerIn: parent
        spacing: Kirigami.Units.smallSpacing * 2.5

        VuGauge {
            id: leftGauge

            width: dials.dialWidth
            height: implicitHeight
            position: VuScale.positionOf(face.leftVu)
            active: face.active
            clipped: face.clippedChannels[0] === true
        }

        VuGauge {
            id: rightGauge

            width: dials.dialWidth
            height: implicitHeight
            position: VuScale.positionOf(face.rightVu)
            active: face.active
            clipped: face.clippedChannels[Math.min(1, Math.max(0, face.clippedChannels.length - 1))] === true
        }
    }
}
