// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "pw/version.h"

#include <pipewire/version.h>

namespace rb::pw
{

std::string_view headersVersion() noexcept
{
    return pw_get_headers_version();
}

std::string_view libraryVersion() noexcept
{
    return pw_get_library_version();
}

} // namespace rb::pw
