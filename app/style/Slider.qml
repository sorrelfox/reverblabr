// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Templates as T
import io.github.reverblabr

// A slider, as the prototype draws one ([ADR-0012]): the thin groove
// and the ring a tile's face has, in the accent, in a row sixteen pixels
// high as a panel's is.
//
// **The prototype's own slider, not the browser's.** A panel's slider in
// the prototype is the browser's range input tinted with the accent, and
// looks like whichever browser shows it; the one slider the prototype draws
// for itself is the one on a Volume tile. So every slider in the window is
// that one: a panel's at sixteen pixels, a face's at twelve and in its
// block's tint (FaceSlider), one groove and one ring between them
// (SliderGroove, SliderHandle).
//
// No tick marks, as the prototype has none. (`slider` rather than the
// style's usual `control` as the id: the groove and the ring each have a
// property of that name, and `control: control` would name their own.)
T.Slider {
    id: slider

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitHandleWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitHandleHeight + topPadding + bottomPadding)

    padding: 0
    opacity: slider.enabled ? 1 : AppMetrics.disabledOpacity

    background: SliderGroove {
        control: slider
    }

    handle: SliderHandle {
        control: slider
        responsive: true

        FocusRing {
            shown: slider.visualFocus
            controlRadius: parent.radius
        }
    }
}
