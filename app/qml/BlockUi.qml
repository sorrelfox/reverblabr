// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

/// What the interface shows of a block, as the block's folder says it
/// (blocks/<folder>/ui/, ADR-0035): the type ids it is for, the face a tile
/// draws, the panel the settings open in and the help page. One it leaves
/// out is the generic face or panel, which is not a lesser one, or the help
/// window's page for a block without one. BlockRegister lists them all.
QtObject {
    required property list<string> typeIds
    property Component face: null
    property Component panel: null
    property Component help: null
}
