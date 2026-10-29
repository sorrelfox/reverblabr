// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// A menu row that destroys something, written in the danger colour
// as the prototype writes its one such row, so that it is read before it
// is pressed.
//
// The Reverblabr style's MenuItem knows this type and draws its words in
// `AppColors.danger`. The desktop's style writes a row in the theme's text
// colour, so that is what this sets while following the system; the
// token is the desktop's own negative text colour there.
QQC2.MenuItem {
    Kirigami.Theme.textColor: AppColors.danger
}
