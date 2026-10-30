// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.ki18n

// Every setting a block has, generated from what the block says about
// itself.
//
// **A block declares its parameters once.** This is the whole reason a new
// block needs no new interface code: the descriptors drive the DSP, the
// presets, the session file and this panel, and none of them keeps a second
// copy to fall out of step with the others.
//
// It is also the accessible fallback for a block that has a panel of its
// own, which is why it has to stay good enough to use rather than being a
// developer's view.
ColumnLayout {
    id: panel

    required property string blockId
    readonly property string typeId: SessionModel.block(panel.blockId).type ?? ""
    /// Re-read when the session changes, so a value set anywhere — undo, a
    /// second window, the block's own panel — is what the controls show.
    readonly property var values: SessionModel.paramRevision >= 0 ? SessionModel.params(panel.blockId) : ({})

    spacing: Kirigami.Units.smallSpacing

    Repeater {
        model: BlockCatalogue.params(panel.typeId)

        delegate: ParamRow {
            required property var modelData

            // **Not shown in the generic panel** when the block says so: its
            // own face already shows it, or it is for diagnostics. A layout
            // leaves a hidden row out by itself; a height set here as well
            // fought the layout for it (a binding loop).
            visible: modelData.hidden !== true

            blockId: panel.blockId
            descriptor: modelData
            currentValue: panel.values[modelData.id]
            Layout.fillWidth: true
        }
    }

    QQC2.Label {
        visible: BlockCatalogue.params(panel.typeId).length === 0
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: AppColors.muted
        text: BlockCatalogue.ready
            ? KI18n.i18nc("@info:placeholder", "This block has nothing to set")
            : KI18n.i18nc("@info:placeholder while the engine is still answering", "Reading the block catalogue…")
    }
}
