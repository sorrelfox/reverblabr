// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

// **A stated fact, not a guess.** Whether a program that feeds a block says, by
// its `media.role`, one of `roles`. Following the wires back to the Application
// blocks: a program saying nothing means nothing.
QtObject {
    id: lane

    /// The block whose lane this reads.
    required property string blockId
    /// The roles that count.
    required property var roles

    readonly property bool declared: {
        if (SessionModel.revision < 0 || SessionModel.paramRevision < 0 || Inventory.revision < 0) {
            return false;
        }
        const wires = SessionModel.wires();
        const seen = [lane.blockId];
        for (let next = 0; next < seen.length; ++next) {
            for (const wire of wires) {
                if (wire.toBlock === seen[next] && seen.indexOf(wire.fromBlock) < 0) {
                    seen.push(wire.fromBlock);
                }
            }
        }
        return seen.some(id => SessionModel.blockType(id) === "source.application" && lane.says(id));
    }

    function says(sourceId: string): bool {
        const rule = Inventory.applicationRuleFrom(SessionModel.param(sourceId, "binary") ?? "",
                                                   SessionModel.param(sourceId, "flatpakId") ?? "",
                                                   SessionModel.param(sourceId, "name") ?? "");
        const streams = rule === "" ? [] : (Inventory.application(rule).streams ?? []);
        return streams.some(stream => lane.roles.indexOf(stream.role) >= 0);
    }
}
