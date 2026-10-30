// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

// A block's help page, as data: what the help window shows for one kind
// of block (docs/adr/0029-block-help.md).
//
// **Words and nothing else.** How a page looks is the window's, from the
// design tokens, so every page looks the same in both appearances and a
// page's author writes text rather than layout. The block's name and icon
// come from BlockPresentation, as the tile's do.
QtObject {
    /// The kind of block the page is about, its type id.
    property string typeId

    /// What the block is for, in a sentence or two: the line under the
    /// title, which is all some readers will read.
    property string summary

    /// The page's parts and settings, in reading order.
    default property list<HelpSection> sections
}
