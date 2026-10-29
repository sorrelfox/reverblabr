// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <string_view>

namespace rb::pw
{

/// Version of the libpipewire headers the engine was compiled against.
[[nodiscard]] std::string_view headersVersion() noexcept;

/// Version of the libpipewire library loaded at run time.
[[nodiscard]] std::string_view libraryVersion() noexcept;

} // namespace rb::pw
