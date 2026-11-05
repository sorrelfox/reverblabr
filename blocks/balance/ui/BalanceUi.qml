// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

/// The Balance block's interface: the face its tile draws, the panel its
/// settings open in and its help page, as BlockRegister lists them
/// (ADR-0035). One it leaves out is the generic one.
BlockUi {
    typeIds: ["effect.balance"]

    /// The Balance face: where it is set, "C" or "L 30", and a slider.
    face: StereoPositionFace {
        typeId: "effect.balance"
        paramId: "balance"
        caption: KI18n.i18nc("@label what the setting on a Balance tile is", "balance")
        sliderName: KI18n.i18nc("@label:slider", "Balance")
    }

    /// The Balance panel: a side and how far, "L 30", which the
    /// generic panel would write as a signed number.
    panel: StereoPositionPanel {
        paramId: "balance"
        title: KI18n.i18nc("@label:slider", "Balance")
    }

    help: BalanceHelp {}
}
