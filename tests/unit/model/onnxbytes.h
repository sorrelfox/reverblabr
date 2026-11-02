// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rb::test
{

/// **ONNX files written by hand**, so no binary and no tool stands in the
/// repository: a model is a few dozen protobuf fields, and these say exactly
/// what they are.
struct OnnxNode {
    std::string op;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    /// The field bytes of the node's attributes, already wrapped.
    std::string attributes;
};

struct OnnxValue {
    std::string name;
    std::vector<std::int64_t> dims;
    /// ONNX's element type: 1 float, 6 int32, 7 int64, 9 bool.
    std::int32_t element = 1;
};

/// A tensor kept in the file, of floats or of one int64.
struct OnnxWeight {
    std::string name;
    std::vector<std::int64_t> dims;
    std::vector<float> floats;
    bool isInt64 = false;
    std::int64_t integer = 0;
    bool isBool = false;
    /// Where its bytes are, for one kept in another file.
    std::string externalLocation;
};

struct OnnxGraph {
    std::vector<OnnxValue> inputs;
    std::vector<OnnxValue> outputs;
    std::vector<OnnxWeight> weights;
    std::vector<OnnxNode> nodes;
};

/// \a graph as the bytes of a model file.
[[nodiscard]] std::string onnxBytes(const OnnxGraph &graph);

/// `audio` of \a frames samples times \a gain, as `out`. \a rank says how
/// the input is shaped: 1 [T], 3 [1, 1, T]. \a channels 2 gives [2, T].
[[nodiscard]] std::string gainModel(float gain, std::int64_t frames, int rank = 1, std::int64_t channels = 1);
/// What came in a hop ago: `audio` goes to `state_out`, `state_in` to `out`.
[[nodiscard]] std::string oneHopLateModel(std::int64_t frames);
/// `audio` divided by zero: not a number and infinities for any sound.
[[nodiscard]] std::string notSoundModel(std::int64_t frames);
/// A pass-through by way of \a iterations turns of a loop that does nothing.
[[nodiscard]] std::string slowModel(std::int64_t frames, std::int64_t iterations);
/// A model that takes inputs by these names and does nothing with them.
[[nodiscard]] std::string modelWithInputs(const std::vector<std::string> &names);
/// A model whose one weight lives in the file at \a location.
[[nodiscard]] std::string externalDataModel(const std::string &location);

} // namespace rb::test
