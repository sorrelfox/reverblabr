// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace rb::model
{

/// An input or an output of a graph: only what recognising a folder needs.
struct OnnxPort {
    std::string name;
    /// ONNX's element type: 1 is a 32-bit float.
    std::int32_t elementType = 0;
    /// -1 for a size the file leaves open.
    std::vector<std::int64_t> dims;
};

struct OnnxSummary {
    std::vector<OnnxPort> inputs;
    std::vector<OnnxPort> outputs;
};

enum class OnnxFault : std::uint8_t {
    Unreadable, ///< It could not be opened.
    TooLarge, ///< Larger than kMostModelBytes.
    NotOnnx, ///< The bytes are not a model.
    UnsafeReference, ///< External data outside the file's own folder.
};

/// The most a model file may hold: the format's own limit.
inline constexpr std::uint64_t kMostModelBytes = 1ULL << 31;

/// **Reads the head of the ONNX file \a path**, untrusted, without loading it
/// and without a protobuf library: what its graph takes and gives, and whether
/// it reaches outside \a folder for its tensors (ADR-0031).
///
/// Every length is held against what is left of the file, nesting is
/// bounded, and what is not needed is skipped unread.
[[nodiscard]] std::expected<OnnxSummary, OnnxFault> readOnnxHead(const std::string &path, const std::string &folder);

} // namespace rb::model
