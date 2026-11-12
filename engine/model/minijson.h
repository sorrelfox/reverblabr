// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rb::model
{

struct JsonMember;

/// **Just enough JSON for a model's configuration**, which comes from a
/// folder and so from anybody. The caller's text is at most kMostJsonBytes;
/// nesting and the number of values are limited, so neither a deep nor a
/// wide file costs more than its size allows. The plain layers have no
/// parser, and this one has only these bounds to keep.
struct JsonValue {
    enum class Kind : std::uint8_t {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<JsonValue> items;
    /// A vector may hold a type that is still incomplete here; a std::pair
    /// of one may not, and Clang with GCC 16's library refuses it.
    std::vector<JsonMember> members;

    /// The member called \a key of an object, or null.
    [[nodiscard]] const JsonValue *find(std::string_view key) const noexcept;
};

/// One member of an object: its name and its value.
struct JsonMember {
    std::string name;
    JsonValue value;
};

inline constexpr std::size_t kMostJsonBytes = 64 * 1024;

/// \a text as JSON, or nothing when it is not, or is out of bounds.
[[nodiscard]] std::optional<JsonValue> parseJson(std::string_view text);

} // namespace rb::model
