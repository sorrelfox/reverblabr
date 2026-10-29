// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QString>

namespace rb::core
{

/// Versions of the libraries the engine runs on, for the About page and for
/// diagnostics.
struct EngineInfo {
    QString pipeWireHeaders; ///< libpipewire headers the engine was built with
    QString pipeWireLibrary; ///< libpipewire loaded at run time
};

[[nodiscard]] EngineInfo engineInfo();

} // namespace rb::core
