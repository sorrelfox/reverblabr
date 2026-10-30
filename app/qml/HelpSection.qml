// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

// One part of a block's help page: a heading, its paragraphs and its list
// (docs/adr/0029-block-help.md, docs/dev/block-help.md).
//
// **Every paragraph and every point is a message of its own**, marked with
// KI18n where it is written, so a translator works through a page one
// paragraph at a time in the ordinary catalogue, and a sentence changed
// in English unsettles one message rather than the page.
QtObject {
    /// The heading. **A setting's is the panel's own name for it**, by the
    /// same message where the panel has one, so that it is the same word in
    /// every language.
    property string title

    /// Whether this is one setting, under the part before it, rather than a
    /// part of the page. A part is drawn as a heading of the page; a
    /// setting as a smaller one under it.
    property bool setting: false

    /// The text, a paragraph each; then a list of points, if any; then
    /// the paragraphs that follow the list.
    property list<string> paragraphs
    property list<string> points
    property list<string> closing

    /// **The other names the panel gives what this section explains**: a
    /// control called by its screen-reader name ("Silence threshold") where
    /// the row's label says "Silence below", the choices in a row (Stereo,
    /// Mono), a button whose words change (Pause, Resume). tst_blockhelp.qml
    /// checks every control in the panel is named by a title or here.
    property list<string> covers
}
