// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "buildversion.h"

#include "rb_version_full.h"

namespace rb
{

std::string_view version() noexcept
{
    return RB_VERSION;
}

std::string_view versionFull() noexcept
{
    return RB_VERSION_FULL;
}

} // namespace rb
