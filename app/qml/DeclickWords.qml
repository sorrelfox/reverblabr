// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.


import QtQuick
import org.kde.ki18n

// The Declick block's words, for its face and its panel alike.
QtObject {
    /// A threshold: "3.0 dB".
    function decibels(db: real): string {
        return KI18n.i18nc("@info a Declick threshold in decibels, '3.0 dB'", "%1 dB",
                           Number(db).toLocaleString(Qt.locale(), 'f', 1));
    }

    /// How many clicks a running block has taken out.
    function clicks(count: int): string {
        return count === 0 ? KI18n.i18nc("@info a running Declick that has found nothing yet", "No clicks taken out yet")
                           : KI18n.i18ncp("@info how many clicks a Declick has taken out since it started",
                                          "%1 click taken out", "%1 clicks taken out", count);
    }
}
