// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

/// The Dynamics Processor block's interface: the face its tile draws, the
/// panel its settings open in and its help page, as BlockRegister lists
/// them (ADR-0035). One it leaves out is the generic one.
BlockUi {
    typeIds: ["effect.dynamics"]

    /// The Dynamics face: the block declares a curve face, and the curve is
    /// what it has to say for itself.
    face: DynamicsFace {}

    /// The Dynamics panel: a transfer curve with four points on it, which is
    /// a shape rather than a list of settings — and the points are four of
    /// the nine parameters, so a generated row per parameter would say the
    /// same things twice.
    panel: DynamicsPanel {}

    help: DynamicsHelp {}
}
