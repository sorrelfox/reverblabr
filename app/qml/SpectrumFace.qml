// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import org.kde.kirigami as Kirigami

// What a Spectrum shows on the canvas: the bars, as the sound goes by.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property SpectrumState reading: SpectrumState {
        blockId: face.blockId
    }
    readonly property alias scope: well.scope
    readonly property alias scopeWell: well
    /// The tile's dot: "live", "warning" or "off".
    readonly property string dot: face.reading.words.dot(well.scope.feedState, face.reading.hold)

    implicitWidth: Kirigami.Units.gridUnit * 10
    implicitHeight: Kirigami.Units.gridUnit * 5

    Accessible.role: Accessible.StaticText
    Accessible.name: face.reading.words.status(well.scope.feedState, face.reading.hold)

    SpectrumScope {
        id: well

        reading: face.reading
        anchors {
            fill: parent
            leftMargin: AppMetrics.faceMargin
            rightMargin: AppMetrics.faceMargin
            topMargin: AppMetrics.faceSpacing
            bottomMargin: AppMetrics.faceSpacing
        }
    }
}
