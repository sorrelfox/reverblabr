// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/paramstore.h"
#include "dsp/smoothing.h"

#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// Rearranges the two sides of a stereo signal: swaps them, folds them,
/// copies one to both, silences or inverts one.
///
/// **Every action is a 2×2 matrix**, each output side a weighted sum of the
/// two input sides. That is what makes a change of action safe to make while
/// sound is playing: the four weights glide from the old matrix to the new
/// one over kCrossfadeMs, which is a crossfade between the two outputs
/// without having to work out both.
///
/// The folds are averages, not sums: Mono is (L+R)/2 and Difference is
/// (L−R)/2, both on both sides, so a sound in the middle keeps its level in
/// Mono and nothing is made louder than the loudest side. The two add back
/// up to the left side, which is a way to check them.
class Channels final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "effect.channels";

    /// How long a change of action takes: long enough not to click, short
    /// enough to sound like a switch.
    static constexpr float kCrossfadeMs = 10.0f;

    /// Output side from input side: `left = leftFromLeft·L + leftFromRight·R`.
    struct Matrix {
        float leftFromLeft = 1.0f;
        float leftFromRight = 0.0f;
        float rightFromLeft = 0.0f;
        float rightFromRight = 1.0f;

        friend bool operator==(const Matrix &, const Matrix &) = default;
    };

    /// One action: its stable id, which the file holds, and its matrix.
    struct Action {
        std::string_view id;
        Matrix matrix;
    };

    enum Param : std::uint32_t {
        ActionParam = 0, ///< An index into actions().
    };

    Channels();

    [[nodiscard]] static std::unique_ptr<dsp::Block> make();

    /// Every action, in the order the interface offers them. The first is
    /// the default, and leaves the signal as it is.
    [[nodiscard]] static std::span<const Action> actions() noexcept;

    [[nodiscard]] const dsp::BlockInfo &info() const noexcept override
    {
        return m_info;
    }

    [[nodiscard]] std::span<const dsp::ParamDescriptor> params() const noexcept override
    {
        return m_descriptors;
    }

    [[nodiscard]] std::expected<void, dsp::BlockError> prepare(const dsp::PrepareSpec &spec) override;
    void process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING override;
    void reset() noexcept RB_NONBLOCKING override;
    void applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING override;

    [[nodiscard]] std::expected<void, dsp::BlockError> setParam(std::string_view id,
                                                                const dsp::ParamValue &value) override;
    [[nodiscard]] dsp::ParamValue param(std::string_view id) const override;
    [[nodiscard]] dsp::ParamValueMap saveState() const override;
    [[nodiscard]] std::expected<void, dsp::BlockError> loadState(const dsp::ParamValueMap &state) override;

    /// The matrix the chosen action asks for, which the audio glides to.
    [[nodiscard]] Matrix targetMatrix() const noexcept RB_NONBLOCKING;

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();
    void resetTo(const Matrix &matrix) noexcept RB_NONBLOCKING;

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    /// The four weights, in the order of Matrix's members.
    std::array<dsp::LinearSmoother, 4> m_weights;
    bool m_prepared = false;
};

} // namespace rb::blocks
