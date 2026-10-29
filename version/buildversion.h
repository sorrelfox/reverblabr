// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <string_view>

namespace rb
{

/// The release this build is of, MAJOR.MINOR.PATCH, as VERSION at the top
/// of the tree says.
[[nodiscard]] std::string_view version() noexcept;

/// Where this build is: the release, and between releases the commits since
/// its tag and the commit itself, `0.1.0+42.gabc1234`, with `.dirty` when
/// the tree had changes. A tree without git (a release tarball) says the
/// release alone. What About, `--version` and a bug report show.
[[nodiscard]] std::string_view versionFull() noexcept;

} // namespace rb
