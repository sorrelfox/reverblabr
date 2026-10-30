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

// What a Frequency Lowering or Frequency Raising block shows on
// the canvas: its first move, "4k → 2.5k", and how many more it has, "(+1)";
// under it, whether it is moving sound now, or how its first move moves it.
// The ear's badge is the tile's own. FrequencyLoweringFace and
// FrequencyRaisingFace set typeId.
Item {
    id: face

    /// Which block this is the face of.
    property alias typeId: moves.typeId
    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(face.blockId) : ({})
    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(face.blockId).enabled ?? true)

    /// The moves that act, in slot order: in use, on, and going the block's
    /// way.
    readonly property var acting: moves.used.filter(slot => moves.move(slot).active)
    /// Which moved sound on the last frame, as FrequencyMoveBlock::
    /// kActingValue's bits, while the session runs.
    readonly property var levels: Meters.frame >= 0 ? Meters.levels(face.blockId) : ({})
    readonly property int movingBits: Meters.live ? Math.round((face.levels.values ?? [])[0] ?? 0) : 0

    readonly property alias summaryLabel: summaryText
    readonly property alias caption: captionText

    implicitWidth: Kirigami.Units.gridUnit * 12
    implicitHeight: Kirigami.Units.gridUnit * 6

    FrequencyMoves {
        id: moves

        params: face.params
    }

    FrequencyMoveWords {
        id: words
    }

    ColumnLayout {
        anchors {
            left: parent.left
            right: parent.right
            verticalCenter: parent.verticalCenter
            leftMargin: Kirigami.Units.smallSpacing * 3
            rightMargin: Kirigami.Units.smallSpacing * 3
        }
        spacing: Kirigami.Units.smallSpacing

        ElidedLabel {
            id: summaryText

            text: {
                if (face.acting.length === 0) {
                    return KI18n.i18nc("@info a Frequency Lowering or Raising tile with no move that acts", "No moves");
                }
                const first = moves.move(face.acting[0]);
                return words.summary(first.sourceLow, first.destinationLow, face.acting.length - 1);
            }
            font: AppFonts.readout
            lineHeightMode: AppMetrics.followsSystem ? Text.ProportionalHeight : Text.FixedHeight
            lineHeight: AppMetrics.followsSystem ? 1.0 : AppFonts.readoutPixels
            color: face.blockEnabled && face.acting.length > 0 ? AppColors.text : AppColors.muted
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        ElidedLabel {
            id: captionText

            text: {
                if (face.acting.length === 0) {
                    return "";
                }
                if (face.movingBits !== 0) {
                    return KI18n.i18nc("@info a Frequency Lowering or Raising tile while one of its moves is moving sound",
                                       "moving sound");
                }
                const first = moves.move(face.acting[0]);
                if (first.method === "harmonics") {
                    return KI18n.i18nc("@info a Frequency Raising tile whose first move gives low sounds their harmonics",
                                       "harmonics added");
                }
                if (moves.raises) {
                    return first.method === "transpose"
                        ? KI18n.i18nc("@info a Frequency Raising tile whose first move copies sound up", "copied up")
                        : KI18n.i18nc("@info a Frequency Raising tile whose first move squeezes sound up",
                                      "squeezed up");
                }
                return first.method === "transpose"
                    ? KI18n.i18nc("@info a Frequency Lowering tile whose first move copies sound down",
                                  "copied down")
                    : KI18n.i18nc("@info a Frequency Lowering tile whose first move squeezes sound down",
                                  "squeezed down");
            }
            font: AppFonts.tileCaption
            color: face.movingBits !== 0 && face.blockEnabled ? AppColors.liveText : AppColors.muted
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }
}
