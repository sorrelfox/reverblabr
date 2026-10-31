// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.ki18n

// "Limit acting now: 3 dB", while a limit is taking anything off.
//
// **Told once, not at each change.** A limit moves many times a second, so a
// screen reader hears it when it first passes 3 dB and when it ends.
QQC2.Label {
    id: line

    /// The block whose limit it is, and which of its published values says
    /// how far the limit took the gain down.
    required property string blockId
    required property int slot

    readonly property var levels: Meters.frame >= 0 ? Meters.levels(line.blockId) : ({})
    readonly property real reduction: (line.levels.values ?? [])[line.slot] ?? 0
    property bool told: false

    visible: line.reduction >= 0.5
    text: KI18n.i18nc("@info:status how far a limit is taking the sound down", "Limit acting now: %1 dB",
                      Math.round(line.reduction))
    color: AppColors.text
    font: AppFonts.small

    onReductionChanged: {
        if (!line.told && line.reduction >= 3) {
            line.told = true;
            Announcer.say(line, "limit:" + line.blockId, line.text);
        } else if (line.told && line.reduction < 0.5) {
            line.told = false;
            Announcer.say(line, "limitEnded:" + line.blockId,
                          KI18n.i18nc("@info:status a limit has stopped taking the sound down", "The limit is no longer acting"));
        }
    }
}
