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
    /// In place of integer, for a tensor of more than one.
    std::vector<std::int64_t> integers;
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

// **A voice conversion set small enough to write by hand**: the same inputs
// and outputs as the real graphs, and a little arithmetic in place of their
// weights. The sound goes through all three: the encoder gives each frame's
// average, and the voice holds it for a frame.

/// What a tiny graph does wrong, to test what happens then.
enum class TinyFault : std::uint8_t {
    None,
    NotSound, ///< Everything it gives is not a number.
    TooLittle, ///< Fewer frames than the window has, or too little of a run.
    Slow, ///< It takes its time, about 100 ms a run.
};

/// `source` of [1, 1, T] to `embed` of [1, T / 320, bands].
[[nodiscard]] std::string tinyContentEncoder(TinyFault fault = TinyFault::None, std::int64_t bands = 768);
/// The other export: `input_values` and `attention_mask` to `hidden_states`.
[[nodiscard]] std::string tinyMaskedContentEncoder();
/// `input` of [1, 128, T] to `output` of [1, T, 360], sure of one pitch.
[[nodiscard]] std::string tinyPitchEstimator(std::int64_t likeliest, TinyFault fault = TinyFault::None);

struct TinyVoice {
    TinyFault fault = TinyFault::None;
    /// Samples a frame makes: its rate over a hundred.
    std::int64_t perFrame = 400;
    /// The other export's names, and no `rnd`.
    bool otherNames = false;
    /// Sound the pitch it is told, in kHz, in place of the features.
    bool sayThePitch = false;
    /// Trained with no pitch: only `phone`, `phone_lengths`, `ds` and `rnd`.
    bool noPitch = false;
    /// Features of this size.
    std::int64_t bands = 768;
};

/// A voice: `phone`, `phone_lengths`, `pitch`, `pitchf`, `ds` and `rnd` to
/// `audio`, which is what it makes of the features.
[[nodiscard]] std::string tinyVoice(const TinyVoice &voice = {});

} // namespace rb::test
