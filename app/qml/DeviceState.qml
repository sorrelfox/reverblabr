// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick

// What one device block is pointed at, and whether that device is there.
//
// **The face and the panel are two views of one answer.** A tile that says
// "Headphones" while its panel says the device has gone is a window arguing
// with itself, so the reading is done once and both read it.
//
// **The two device blocks are mirror images**, and differ here only in which
// half of the inventory they look in.
QtObject {
    id: reading

    /// The block this is about.
    required property string blockId
    /// Whether it plays or records.
    required property bool output

    /// What the block's `device` parameter holds: a `node.name`, or the
    /// reference that follows whatever the system has made the default.
    readonly property string device: SessionModel.revision >= 0
        ? (SessionModel.param(reading.blockId, "device") ?? Inventory.defaultDevice)
        : Inventory.defaultDevice

    /// Whether the block has been pointed anywhere at all. A block whose
    /// device is empty is one somebody still has to choose for, which its
    /// face says out loud rather than showing a blank.
    readonly property bool unset: reading.device === ""
    readonly property bool followsDefault: reading.device === Inventory.defaultDevice

    /// The node the block actually reaches at the moment — for one
    /// following the default, whatever the default is now.
    readonly property string node: reading.followsDefault
        ? (reading.output ? Inventory.defaultOutput : Inventory.defaultInput)
        : reading.device

    /// What that device is called. The node name for a device the inventory
    /// has never heard of, which is what a session naming something nobody
    /// has plugged in yet looks like.
    readonly property string deviceName: Inventory.revision >= 0 && reading.node !== ""
        ? (reading.output ? Inventory.outputName(reading.node) : Inventory.inputName(reading.node))
        : ""

    /// That device's own channels, by the names its ports carry.
    readonly property var channels: Inventory.revision >= 0 && reading.node !== ""
        ? (reading.output ? Inventory.outputChannels(reading.node) : Inventory.inputChannels(reading.node))
        : []

    /// Whether the device is one of the ones the engine is listing.
    readonly property bool listed: Inventory.revision >= 0
        && (reading.output ? Inventory.hasOutput(reading.node) : Inventory.hasInput(reading.node))

    /// Whether the input is a wireless headset on its playback profile,
    /// whose microphone records nothing until the profile changes. Never
    /// for an output, which plays in either profile.
    readonly property bool playbackProfile: !reading.output && Inventory.revision >= 0 && reading.node !== ""
        && Inventory.inputOnPlaybackProfile(reading.node)

    readonly property var levels: Meters.frame >= 0 ? Meters.levels(reading.blockId) : ({})
    /// Whether the engine is publishing anything about this block at all.
    readonly property bool metered: Meters.live && (reading.levels.values ?? []).length > 0

    /// **Whether the device is there.** While the session runs the engine is
    /// the authority: it holds the link and publishes whether the device it
    /// was asked for is on the other end of it — the first of the values a
    /// device block publishes about itself. With nothing running there is no
    /// telemetry, and the lists are both all there is and the honest answer,
    /// because a device that is not listed is not one anything could reach.
    ///
    /// Before the engine has answered at all, and for a block nobody has
    /// chosen a device for, the answer is yes: a warning shown because
    /// nothing is known yet is a warning people learn to ignore.
    readonly property bool present: reading.metered
        ? reading.levels.values[0] >= 0.5
        : (!Inventory.ready || reading.unset || reading.listed)

    /// Points the block at a device, or at the system default.
    function choose(node: string) {
        SessionModel.setParam(reading.blockId, "device", node);
    }
}
