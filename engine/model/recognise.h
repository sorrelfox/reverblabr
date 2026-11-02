// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "model/description.h"

#include <string>

namespace rb::model
{

/// The name a configuration has in a folder.
inline constexpr std::string_view kConfigurationName = "reverblabr-model.json";

/// The path of \a folder on this machine. **A session keeps a folder under
/// the home as `~/...`**, so that it still means something on another account,
/// and only a shell knows that word: this is where it is turned back.
[[nodiscard]] std::string localFolder(const std::string &folder);

/// **What the folder \a folder holds**, untrusted (ADR-0031): its
/// configuration if it has one, or the family its files show. Nothing is
/// loaded and nothing in it runs. A refusal says why.
[[nodiscard]] Description recognise(const std::string &folder);

} // namespace rb::model
