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

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// A gain and nothing else, up to ten times.
///
/// **Per cent means amplitude**, as on Volume: 100 % is the sound as it
/// arrived, 200 % twice it (6.02 dB), 1000 % ten times (20 dB).
///
/// **Nothing else.** No limiter, no mute, no overdrive: the output is the
/// input times the gain, with no latency. A gain that takes a loud sound
/// past full scale sends it on past full scale, and whatever comes next,
/// a device or a file, clips it. That is the difference from Volume, whose
/// limiter keeps it below; this block is for a sound too quiet to need one.
///
/// A change of the gain glides in over kGainSmoothingMs, so that moving
/// the slider does not step the signal.
class Amplify final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "effect.amplify";

    static constexpr float kHighestGain = 10.0f;
    static constexpr float kGainSmoothingMs = 20.0f;

    enum Param : std::uint32_t {
        Gain = 0, ///< 0…10 as an amplitude; the interface shows 0…1000 %.
    };

    Amplify();

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

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    dsp::LinearSmoother m_gain;
    bool m_prepared = false;
};

} // namespace rb::blocks
