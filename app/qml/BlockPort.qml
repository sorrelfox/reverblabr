// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// One place a link can be attached to a tile.
//
// **No name beside it**. There used to be one per channel, reading
// the engine's own port ids — `out_FL`, `out_FR` — which is the engine's
// port list showing through the interface, and on a narrow tile the words
// ran into the face. A tile now shows one connector a side, and a block
// with one connector has nothing to tell it apart from.
//
// **The ring straddles the edge.** Half of it is inside the tile and half
// outside, so that a wire drawn to it meets the tile rather than stopping
// short of it. The item puts *itself* where its dot has to be: the canvas
// works out where each port sits, because a wire has to be drawn to a
// port whether that tile has been built yet or not, and a port that placed
// itself to its own taste would be a port the wires missed.
Item {
    id: port

    /// Whether it is on the right-hand edge rather than the left.
    property bool output: false

    /// Where the wire meets it, in the parent's coordinates. The item
    /// places itself so that the middle of its dot lands exactly there.
    property point attachTo: Qt.point(0, 0)

    /// Whether a wire being drawn could be dropped here. **Only the ports
    /// that would take it light up**, so that a drag that cannot end well
    /// says so before the button comes up rather than afterwards.
    property bool candidate: false
    /// Whether the wire being drawn is hovering over this port.
    property bool aimedAt: false
    /// Whether more than one link arrives here, which the engine sums.
    property bool merging: false
    /// Whether audio is going through the block this connector belongs to.
    /// **The tile's own reading**, the one its state dot shows, so
    /// that a connector and the dot above it cannot disagree.
    property bool live: false
    /// What this connector is for, on a block that takes more than one
    /// signal: "Main", "Background". Empty on an edge with one, where
    /// there is nothing to tell apart. Said on hover and to a screen reader.
    property string name: ""

    /// The ring and the merge marker, for the tests.
    readonly property alias ring: dot
    readonly property alias mergeRing: mergeRing

    /// A Ctrl-drag began, moved and ended on this port. **In scene
    /// coordinates**, because what is on the other end of them is the
    /// canvas, several items and one scale factor away.
    signal wireStarted()
    signal wireMoved(real sceneX, real sceneY)
    signal wireDropped(real sceneX, real sceneY)
    signal wireCancelled()

    x: port.output ? port.attachTo.x - port.width + dot.width / 2 : port.attachTo.x - dot.width / 2
    y: port.attachTo.y - port.height / 2

    implicitWidth: dot.width
    implicitHeight: dot.height

    // **A ring, not a dot**: the canvas colour inside and the colour
    // on the border. The border takes the live colour while the block is
    // passing audio, as the wires either side of it already do, rather than
    // staying grey between two orange lines; and a hollow ring still reads
    // as a place to attach something when it is orange.
    Rectangle {
        id: dot

        x: port.output ? port.width - width : 0
        y: Math.round((port.height - height) / 2)
        width: AppMetrics.connectorSize
        height: width
        radius: width / 2
        // **Aimed at is filled**: the one connector the wire will land on
        // if the button comes up now says so more loudly than the ones it
        // merely could.
        color: port.aimedAt ? AppColors.accentMark : AppColors.canvas
        border.width: 2
        // **The gesture wins over the signal.** While a wire is being
        // drawn, what matters is where it may land, not what is playing.
        border.color: port.aimedAt || port.candidate
            ? AppColors.accent
            : port.live ? AppColors.live : AppColors.wire
        // **A candidate grows rather than changing colour alone.** A colour
        // difference on something nine pixels across is one a good many
        // people cannot see at all.
        scale: port.aimedAt ? 1.8 : port.candidate ? 1.4 : 1.0

        Behavior on scale {
            NumberAnimation {
                duration: Kirigami.Units.shortDuration
                easing.type: Easing.OutCubic
            }
        }
    }

    // **The merge marker**: a ring around a connector that more than one
    // link arrives at. The engine sums them into the one signal the lane
    // carries, and a person who cannot see that a lane sums cannot tell a
    // mistake from a mix.
    Rectangle {
        id: mergeRing

        anchors.centerIn: dot
        visible: port.merging
        width: dot.width * 2
        height: width
        radius: width / 2
        color: "transparent"
        border.color: AppColors.wire
        border.width: 1
        Accessible.ignored: true
    }

    // **Ctrl and drag is how a wire is drawn**. Without the modifier
    // the press is handed back, so that it reaches the tile underneath: the
    // dot straddles the edge, and half of it is tile that has to go on
    // being selectable and draggable like the rest.
    //
    // The target is a good deal bigger than the ring, which is nine pixels
    // across on a 100 % screen — a thing to aim at, not a thing to hit.
    MouseArea {
        id: grab

        anchors.centerIn: dot
        width: Kirigami.Units.gridUnit
        height: width
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.CrossCursor

        onPressed: mouse => {
            if ((mouse.modifiers & Qt.ControlModifier) === 0) {
                mouse.accepted = false;
                return;
            }
            port.wireStarted();
        }

        onPositionChanged: mouse => {
            const at = grab.mapToItem(null, mouse.x, mouse.y);
            port.wireMoved(at.x, at.y);
        }

        onReleased: mouse => {
            const at = grab.mapToItem(null, mouse.x, mouse.y);
            port.wireDropped(at.x, at.y);
        }

        onCanceled: port.wireCancelled()
    }

    HoverHandler {
        id: hover

        enabled: port.name !== ""
    }

    Accessible.role: Accessible.StaticText
    Accessible.name: port.name
    Accessible.ignored: port.name === ""

    QQC2.ToolTip.text: port.name
    QQC2.ToolTip.visible: hover.hovered && port.name !== ""
    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
}
