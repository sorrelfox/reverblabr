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

#include <array>
#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// Repeats the sound after a delay, each repeat quieter and duller than the
/// one before, mixed in behind the original.
///
/// **A feedback delay with a filter in the loop.** What comes back from the
/// line is played, and also fed into it again, through a low pass and
/// turned down by the feedback: so the first repeat is the sound as it was,
/// and every one after it has lost more of its top and more of its level.
/// The loop's gain is the feedback times the filter's, and the filter never
/// lifts anything, so at the most feedback allows, 95 %, the repeats still
/// die away.
///
/// **The repeats are the effect, not a latency.** The original goes
/// straight through, so the block reports no latency and nothing is lined
/// up with its echoes.
///
/// **A new delay time fades across**, over kFadeMs, from the repeats at the
/// old time to the repeats at the new one. Sliding the time instead would
/// bend the pitch of everything in the line, which is a different effect.
/// The feedback, the tone and the mix glide.
class Echo final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "effect.echo";

    static constexpr float kShortestTimeMs = 1.0f;
    static constexpr float kLongestTimeMs = 2000.0f;
    static constexpr float kDefaultTimeMs = 350.0f;
    static constexpr float kHighestFeedback = 95.0f;
    static constexpr float kDefaultFeedback = 40.0f;
    static constexpr float kLowestToneHz = 500.0f;
    static constexpr float kHighestToneHz = 20000.0f;
    static constexpr float kDefaultToneHz = 5000.0f;
    static constexpr float kDefaultMix = 35.0f;

    /// How long a change of delay time fades across.
    static constexpr float kFadeMs = 50.0f;
    /// How long the feedback and the mix take to arrive.
    static constexpr float kGlideMs = 20.0f;
    /// How long the tone takes, in octaves.
    static constexpr float kToneGlideMs = 30.0f;

    enum Param : std::uint32_t {
        Time = 0, ///< ms.
        Feedback, ///< Per cent of each repeat fed back in.
        Tone, ///< Hz, the low pass in the loop.
        Mix, ///< Per cent: 0 is the original alone, 100 the repeats alone.
    };

    Echo();

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

    /// The delay the repeats are at, or fading to, in samples.
    [[nodiscard]] std::uint32_t delaySamples() const noexcept RB_NONBLOCKING
    {
        return m_to;
    }

private:
    static constexpr std::size_t kChannels = 2;

    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();
    [[nodiscard]] std::uint32_t targetSamples() const noexcept RB_NONBLOCKING;
    void settle() noexcept RB_NONBLOCKING;
    void design(float octaves) noexcept RB_NONBLOCKING;

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    double m_sampleRate = 48000.0;

    /// Every channel's line, one after another, each m_length long.
    std::vector<float> m_line;
    std::size_t m_length = 1;
    std::size_t m_write = 0;

    /// The delay faded from and to, and how far the fade has gone.
    std::uint32_t m_from = 1;
    std::uint32_t m_to = 1;
    std::uint32_t m_fadeLength = 1;
    std::uint32_t m_fadeLeft = 0;

    dsp::LinearSmoother m_feedback;
    dsp::LinearSmoother m_mix;
    dsp::OnePoleSmoother m_toneOctaves;
    dsp::SvfCoefficients m_toneFilter;
    std::array<dsp::Svf, kChannels> m_tone;
    bool m_prepared = false;
};

} // namespace rb::blocks
