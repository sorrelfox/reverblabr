// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls as QQC2

// What an empty part of the window says while the task that fills it is
// still to come.
//
// **It says what belongs there**, not "coming soon": somebody looking at a
// half-built window should be able to tell whether they are looking at a
// gap or at a mistake.
QQC2.Label {
    horizontalAlignment: Text.AlignHCenter
    wrapMode: Text.WordWrap
    color: AppColors.muted
    font: AppFonts.small
}
