// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.ki18n

// What has gone wrong with the session, across the top of the canvas.
//
// **A strip, not a dialog.** Nothing here needs an answer before anything
// else can happen: the audio system being away, or a session refusing to
// start, is something to read and act on when ready. A modal box would stop
// the person doing the one thing that might help.
//
// **It offers the action, not the diagnosis.** Where there is something
// worth trying again — which is exactly when the audio system is missing —
// the banner carries a Retry beside the sentence, so the answer is where the
// question is.
Kirigami.InlineMessage {
    id: banner

    position: Kirigami.InlineMessage.Position.Header
    // Nothing is broken when the audio system is merely away, so it is a
    // warning; a refusal by the engine is a failure and reads as one.
    type: ErrorSurface.aboutConnection ? Kirigami.MessageType.Warning : Kirigami.MessageType.Error
    text: ErrorSurface.message
    showCloseButton: true

    actions: [
        Kirigami.Action {
            text: KI18n.i18nc("@action:button try to reach the audio system again", "Retry")
            icon.name: "view-refresh"
            visible: ErrorSurface.canRetry
            onTriggered: ErrorSurface.retry()
        }
    ]

    // **A Binding object, not a plain one.** The close button writes to
    // `visible` itself, and a plain binding written to is a binding gone for
    // good; this one is applied again the next time there is something to
    // say.
    Binding {
        target: banner
        property: "visible"
        value: ErrorSurface.visible
        restoreMode: Binding.RestoreBindingOrValue
    }

    // Putting a message away is the person saying they have read it, which
    // is something the surface has to know: otherwise the next thing that
    // changes brings the same sentence back.
    onVisibleChanged: {
        if (!banner.visible && ErrorSurface.visible) {
            ErrorSurface.dismiss();
        }
    }
}
