// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// Where a Sync delay sits on a slider, and how it is written. The
// tile and the panel both use it, so a delay is the same place on both.
//
// **Square law, not linear.** The delays people line things up with are
// mostly the short ones — a few milliseconds between two microphones, a
// few tens between a picture and its sound — and a linear slider over two
// seconds puts all of them in its first few pixels. With the position the
// square root of the share of the range, 120 ms is a quarter of the way
// along and a single pixel near the start is a fraction of a millisecond.
QtObject {
    id: scale

    /// The longest delay there is, in milliseconds; the block's own range.
    readonly property real maximumMs: 2000

    /// Where \a ms sits, from 0 to 1.
    function positionOf(ms: real): real {
        return Math.sqrt(Math.max(0, Math.min(scale.maximumMs, ms)) / scale.maximumMs);
    }

    /// The delay at \a position, from 0 to 1, to a hundredth of a
    /// millisecond, which is finer than a sample at any rate and no finer
    /// than anybody types.
    function msAt(position: real): real {
        const clamped = Math.max(0, Math.min(1, position));
        return Math.round(clamped * clamped * scale.maximumMs * 100) / 100;
    }

    /// The number alone, as exact as the setting is and no more: "120",
    /// "125.3", "12.35".
    function number(ms: real, locale: var): string {
        const hundredths = Math.round(ms * 100);
        const decimals = hundredths % 100 === 0 ? 0 : (hundredths % 10 === 0 ? 1 : 2);
        return Number(hundredths / 100).toLocaleString(locale, 'f', decimals);
    }

    /// A delay as a reader reads it: "120 ms".
    function text(ms: real, locale: var): string {
        return KI18n.i18nc("@info a delay in milliseconds", "%1 ms", scale.number(ms, locale));
    }
}
