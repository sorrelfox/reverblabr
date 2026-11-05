// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

/// The Pan block's interface: the face its tile draws, the panel its
/// settings open in and its help page, as BlockRegister lists them
/// (ADR-0035). One it leaves out is the generic one.
BlockUi {
    typeIds: ["effect.pan"]

    /// The Pan face: the same drawing as Balance's, for where one sound is
    /// placed rather than which side is trimmed.
    face: StereoPositionFace {
        typeId: "effect.pan"
        paramId: "pan"
        caption: KI18n.i18nc("@label what the setting on a Pan tile is", "pan")
        sliderName: KI18n.i18nc("@label:slider", "Pan")
    }

    /// The Pan panel: Balance's panel, moving the Pan's setting.
    panel: StereoPositionPanel {
        paramId: "pan"
        title: KI18n.i18nc("@label:slider", "Pan")
    }

    help: PanHelp {}
}
