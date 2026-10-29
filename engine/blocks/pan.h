// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/paramstore.h"
#include "dsp/stereogains.h"

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// Places a sound anywhere between the left and the right.
///
/// **One sound, put somewhere.** A stereo input is folded to mono first —
/// the two sides averaged, so a sound that was already in the middle
/// arrives at the level it had — and that one signal is then spread across
/// the two sides by dsp::panGains(). That is what separates it from
/// Balance, which keeps two sides and trims one of them.
///
/// **Constant power**: wherever it is put, the sound keeps its loudness.
/// At the centre each side carries it at −3.01 dB; at an end, one side
/// carries all of it. The two gains glide over kSmoothingMs, so dragging
/// the control while something plays does not click.
class Pan final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "effect.pan";

    /// How long a change takes to arrive. Long enough not to step the
    /// signal, short enough not to be heard as a fade.
    static constexpr float kSmoothingMs = 20.0f;

    /// The ends of the control: −100 is all the way left, +100 all the way
    /// right. What a person sets, and what the file holds.
    static constexpr float kFullyLeft = -100.0f;
    static constexpr float kFullyRight = 100.0f;

    enum Param : std::uint32_t {
        Position = 0, ///< −100 (left) … 0 (centre) … +100 (right).
    };

    Pan();

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

    /// The gains the current setting asks for, which the audio glides to.
    [[nodiscard]] dsp::StereoGains targetGains() const noexcept RB_NONBLOCKING;

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    dsp::StereoGainSmoother m_gains;
    bool m_prepared = false;
};

} // namespace rb::blocks
