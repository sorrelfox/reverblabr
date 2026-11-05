// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/limiter.h"
#include "dsp/paramstore.h"
#include "dsp/smoothing.h"

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// A gain, for matching one source against another.
///
/// **Per cent means amplitude.** 100 % is the signal as it arrived, 200 % is
/// twice it — which is 6.02 dB — and 400 % is four times it. A person
/// turning a source up to match another is thinking in "twice as loud", not
/// in decibels, and the number in the file is the number the audio uses.
///
/// **Overdrive is the top of the slider, not a multiplier.** Turning it off
/// again brings the gain down to what the smaller range allows, rather than
/// leaving a value nobody can see.
///
/// **The limiter is always in the signal path**, so the latency is a
/// constant the block can report and the sound does not change shape when it
/// engages. Below the ceiling it applies a gain of exactly one: at 100 % the
/// output is the input delayed by the reported latency and nothing else.
class Volume final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "effect.volume";

    /// The stable ids of the overdrive settings, and what each one makes the
    /// top of the slider.
    static constexpr std::string_view kOverdriveOff = "off";
    static constexpr std::string_view kOverdrive2x = "2x";
    static constexpr std::string_view kOverdrive3x = "3x";
    static constexpr std::string_view kOverdrive4x = "4x";

    /// How long the gain takes to arrive, and how long a mute takes. Long
    /// enough not to step the signal, short enough not to be a delay.
    static constexpr float kGainSmoothingMs = 20.0f;
    static constexpr float kMuteSmoothingMs = 10.0f;

    enum Param : std::uint32_t {
        Gain = 0, ///< 0…4 as an amplitude; the interface shows a percentage.
        Overdrive, ///< One of the four ids above.
        Muted,
    };

    Volume();

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

    /// The largest gain the current overdrive setting allows.
    [[nodiscard]] float maximumGain() const noexcept RB_NONBLOCKING;

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();
    /// Brings the stored gain down to what the overdrive allows, so that
    /// what is saved is what is heard.
    void clampToOverdrive();

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    dsp::LinearSmoother m_gain;
    dsp::LinearSmoother m_mute;
    dsp::LookaheadLimiter m_limiter;
    bool m_prepared = false;
};

} // namespace rb::blocks
