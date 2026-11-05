// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.ki18n

// What a System Audio block shows on the canvas: the block's own icon, how
// much of the machine it is taking, and whether anything is coming through.
//
// **The line under the icon is the setting, not the reading.** "Everything"
// or "everything except these two" is what somebody chose and can check at a
// glance; how many streams that works out to right now changes every time an
// application opens a sound and is nobody's idea of a label. So the words
// come from the exclusion list, which is also why a stopped session still
// says what the block is set to. Whether anything is coming through is the
// tile's state dot, not a second dot on the face.
Item {
    id: face

    /// The block whose face this is. **Not a required property**: a face is
    /// made from a Component in the register, and a Loader cannot hand a
    /// Component its properties, so it is set once the face exists.
    property string blockId: ""

    /// The type whose drawing and words this face uses. It is the one type
    /// this face is for, so it is written down once rather than being asked
    /// of the session on every binding.
    readonly property string typeId: "source.system"

    /// The applications left out, by match rule. Re-read when the session
    /// changes, so an exclusion added in the panel is on the tile at once.
    readonly property var excluded: SessionModel.revision >= 0
        ? (SessionModel.param(face.blockId, "excluded") ?? [])
        : []

    readonly property var levels: Meters.frame >= 0 ? Meters.levels(face.blockId) : ({})
    /// How many streams the block has hold of — the first of the values a
    /// System Audio block publishes about itself. **The engine counts; it
    /// does not name.** What that number means to a reader is decided here,
    /// in their language.
    readonly property int streamCount: (face.levels.values ?? []).length > 0
        ? Math.round(face.levels.values[0])
        : 0
    /// Whether anything is actually being taken. A session that is not
    /// running and one where nothing is playing are the same dot, on
    /// purpose: in both, nothing is being captured.
    readonly property bool capturing: Meters.live && face.streamCount > 0

    /// What the block is set to take.
    readonly property string subtitle: face.excluded.length === 0
        ? KI18n.i18nc("@info the System Audio block is leaving nothing out", "All system audio")
        : KI18n.i18ncp("@info the System Audio block is leaving some applications out",
                       "All except %1 app", "All except %1 apps", face.excluded.length)

    /// The one line the tile shows, and everything there is to say.
    readonly property string line: face.subtitle
    readonly property string detail: face.capturing
        ? KI18n.i18ncp("@info:tooltip what System Audio takes, then how many streams that is now",
                       "%2 — %1 stream", "%2 — %1 streams", face.streamCount, face.subtitle)
        : face.subtitle

    /// The parts, so that a test reads what a person reads.
    readonly property alias icon: blockIcon
    readonly property alias lineLabel: lineText

    implicitWidth: Kirigami.Units.gridUnit * 8
    implicitHeight: Kirigami.Units.gridUnit * 5

    Accessible.role: Accessible.StaticText
    Accessible.name: face.subtitle

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: AppMetrics.faceMargin
        spacing: AppMetrics.faceSpacing

        Item {
            Layout.fillHeight: true
        }

        Kirigami.Icon {
            id: blockIcon

            // The block's own drawing, as the library and the canvas use it:
            // a single-colour mask, tinted by the category it belongs to, so
            // that a glance places it among the sources before a word of it
            // has been read.
            source: BlockPresentation.icon(face.typeId)
            color: AppColors.categoryTint(BlockPresentation.category(face.typeId))
            isMask: true
            implicitWidth: AppMetrics.faceIconSize
            implicitHeight: AppMetrics.faceIconSize
            Layout.alignment: Qt.AlignHCenter
            // The line below says it in words.
            Accessible.ignored: true
        }

        ElidedLabel {
            id: lineText

            text: face.line
            color: AppColors.muted
            font: AppFonts.faceLine
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
