// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/biquad.h"
#include "dsp/block.h"
#include "dsp/paramstore.h"
#include "dsp/smoothing.h"

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// Gives a mono recording a sense of width without breaking it.
///
/// The input is folded to one signal x, the two sides averaged. Above the
/// crossover a copy of it, d, is held back by a few milliseconds, and the
/// two sides are
///
///     left  = x + d
///     right = x − d
///
/// **So the two sides always add back up to exactly the input**: whatever d
/// is, (left + right) / 2 = x. Played on one speaker, or folded to mono by
/// anything downstream, the sound is the one that went in. Heard in stereo,
/// the high frequencies differ between the two sides — each side's comb is
/// the other's gaps — and the ear hears that as width.
///
/// **Below the crossover d is nothing**, so the lows stay in the middle:
/// bass spread across the two sides sounds vague and wastes a speaker's
/// power. d comes from a second-order high pass at the crossover, which
/// glides in octaves over kCrossoverSmoothingMs when the crossover moves.
class MonoEnhancer final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "effect.monoenhancer";

    static constexpr float kMinimumCrossover = 100.0f;
    static constexpr float kMaximumCrossover = 2000.0f;
    static constexpr float kDefaultCrossover = 500.0f;
    static constexpr float kCrossoverSmoothingMs = 30.0f;

    /// How far d is held back. Inside the time the ear fuses two arrivals
    /// into one sound, so it is heard as width and never as an echo.
    static constexpr float kDelayMs = 12.0f;
    /// How much of the held-back copy goes into each side. At 0.6 the two
    /// sides above the crossover correlate at (1 − 0.36) / (1 + 0.36), about
    /// 0.47 where a mono signal is 1, and each side's comb swings between
    /// +4.1 and −8.0 dB rather than cutting whole notes out.
    static constexpr float kDepth = 0.6f;

    enum Param : std::uint32_t {
        Crossover = 0, ///< Hz, 100 … 2000, logarithmic.
    };

    MonoEnhancer();

    [[nodiscard]] static std::unique_ptr<dsp::Block> make();

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

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();
    void design(float octaves) noexcept RB_NONBLOCKING;
    void settle() noexcept RB_NONBLOCKING;
    void clearHistory() noexcept RB_NONBLOCKING;

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    double m_sampleRate = 48000.0;
    /// The crossover as log2 of its frequency, so a glide moves evenly in
    /// pitch rather than rushing through the low end.
    dsp::OnePoleSmoother m_octaves;
    dsp::SvfCoefficients m_highPass;
    dsp::Svf m_filter;
    /// The held-back copy: a ring the length of the delay, made in prepare().
    std::vector<float> m_delay;
    std::size_t m_delayIndex = 0;
    bool m_prepared = false;
};

} // namespace rb::blocks
