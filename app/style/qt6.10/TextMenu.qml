// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Templates as T

// The text menu on Qt 6.10, which has none: a right click on a field
// opens nothing, as it does in Qt's own styles there. An empty menu would
// still open, so there is no menu at all.
//
// It stands in for ../TextMenu.qml, under the same name, so that the
// fields need not know which Qt they run on.
QtObject {
    /// The text item; unused here.
    property Item editor

    readonly property T.Menu menu: null
    /// Whether a right click opens anything: not on this Qt.
    readonly property bool available: false

    function popup(position: point) {
    }
}
