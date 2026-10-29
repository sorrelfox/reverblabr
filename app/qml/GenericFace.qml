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

// What a tile shows for a block that has no face of its own yet.
//
// **A block declares what kind of face it wants** — meter, value, icon,
// text, bars or curve — before anybody has drawn one for it, and the canvas
// has to put something on the tile either way. What it draws here comes
// from what the block already says about itself: its levels, its
// parameters, its descriptors. Nothing is invented.
//
// **This is a fallback, not a lesser face.** For a block whose settings are
// a list of settings it is the right face and will stay, the same way the
// generic settings panel is. A block that wants something else adds its own
// component to BlockFaces, and this stops being asked.
//
// **There is no generic curve.** A response curve is worked out from what
// the block does to a signal, which is the block's business and nothing the
// interface can guess; a drawn line that means nothing would be worse than
// no line. So a `curve` block shows what it is until it draws its own.
Item {
    id: face

    /// The block whose face this is. **Not a required property**: a face is
    /// made from a Component the tile holds, which cannot be handed its
    /// properties, so it is set once the face exists.
    property string blockId: ""
    /// What the block asked for: meter, value, icon, text, bars or curve.
    property string kind: "icon"

    readonly property string typeId: SessionModel.revision >= 0 ? (SessionModel.block(face.blockId).type ?? "") : ""
    /// Re-read when the session changes, so a value set anywhere — the
    /// panel, undo, a second window — is what the face shows.
    readonly property var values: SessionModel.revision >= 0 ? SessionModel.params(face.blockId) : ({})

    /// The parameters worth putting on a tile: the ones the block does not
    /// hide, in the order it declares them, which is the order it thinks
    /// they matter in.
    readonly property var shown: {
        const params = BlockCatalogue.ready ? BlockCatalogue.params(face.typeId) : [];
        const kept = [];
        for (const descriptor of params) {
            if (descriptor.hidden !== true) {
                kept.push(descriptor);
            }
        }
        return kept;
    }

    /// What the body of the face is showing, for a test and for the tile's
    /// accessible name.
    readonly property alias body: body

    implicitWidth: Kirigami.Units.gridUnit * 6
    implicitHeight: Kirigami.Units.gridUnit * 3

    /// The first parameter whose type is \a wanted, or undefined.
    function firstOf(wanted: var): var {
        for (const descriptor of face.shown) {
            if (wanted.indexOf(descriptor.type ?? "") >= 0) {
                return descriptor;
            }
        }
        return undefined;
    }

    /// What a parameter reads at the moment, falling back to its default —
    /// **a parameter the session does not mention is at its default**, which
    /// is the descriptor's business and not the session's.
    function valueOf(descriptor: var): var {
        const set = face.values[descriptor.id];
        return set === undefined ? descriptor.default : set;
    }

    Loader {
        id: body

        anchors.fill: parent
        // The tile gives a face its whole width; the margin is the
        // face's own.
        anchors.margins: Kirigami.Units.smallSpacing
        sourceComponent: {
            switch (face.kind) {
            case "meter": return levels;
            case "value": return number;
            case "text": return words;
            case "bars": return columns;
            }
            // icon, curve, and anything a later engine invents.
            return emblem;
        }
    }

    Component {
        id: levels

        LevelMeter {
            blockId: face.blockId
            channelClips: true
        }
    }

    Component {
        id: number

        // The block's first number, as large as the tile allows. A tile is
        // glanced at rather than read, so one reading in a size that can be
        // taken in at a glance beats four in a size that cannot.
        ElidedLabel {
            readonly property var descriptor: face.firstOf(["float", "int"])

            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            color: AppColors.text
            font: AppFonts.tileValue
            text: descriptor === undefined
                ? KI18n.i18nc("@info:placeholder a block with no reading to put on its tile", "—")
                : ParamFormat.formatted(Number(face.valueOf(descriptor)), descriptor.type ?? "float",
                                        descriptor.unit ?? "none", Qt.locale())
        }
    }

    Component {
        id: words

        // The block's first piece of text — the application it captured,
        // the name it is publishing under — and what the block is for when
        // it has none to show yet.
        ElidedLabel {
            readonly property var descriptor: face.firstOf(["string", "deviceRef", "enum"])
            readonly property string written: descriptor === undefined ? "" : String(face.valueOf(descriptor) ?? "")

            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap
            elide: Text.ElideRight
            maximumLineCount: 2
            color: written === "" ? AppColors.muted : AppColors.text
            text: written === "" ? BlockPresentation.description(face.typeId) : written
        }
    }

    Component {
        id: columns

        // One bar per setting, each at its own place in its own range: for a
        // band equaliser that is the shape of the curve, and for anything
        // else it is still an honest picture of what the block is set to.
        RowLayout {
            spacing: Math.max(1, Math.round(Kirigami.Units.smallSpacing / 2))

            Repeater {
                model: face.firstOf(["float", "int"]) === undefined ? [] : face.shown

                delegate: Item {
                    required property var modelData

                    readonly property real minimum: modelData.minimum ?? 0
                    readonly property real maximum: modelData.maximum ?? 1
                    readonly property bool numeric: (modelData.type ?? "") === "float"
                        || (modelData.type ?? "") === "int"
                    // A range of nothing would divide by zero, and a bar
                    // that is always full says nothing anyway.
                    readonly property real fraction: !numeric || maximum <= minimum
                        ? 0
                        : Math.max(0, Math.min(1, (Number(face.valueOf(modelData)) - minimum) / (maximum - minimum)))

                    visible: numeric
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Rectangle {
                        anchors.fill: parent
                        color: AppColors.well
                        radius: AppMetrics.barRadius
                    }

                    Rectangle {
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                        height: Math.round(parent.height * parent.fraction)
                        color: AppColors.accent
                        radius: AppMetrics.barRadius
                    }
                }
            }
        }
    }

    Component {
        id: emblem

        // The block itself, in its category's colour. What a block with
        // nothing to read says is which block it is.
        Kirigami.Icon {
            source: BlockPresentation.icon(face.typeId)
            color: AppColors.categoryTint(BlockPresentation.category(face.typeId))
            isMask: true
            // The tile's header already names the block; a screen reader
            // reading the icon as well would say it twice.
            Accessible.ignored: true
        }
    }
}
