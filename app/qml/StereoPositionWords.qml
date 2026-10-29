// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// How a place between the left and the right is said: a Balance's
// setting, and a Pan's. One set of words for the face and the panel, so the
// two cannot say the same setting differently.
//
// A position runs from −100 (all the way left) through 0 to +100 (all the
// way right), whole numbers, which is what the blocks store.
QtObject {
    /// The short form a tile shows: "C", "L 30", "R 30".
    function shortText(position: real): string {
        const whole = Math.round(position);
        if (whole === 0) {
            return KI18n.i18nc("@info short for centre, on a Balance or Pan tile", "C");
        }
        return whole < 0 ? KI18n.i18nc("@info short for how far to the left, 'L 30'", "L %1", -whole)
                         : KI18n.i18nc("@info short for how far to the right, 'R 30'", "R %1", whole);
    }

    /// The form a panel reads out: "Centre", "L 30", "R 30".
    function longText(position: real): string {
        return Math.round(position) === 0 ? KI18n.i18nc("@info a Balance or Pan set to the middle", "Centre")
                                          : shortText(position);
    }
}
