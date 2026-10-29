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

// What a plugin block shows on the canvas: which plugin, and what its
// host is doing — in red when it crashed or stopped responding, which is
// when the sound goes around it.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(face.blockId) : ({})
    /// Which plugin: nothing set is the default one, which the catalogue
    /// names. An id, not a sentence.
    /// The block's type, and so the plugin's format: `plugin.<format>`.
    readonly property string blockType: SessionModel.revision >= 0
        ? (SessionModel.block(face.blockId).type ?? InstalledPlugins.blockType("builtin"))
        : InstalledPlugins.blockType("builtin")
    readonly property string format: face.blockType.substring(face.blockType.indexOf(".") + 1)
    readonly property string pluginId: face.params.plugin
        ?? (BlockCatalogue.describeParam(face.blockType, "plugin")["default"] ?? "")
    /// What the plugin calls itself, as the scan found it, or its id
    /// for one this machine has not got.
    readonly property string pluginName: face.pluginId === ""
        ? KI18n.i18nc("@label a plugin block with no plugin chosen yet", "Choose a plugin")
        : (InstalledPlugins.plugin(face.format, face.pluginId).name ?? face.pluginId)
    /// What the block publishes while the session runs; a test sets it.
    property var published: Meters.frame >= 0 && Meters.live ? (Meters.levels(face.blockId).values ?? []) : []
    readonly property int health: Math.round(face.published[0] ?? 0)
    readonly property bool failed: PluginWords.failed(face.health)

    readonly property alias titleLabel: titleText
    readonly property alias statusLabel: statusText

    implicitWidth: Kirigami.Units.gridUnit * 10
    implicitHeight: Kirigami.Units.gridUnit * 3

    Accessible.role: Accessible.StaticText
    Accessible.name: titleText.text + ", " + statusText.text

    ColumnLayout {
        anchors {
            left: parent.left
            right: parent.right
            verticalCenter: parent.verticalCenter
            leftMargin: AppMetrics.faceMargin
            rightMargin: AppMetrics.faceMargin
        }
        spacing: AppMetrics.faceSpacing

        ElidedLabel {
            id: titleText

            text: face.pluginName
            font: AppFonts.tileTitle
            color: AppColors.text
            elide: Text.ElideRight
            Layout.fillWidth: true
            Accessible.ignored: true
        }

        ElidedLabel {
            id: statusText

            text: PluginWords.status(face.health)
            font: AppFonts.tileCaption
            color: face.failed ? AppColors.dangerText : AppColors.muted
            elide: Text.ElideRight
            Layout.fillWidth: true
            Accessible.ignored: true
        }
    }
}
