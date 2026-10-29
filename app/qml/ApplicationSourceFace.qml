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

// What an Application block shows on the canvas: which program it is
// listening to, and whether anything is coming out of it.
//
// **One line, and it is the application**. Which program the block
// is pointed at is what somebody chose and wants to check at a glance. The
// tile used to add the block's own name above it, and a dot and a word
// below it for whether the program was playing — three things at a width
// where each of them was cut off. Whether it is playing is said by the
// program's own icon (below) and the tile's state dot; how many streams it
// has is in the tooltip. A block nobody has pointed at anything says so.
//
// **The program's own icon while it is playing.** It is what somebody
// recognises without reading, and the desktop file is where it comes from;
// with the program closed there is nothing to ask, so the block falls back
// to its own drawing.
Item {
    id: face

    /// The block whose face this is. **Not a required property**: a face is
    /// made from a Component in the register, and a Loader cannot hand a
    /// Component its properties, so it is set once the face exists.
    property string blockId: ""

    /// The type whose drawing and words this face uses. It is the one type
    /// this face is for, so it is written down once rather than being asked
    /// of the session on every binding.
    readonly property string typeId: "source.application"

    /// The application the block is pointed at, as one match rule. The
    /// session holds it as three fields, in the order the engine matches
    /// them; turning those into a rule is the inventory's job, so that the
    /// face and the panel cannot disagree about it.
    readonly property string rule: SessionModel.revision >= 0
        ? Inventory.applicationRuleFrom(SessionModel.param(face.blockId, "binary") ?? "",
                                        SessionModel.param(face.blockId, "flatpakId") ?? "",
                                        SessionModel.param(face.blockId, "name") ?? "")
        : ""
    readonly property bool pointedAtSomething: face.rule !== ""

    /// What the application is called, and what it looks like, while it is
    /// playing.
    readonly property var application: Inventory.revision >= 0 && face.pointedAtSomething
        ? Inventory.application(face.rule)
        : ({})

    readonly property var levels: Meters.frame >= 0 ? Meters.levels(face.blockId) : ({})
    /// How many of the application's streams the block has hold of — the
    /// first of the values an Application block publishes about itself.
    /// **The engine counts; it does not name.** What that number means to a
    /// reader is decided here, in their language.
    readonly property int streamCount: (face.levels.values ?? []).length > 0
        ? Math.round(face.levels.values[0])
        : 0
    /// Whether anything is actually being taken. A session that is not
    /// running and one where the application is closed are the same dot, on
    /// purpose: in both, nothing is being captured.
    readonly property bool capturing: Meters.live && face.streamCount > 0

    /// Which program this is, or the block's own name until somebody says.
    readonly property string title: face.pointedAtSomething
        ? (RecentApplications.nameFor(face.rule) || Inventory.applicationName(face.rule))
        : BlockPresentation.name(face.typeId)

    /// What is happening to it.
    readonly property string subtitle: !face.pointedAtSomething
        ? KI18n.i18nc("@info the Application block has not been pointed at a program yet", "Choose an app")
        : face.capturing
        ? KI18n.i18ncp("@info how many of the application's sounds are being captured",
                       "%1 stream", "%1 streams", face.streamCount)
        : KI18n.i18nc("@info the application the block is pointed at is not playing", "Waiting…")

    /// The one line the tile shows.
    readonly property string line: face.pointedAtSomething ? face.title : face.subtitle
    /// Everything there is to say, for the tooltip.
    readonly property string detail: face.pointedAtSomething
        ? KI18n.i18nc("@info:tooltip an application and what is happening to it, 'Firefox — 2 streams'",
                      "%1 — %2", face.title, face.subtitle)
        : face.subtitle

    /// The parts, so that a test reads what a person reads.
    readonly property alias icon: applicationIcon
    readonly property alias lineLabel: lineText

    implicitWidth: Kirigami.Units.gridUnit * 8
    implicitHeight: Kirigami.Units.gridUnit * 5

    Accessible.role: Accessible.StaticText
    Accessible.name: face.title
    Accessible.description: face.subtitle

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: AppMetrics.faceMargin
        spacing: AppMetrics.faceSpacing

        Item {
            Layout.fillHeight: true
        }

        Kirigami.Icon {
            id: applicationIcon

            // The program's own icon where the desktop file gave one, and
            // the block's own drawing where it did not — tinted by the
            // category it belongs to, as the library and the canvas draw it.
            readonly property string applicationIconName: face.application.icon ?? ""

            source: applicationIcon.applicationIconName !== ""
                ? applicationIcon.applicationIconName
                : BlockPresentation.icon(face.typeId)
            color: AppColors.categoryTint(BlockPresentation.category(face.typeId))
            isMask: applicationIcon.applicationIconName === ""
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
